#include "desktop_window_host.h"

#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "services/desktop_shell_service.h"

#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace xdstarter {
namespace {
QJsonObject rectangleJson(const QRect &rect) {
    return {{QStringLiteral("x"), rect.x()},
            {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}

qreal windowMetric(QWindow *window, const char *name, qreal fallback) {
    bool converted = false;
    const qreal value = window->property(name).toDouble(&converted);
    return converted && std::isfinite(value) ? std::clamp(value, 0.0, 128.0) : fallback;
}
} // namespace

DesktopWindowHost::DesktopWindowHost(QWindow &window, DesktopShellService &shell,
                                     DesktopPlatform &placement, DesktopTray &tray, QObject *parent)
    : QObject(parent), window_(&window), shell_(shell), placement_(placement), tray_(tray),
      previousQuitOnClose_(QGuiApplication::quitOnLastWindowClosed()) {
    window.installEventFilter(this);
    focusTimer_.setSingleShot(true);
    focusTimer_.setInterval(100);
    connect(&focusTimer_, &QTimer::timeout, this, &DesktopWindowHost::checkFocus);
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this,
            &DesktopWindowHost::scheduleFocusCheck);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            &DesktopWindowHost::scheduleFocusCheck);
    connect(&window, &QWindow::activeChanged, this, &DesktopWindowHost::scheduleFocusCheck);
    connect(&window, &QWindow::visibilityChanged, this, &DesktopWindowHost::scheduleFocusCheck);
    // Plain QWindow fixtures have no QML contract. Check before connecting so
    // headless tests and unsupported hosts produce no spurious Qt warnings.
    const auto *windowMeta = window.metaObject();
    if (windowMeta->indexOfSignal("dismissRequested()") >= 0) {
        window.setProperty("dismissalManaged", true);
        connect(&window, SIGNAL(dismissRequested()), this, SLOT(dismissWindow()));
    }
    const int blockedProperty = windowMeta->indexOfProperty("dismissalBlocked");
    if (blockedProperty >= 0) {
        const auto property = windowMeta->property(blockedProperty);
        if (property.hasNotifySignal()) {
            connect(&window, property.notifySignal(), this,
                    metaObject()->method(metaObject()->indexOfSlot("scheduleFocusCheck()")));
        }
    }
    connect(&tray_, &DesktopTray::toggleRequested, this, &DesktopWindowHost::toggleWindow);
    connect(&tray_, &DesktopTray::menuRequested, this, [this](QPoint) {
        if (!stopping_) {
            trayMenuActive_ = true;
            focusTimer_.stop();
            tray_.showContextMenu();
            trayMenuActive_ = false;
            scheduleFocusCheck();
        }
    });
    connect(&tray_, &DesktopTray::quitRequested, this, [this] {
        if (!stopping_)
            emit exitRequested();
    });
    connect(&tray_, &DesktopTray::registrationChanged, this, &DesktopWindowHost::updateRecovery);
    connect(&window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            pendingReveal_ = false;
            focusTimer_.stop();
            focusArmed_ = false;
            prepareWindow("prepareForDismissal");
            placementRequest_ = 0;
            shell_.invalidateStartButtonAnchor();
        }
    });
    connect(&shell_, &DesktopShellService::startButtonAnchorReady, this,
            [this](quint64 request, const DesktopAnchorResult &anchor) {
                if (stopping_ || request != placementRequest_ || !window_ ||
                    (!pendingReveal_ && (!window_->isVisible() ||
                                         window_->visibility() == QWindow::Minimized)))
                    return;
                const bool reveal = pendingReveal_;
                if (!anchor.monitorName.isEmpty()) {
                    for (auto *screen : QGuiApplication::screens()) {
                        if (screen->name() == anchor.monitorName && screen != window_->screen()) {
                            window_->setScreen(screen);
                            break;
                        }
                    }
                }
                if (reveal)
                    configureAppearance();
                // The panel and shadow use the same Qt render scale. UIA
                // trust/failure and an unknown taskbar DPI never change it.
                const qreal scale = std::clamp(window_->devicePixelRatio(), 1.0, 8.0);
                const int gap = qRound(windowMetric(window_, "screenGapDip", 8) * scale);
                const int inset = qRound(windowMetric(window_, "shadowInset", 0) * scale);
                const auto result = placement_.placeWindow(
                    window_->winId(), anchor, gap, !reveal && window_->isActive(), inset);
                anchorStatus_ =
                    result.success
                        ? QStringLiteral("面板距工作区左侧和底部均为 %1 DIP。")
                              .arg(windowMetric(window_, "screenGapDip", 8))
                        : QStringLiteral("原生定位不可用，使用 Qt 工作区边距回退。");
                if (!result.success && window_->screen()) {
                    const auto area = window_->screen()->availableGeometry();
                    const int logicalGap = qRound(windowMetric(window_, "screenGapDip", 8));
                    const int logicalInset = qRound(windowMetric(window_, "shadowInset", 0));
                    const int panelHeight = window_->height() - 2 * logicalInset;
                    window_->setPosition(area.left() + logicalGap - logicalInset,
                                         std::max(area.top() + logicalGap,
                                                  area.bottom() + 1 - logicalGap - panelHeight) -
                                             logicalInset);
                }
                const QString detail = result.message.isEmpty() ? anchor.message : result.message;
                if (!detail.isEmpty())
                    anchorStatus_ += QLatin1Char(' ') + detail;
                publishStatus();
                const QJsonObject evidence{
                    {QStringLiteral("trustedAnchor"), anchor.trusted},
                    {QStringLiteral("startButton"), rectangleJson(anchor.startButtonPhysicalRect)},
                    {QStringLiteral("workArea"), rectangleJson(anchor.workAreaPhysicalRect)},
                    {QStringLiteral("placedWindow"), rectangleJson(result.physicalRect)},
                    {QStringLiteral("dpi"), static_cast<int>(anchor.dpi)},
                    {QStringLiteral("renderDpr"), scale},
                    {QStringLiteral("gapPhysical"), gap},
                    {QStringLiteral("shadowInsetPhysical"), inset},
                    {QStringLiteral("placedBeforeReveal"), reveal},
                    {QStringLiteral("success"), result.success},
                    {QStringLiteral("message"), anchorStatus_}};
                qInfo().noquote() << "Desktop placement:"
                                  << QJsonDocument(evidence).toJson(QJsonDocument::Compact);
                if (reveal) {
                    pendingReveal_ = false;
                    window_->showNormal();
                    window_->raise();
                    window_->requestActivate();
                    prepareWindow("prepareForPresentation");
                    scheduleFocusCheck();
                }
            });
}

DesktopWindowHost::~DesktopWindowHost() {
    shutdown();
    QGuiApplication::setQuitOnLastWindowClosed(previousQuitOnClose_);
}

void DesktopWindowHost::start(const QImage &icon) {
    if (stopping_ || started_)
        return;
    const auto result = tray_.start(icon);
    updateRecovery(result.success && tray_.isAvailable(), result.message);
    started_ = true;
    showWindow();
}

void DesktopWindowHost::showWindow() {
    if (stopping_ || !window_)
        return;
    focusTimer_.stop();
    focusArmed_ = false;
    if (window_->visibility() == QWindow::Minimized)
        window_->hide();
    pendingReveal_ = !window_->isVisible();
    if (!pendingReveal_) {
        prepareWindow("prepareForPresentation");
        window_->raise();
        window_->requestActivate();
        scheduleFocusCheck();
        return;
    }
    requestPlacement();
}

void DesktopWindowHost::toggleWindow() {
    if (stopping_ || !window_)
        return;
    if (pendingReveal_ || (window_->isVisible() && window_->visibility() != QWindow::Minimized))
        dismiss(QStringLiteral("tray-toggle"));
    else
        showWindow();
}

bool DesktopWindowHost::eventFilter(QObject *object, QEvent *event) {
    if (object == window_ && event->type() == QEvent::Close && pendingReveal_) {
        pendingReveal_ = false;
        placementRequest_ = 0;
        shell_.invalidateStartButtonAnchor();
    }
    return QObject::eventFilter(object, event);
}

void DesktopWindowHost::dismissWindow() {
    dismiss(QStringLiteral("explicit"));
}

bool DesktopWindowHost::ownsFocus() const {
    if (!window_)
        return false;
    const auto *focused = QGuiApplication::focusWindow();
    // Menus rendered in their own native window remain part of this menu.
    // Item popups keep focus in the root QQuickWindow and need no special case.
    for (const QWindow *candidate = focused; candidate; candidate = candidate->transientParent()) {
        if (candidate == window_)
            return true;
    }
    return !focused && window_->isActive();
}

void DesktopWindowHost::scheduleFocusCheck() {
    if (stopping_ || !window_ || !window_->isVisible() ||
        window_->visibility() == QWindow::Minimized || trayMenuActive_)
        return;
    if (ownsFocus()) {
        focusArmed_ = true;
        focusTimer_.stop();
    } else if (focusArmed_) {
        // Coalesce popup/activation handoffs; never hide a window whose initial
        // activation was denied by Windows, or resurrect a previous dismissal.
        if (!focusTimer_.isActive())
            focusTimer_.start(100);
    }
}

void DesktopWindowHost::checkFocus() {
    if (stopping_ || !window_ || !window_->isVisible() ||
        window_->visibility() == QWindow::Minimized || trayMenuActive_ || !focusArmed_)
        return;
    if (ownsFocus())
        return;
    if (window_->property("dragActive").toBool() &&
        QGuiApplication::mouseButtons().testFlag(Qt::LeftButton)) {
        // Poll only during an outstanding external-focus drag. Qt delivers the
        // grabbed release even outside our surface; cleanup then runs once.
        focusTimer_.start(50);
        return;
    }
    // A genuinely external focus lasting beyond the grace period dismisses
    // even an open submenu. Edit mode remains enabled in the persisted model.
    dismiss(QStringLiteral("external-focus"));
}

void DesktopWindowHost::prepareWindow(const char *method) {
    if (!window_)
        return;
    const QByteArray signature = QByteArray(method) + "()";
    if (window_->metaObject()->indexOfMethod(signature.constData()) >= 0)
        QMetaObject::invokeMethod(window_, method, Qt::DirectConnection);
}

void DesktopWindowHost::dismiss(const QString &reason) {
    if (stopping_ || !window_)
        return;
    focusTimer_.stop();
    focusArmed_ = false;
    pendingReveal_ = false;
    placementRequest_ = 0;
    shell_.invalidateStartButtonAnchor();
    prepareWindow("prepareForDismissal");
    if (recoveryAvailable_)
        window_->hide();
    else
        window_->showMinimized();
    qInfo().noquote() << "Menu dismissal:"
                      << QJsonDocument(QJsonObject{{QStringLiteral("reason"), reason},
                                                   {QStringLiteral("trayRecovery"), recoveryAvailable_}})
                             .toJson(QJsonDocument::Compact);
}

void DesktopWindowHost::configureAppearance() {
    if (!window_)
        return;
    const auto result = placement_.configureMenuWindow(window_->winId());
    appearanceStatus_ = result.message;
    publishStatus();
    qInfo().noquote() << "Menu appearance:"
                      << QJsonDocument(QJsonObject{
                             {QStringLiteral("requestsAccepted"), result.success},
                             {QStringLiteral("nativeDecorationsDisabled"), result.nativeDecorationsDisabled},
                             {QStringLiteral("errorCode"), result.errorCode}})
                             .toJson(QJsonDocument::Compact);
}

void DesktopWindowHost::keepReachableAfterLaunch() {
    // QML hides immediately after success. If tray registration failed, keep
    // a taskbar entry without stealing focus from the launched application.
    QTimer::singleShot(0, this, [this] {
        if (!stopping_ && !recoveryAvailable_ && !pendingReveal_ && window_ && !window_->isVisible())
            window_->showMinimized();
    });
}

void DesktopWindowHost::shutdown() {
    if (stopping_)
        return;
    stopping_ = true;
    focusTimer_.stop();
    focusArmed_ = false;
    pendingReveal_ = false;
    placementRequest_ = 0;
    shell_.invalidateStartButtonAnchor();
    tray_.stop();
}

void DesktopWindowHost::requestPlacement() {
    placementRequest_ = shell_.requestStartButtonAnchor();
}

void DesktopWindowHost::updateRecovery(bool available, const QString &message) {
    if (stopping_)
        return;
    recoveryAvailable_ = available;
    QGuiApplication::setQuitOnLastWindowClosed(!available);
    trayStatus_ = available ? QStringLiteral("托盘可显示、收起和退出 XDStarter。")
                            : QStringLiteral("托盘不可用，保留普通窗口及任务栏恢复入口。");
    if (!message.isEmpty())
        trayStatus_ += QLatin1Char(' ') + message;
    publishStatus();
    if (started_ && !available && window_ && !window_->isVisible() && !pendingReveal_)
        window_->showMinimized();
}

void DesktopWindowHost::publishStatus() {
    if (window_)
        window_->setProperty("integrationStatus", trayStatus_ + QLatin1Char('\n') + anchorStatus_ +
                                                       QLatin1Char('\n') + appearanceStatus_);
}
} // namespace xdstarter
