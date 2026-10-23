#include "desktop_window_host.h"

#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/start_button_capture.h"
#include "platform/win_key_capture.h"
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
#include <limits>

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

bool HiddenWindowReadiness::observe(bool onTargetScreen, qreal targetDpr, qreal renderDpr,
                                    QSize logicalSize, const DesktopWindowMetricsResult &native) {
    const bool validDpr = std::isfinite(targetDpr) && std::isfinite(renderDpr) &&
                          targetDpr >= 1 && targetDpr <= 8 &&
                          qFuzzyCompare(targetDpr, renderDpr);
    if (!onTargetScreen || !validDpr || logicalSize.isEmpty() ||
        logicalSize.width() * renderDpr > std::numeric_limits<int>::max() ||
        logicalSize.height() * renderDpr > std::numeric_limits<int>::max()) {
        reset();
        return false;
    }
    const QSize expected(qRound(logicalSize.width() * renderDpr),
                         qRound(logicalSize.height() * renderDpr));
    const bool nativeMatches = !native.supported ||
                               (native.success && !native.physicalSize.isEmpty() &&
                                std::abs(native.physicalSize.width() - expected.width()) <= 1 &&
                                std::abs(native.physicalSize.height() - expected.height()) <= 1);
    if (!nativeMatches) {
        reset();
        return false;
    }
    const bool ready = sampled_ && qFuzzyCompare(dpr_, renderDpr) &&
                       logicalSize_ == logicalSize && physicalSize_ == native.physicalSize;
    sampled_ = true;
    dpr_ = renderDpr;
    logicalSize_ = logicalSize;
    physicalSize_ = native.physicalSize;
    return ready;
}

void HiddenWindowReadiness::reset() {
    sampled_ = false;
    dpr_ = 0;
    logicalSize_ = {};
    physicalSize_ = {};
}

DesktopWindowHost::DesktopWindowHost(QWindow &window, DesktopShellService &shell,
                                     DesktopPlatform &placement, DesktopTray &tray, QObject *parent)
    : QObject(parent), window_(&window), shell_(shell), placement_(placement), tray_(tray),
      previousQuitOnClose_(QGuiApplication::quitOnLastWindowClosed()) {
    preferredMinimumSize_ = window.minimumSize();
    window.installEventFilter(this);
    focusTimer_.setSingleShot(true);
    focusTimer_.setInterval(100);
    connect(&focusTimer_, &QTimer::timeout, this, &DesktopWindowHost::checkFocus);
    revealTimer_.setSingleShot(true);
    revealTimer_.setInterval(16);
    connect(&revealTimer_, &QTimer::timeout, this, &DesktopWindowHost::checkRevealPreparation);
    dismissTimer_.setSingleShot(true);
    connect(&dismissTimer_, &QTimer::timeout, this, [this] {
        if (!dismissPending_ || stopping_ || !window_)
            return;
        dismissPending_ = false;
        if (recoveryAvailable_)
            window_->hide();
        else
            showRecoveryWindow();
    });
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this,
            &DesktopWindowHost::scheduleFocusCheck);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            &DesktopWindowHost::scheduleFocusCheck);
    connect(&window, &QWindow::activeChanged, this, &DesktopWindowHost::scheduleFocusCheck);
    connect(&window, &QWindow::visibilityChanged, this, &DesktopWindowHost::scheduleFocusCheck);
    connect(&window, &QWindow::widthChanged, this,
            &DesktopWindowHost::rememberVisibleWindowSize);
    connect(&window, &QWindow::heightChanged, this,
            &DesktopWindowHost::rememberVisibleWindowSize);
    // Plain QWindow fixtures have no QML contract. Check before connecting so
    // headless tests and unsupported hosts produce no spurious Qt warnings.
    const auto *windowMeta = window.metaObject();
    if (windowMeta->indexOfSignal("dismissRequested()") >= 0) {
        window.setProperty("dismissalManaged", true);
        connect(&window, SIGNAL(dismissRequested()), this, SLOT(dismissWindow()));
    }
    if (windowMeta->indexOfSignal("winKeyCaptureToggleRequested()") >= 0)
        connect(&window, SIGNAL(winKeyCaptureToggleRequested()), this,
                SLOT(toggleWinKeyCapture()));
    if (windowMeta->indexOfSignal("startButtonCaptureToggleRequested()") >= 0)
        connect(&window, SIGNAL(startButtonCaptureToggleRequested()), this,
                SLOT(toggleStartButtonCapture()));
    if (windowMeta->indexOfSignal("trayRecoveryRetryRequested()") >= 0)
        connect(&window, SIGNAL(trayRecoveryRetryRequested()), this, SLOT(retryTrayRegistration()));
    const int blockedProperty = windowMeta->indexOfProperty("dismissalBlocked");
    if (blockedProperty >= 0) {
        const auto property = windowMeta->property(blockedProperty);
        if (property.hasNotifySignal()) {
            connect(&window, property.notifySignal(), this,
                    metaObject()->method(metaObject()->indexOfSlot("scheduleFocusCheck()")));
        }
    }
    connect(&tray_, &DesktopTray::toggleRequested, this, &DesktopWindowHost::toggleWindow);
    connect(&tray_, &DesktopTray::winKeyCaptureToggleRequested, this,
            &DesktopWindowHost::toggleWinKeyCapture);
    connect(&tray_, &DesktopTray::startButtonCaptureToggleRequested, this,
            &DesktopWindowHost::toggleStartButtonCapture);
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
            recordCaptureHostObservation(QStringLiteral("hidden"));
            startButtonTogglePending_ = false;
            cancelRevealPreparation();
            pendingReveal_ = false;
            focusTimer_.stop();
            focusArmed_ = false;
            prepareWindow("prepareForDismissal");
            placementRequest_ = 0;
            shell_.invalidateStartButtonAnchor();
            if (started_ && !stopping_ && !recoveryAvailable_)
                QTimer::singleShot(0, this, &DesktopWindowHost::showRecoveryWindow);
        }
    });
    connect(&shell_, &DesktopShellService::startButtonAnchorReady, this,
            [this](quint64 request, const DesktopAnchorResult &anchor) {
                if (stopping_ || request != placementRequest_ || !window_ ||
                    !pendingReveal_ || window_->isVisible())
                    return;
                beginRevealPreparation(anchor);
            });
}

void DesktopWindowHost::beginRevealPreparation(const DesktopAnchorResult &anchor) {
    const quint64 request = placementRequest_;
    cancelRevealPreparation();
    preparationRequest_ = request;
    revealAnchor_ = anchor;
    window_->setProperty("menuConservativeWorkAreaFallback", false);
    window_->setProperty("allowedResizeEdges", static_cast<int>(desktopResizeEdges(anchor)));
    targetScreen_ = anchor.monitorName.isEmpty() ? window_->screen()
                                               : placement_.screenForAnchor(anchor);
    if (targetScreen_)
        window_->setScreen(targetScreen_);
    if (!isCurrentReveal(request))
        return;
    configureAppearance(); // Creates the native surface, still hidden.
    if (!isCurrentReveal(request))
        return;
    if (intendedWindowSize_.isEmpty())
        intendedWindowSize_ = window_->size();
    if (targetScreen_) {
        // setScreen() does not move an existing HWND on a virtual desktop.
        // Moving the hidden surface to the target monitor delivers its DPI
        // transition before final panel placement, without a visible jump.
        const QRect area = targetScreen_->availableGeometry();
        window_->setPosition(area.center() - QPoint(window_->width() / 2,
                                                    window_->height() / 2));
    }
    if (!isCurrentReveal(request))
        return;
    revealDeadline_.start();
    revealTimer_.start();
}

bool DesktopWindowHost::isCurrentReveal(quint64 request) const {
    return !stopping_ && request && placementRequest_ == request && pendingReveal_ &&
           window_ && !window_->isVisible();
}

void DesktopWindowHost::cancelRevealPreparation() {
    revealTimer_.stop();
    revealDeadline_.invalidate();
    preparationRequest_ = 0;
    readiness_.reset();
    targetScreen_.clear();
}

void DesktopWindowHost::checkRevealPreparation() {
    if (stopping_ || !pendingReveal_ || !window_ || window_->isVisible() ||
        !placementRequest_ || preparationRequest_ != placementRequest_ ||
        !revealDeadline_.isValid())
        return;
    const quint64 request = placementRequest_;
    if (revealDeadline_.elapsed() >= 1500) {
        cancelRevealPreparation();
        pendingReveal_ = false;
        placementRequest_ = 0;
        shell_.invalidateStartButtonAnchor();
        anchorStatus_ = QStringLiteral("屏幕缩放尚未稳定，本次显示已取消；请从恢复入口重试。");
        recordCaptureHostObservation(QStringLiteral("presentation-canceled"), nullptr,
                                     QStringLiteral("placement-preparation-timeout"));
        startButtonTogglePending_ = false;
        publishStatus();
        qWarning().noquote() << "Desktop placement preparation timed out; window remains hidden.";
        if (!stopping_ && !pendingReveal_ && !placementRequest_ && !recoveryAvailable_ && window_)
            showRecoveryWindow();
        return;
    }
    const qreal renderDpr = window_->devicePixelRatio();
    const qreal screenDpr = targetScreen_ ? targetScreen_->devicePixelRatio() : renderDpr;
    if (targetScreen_ && window_->screen() == targetScreen_ && qFuzzyCompare(renderDpr, screenDpr))
        updateWindowConstraints();
    if (targetScreen_ && window_->screen() == targetScreen_ &&
        qFuzzyCompare(renderDpr, screenDpr) && !intendedWindowSize_.isEmpty() &&
        window_->size() != intendedWindowSize_) {
        // Windows can preserve the physical HWND size during a hidden monitor
        // move, changing Qt's logical size. Restore the user's DIP size only
        // after Qt has adopted the target render scale, then settle again.
        readiness_.reset();
        window_->resize(intendedWindowSize_);
        if (isCurrentReveal(request))
            revealTimer_.start();
        return;
    }
    const auto native = placement_.queryWindowMetrics(window_->winId());
    if (!isCurrentReveal(request))
        return;
    if (readiness_.observe(targetScreen_ && window_->screen() == targetScreen_,
                           screenDpr, renderDpr, window_->size(), native)) {
        finishRevealPreparation();
        return;
    }
    revealTimer_.start();
}

void DesktopWindowHost::finishRevealPreparation() {
    const quint64 request = placementRequest_;
    const DesktopAnchorResult anchor = revealAnchor_;
    // Only the settled Qt render DPR converts the QML panel, shadow and gap.
    // The queried taskbar DPI is diagnostic and never substitutes for it.
    const qreal scale = window_->devicePixelRatio();
    const int gap = qRound(windowMetric(window_, "screenGapDip", 8) * scale);
    const int inset = qRound(windowMetric(window_, "shadowInset", 0) * scale);
    const auto result = placement_.placeWindow(window_->winId(), anchor, gap, false, inset);
    if (!isCurrentReveal(request))
        return;
    if (result.errorCode == QStringLiteral("menu-input-region-failed")) {
        recordCaptureHostObservation(QStringLiteral("presentation-canceled"), nullptr,
                                     result.errorCode);
        startButtonTogglePending_ = false;
        cancelRevealPreparation();
        pendingReveal_ = false;
        placementRequest_ = 0;
        anchorStatus_ = result.message;
        publishStatus();
        if (!recoveryAvailable_)
            emit exitRequested();
        return;
    }
    anchorStatus_ = result.success ? QStringLiteral("面板按任务栏方向停靠，边距为 %1 DIP。")
                                         .arg(windowMetric(window_, "screenGapDip", 8))
                                   : QStringLiteral("原生定位不可用，使用 Qt 工作区边距回退。");
    if (!result.success && window_->screen()) {
        DesktopAnchorResult logical = anchor;
        const auto monitor = window_->screen()->geometry();
        logical.monitorPhysicalRect = monitor;
        logical.workAreaPhysicalRect = window_->screen()->availableGeometry();
        switch (desktopTaskbarEdge(anchor)) {
        case TaskbarEdge::Top:
            logical.taskbarPhysicalRect = QRect(monitor.topLeft(), QSize(monitor.width(), 1));
            break;
        case TaskbarEdge::Left:
            logical.taskbarPhysicalRect = QRect(monitor.topLeft(), QSize(1, monitor.height()));
            break;
        case TaskbarEdge::Right:
            logical.taskbarPhysicalRect =
                QRect(monitor.right(), monitor.top(), 1, monitor.height());
            break;
        default:
            logical.taskbarPhysicalRect =
                QRect(monitor.left(), monitor.bottom(), monitor.width(), 1);
            break;
        }
        const int logicalGap = qRound(windowMetric(window_, "screenGapDip", 8));
        const auto bounds = desktopMenuPanelBounds(logical, logicalGap);
        if (!bounds.isValid()) {
            cancelRevealPreparation();
            pendingReveal_ = false;
            placementRequest_ = 0;
            anchorStatus_ = QStringLiteral("无法取得安全工作区，本次显示已取消。");
            recordCaptureHostObservation(QStringLiteral("presentation-canceled"), nullptr,
                                         QStringLiteral("safe-work-area-unavailable"));
            startButtonTogglePending_ = false;
            publishStatus();
            if (!recoveryAvailable_)
                emit exitRequested();
            return;
        }
        // A placement error may precede region initialization. Keep the whole
        // outer HWND off the taskbar for this one fallback presentation.
        window_->setProperty("menuConservativeWorkAreaFallback", true);
        window_->setProperty("allowedResizeEdges", 0);
        window_->setMinimumSize(preferredMinimumSize_.boundedTo(bounds.size()));
        window_->setMaximumSize(bounds.size());
        window_->resize(window_->size().boundedTo(bounds.size()));
        const auto outer = desktopPopupPhysicalRect(window_->size(), logical, logicalGap);
        window_->setPosition(outer.topLeft());
    }
    if (!isCurrentReveal(request))
        return;
    const QString detail = result.message.isEmpty() ? anchor.message : result.message;
    if (!detail.isEmpty())
        anchorStatus_ += QLatin1Char(' ') + detail;
    publishStatus();
    if (!isCurrentReveal(request))
        return;
    qInfo().noquote()
        << "Desktop placement:"
        << QJsonDocument(
               QJsonObject{
                   {QStringLiteral("trustedAnchor"), anchor.trusted},
                   {QStringLiteral("startButton"), rectangleJson(anchor.startButtonPhysicalRect)},
                   {QStringLiteral("taskbar"), rectangleJson(anchor.taskbarPhysicalRect)},
                   {QStringLiteral("taskbarEdge"), static_cast<int>(desktopTaskbarEdge(anchor))},
                   {QStringLiteral("workArea"), rectangleJson(anchor.workAreaPhysicalRect)},
                   {QStringLiteral("placedWindow"), rectangleJson(result.physicalRect)},
                   {QStringLiteral("dpi"), static_cast<int>(anchor.dpi)},
                   {QStringLiteral("renderDpr"), scale},
                   {QStringLiteral("gapPhysical"), gap},
                   {QStringLiteral("shadowInsetPhysical"), inset},
                   {QStringLiteral("placedBeforeReveal"), true},
                   {QStringLiteral("preparationMs"), revealDeadline_.elapsed()},
                   {QStringLiteral("success"), result.success},
                   {QStringLiteral("message"), anchorStatus_}})
               .toJson(QJsonDocument::Compact);
    cancelRevealPreparation();
    pendingReveal_ = false;
    prepareWindow("stagePresentation");
    window_->showNormal();
    if (stopping_ || !window_ || !window_->isVisible() || placementRequest_ != request)
        return;
    window_->raise();
    window_->requestActivate();
    activatePresentedWindow();
    prepareWindow("prepareForPresentation");
    scheduleFocusCheck();
}

DesktopWindowHost::~DesktopWindowHost() {
    shutdown();
    QGuiApplication::setQuitOnLastWindowClosed(previousQuitOnClose_);
}

void DesktopWindowHost::start(const QImage& icon, bool showInitially) {
    if (stopping_ || started_)
        return;
    trayIcon_ = icon;
    const auto result = tray_.start(icon);
    updateRecovery(result.success && tray_.isAvailable(), result.message);
    started_ = true;
    if (recoveryAvailable_ && startButtonCapture_ && startButtonCapture_->isSupported())
        toggleStartButtonCapture();
    if (showInitially || !recoveryAvailable_)
        showWindow();
}

void DesktopWindowHost::attachStartButtonCapture(WinKeyCapture& capture) {
    if (started_ || stopping_ || startButtonCapture_)
        return;
    startButtonCapture_ = &capture;
    connect(&capture, &WinKeyCapture::toggleRequested, this, [this] {
        if (!stopping_ && startButtonRequested_ && startButtonActive_ && startButtonCapture_ &&
            startButtonCapture_->isActive()) {
            startButtonTogglePending_ = true;
            recordCaptureHostObservation(QStringLiteral("host-toggle-received"));
            toggleWindowFrom(QStringLiteral("start-button"));
        }
    });
    connect(&capture, &WinKeyCapture::statusChanged, this,
            [this](bool active, const QString& message, const QString& errorCode) {
                if (stopping_)
                    return;
                if (active && (!startButtonRequested_ || !recoveryAvailable_)) {
                    startButtonCapture_->setEnabled(false);
                    return;
                }
                startButtonActive_ = active;
                if (!active)
                    startButtonRequested_ = false;
                startButtonStatus_ = message;
                publishStatus();
                qInfo().noquote() << "Start button capture:"
                                  << QJsonDocument(
                                         QJsonObject{{QStringLiteral("active"), active},
                                                     {QStringLiteral("errorCode"), errorCode}})
                                         .toJson(QJsonDocument::Compact);
            });
    publishStatus();
}

void DesktopWindowHost::toggleStartButtonCapture() {
    if (stopping_ || !started_ || !startButtonCapture_ || !startButtonCapture_->isSupported())
        return;
    if (startButtonRequested_ || startButtonActive_) {
        startButtonRequested_ = false;
        startButtonActive_ = false;
        startButtonCapture_->setEnabled(false);
        startButtonStatus_ = QStringLiteral("开始按钮接管已暂停。");
    } else if (recoveryAvailable_) {
        startButtonRequested_ = true;
        startButtonStatus_ = QStringLiteral("正在启用开始按钮接管…");
        startButtonCapture_->setEnabled(true);
    }
    publishStatus();
}

void DesktopWindowHost::attachWinKeyCapture(WinKeyCapture &capture) {
    if (started_ || stopping_ || winKeyCapture_)
        return;
    winKeyCapture_ = &capture;
    winKeyStatus_ = capture.isSupported() ? QStringLiteral("Win 键唤起未启用。")
                                          : QStringLiteral("当前平台不支持 Win 键唤起。");
    connect(&capture, &WinKeyCapture::toggleRequested, this, [this] {
        if (!stopping_ && winKeyRequested_ && winKeyActive_ && winKeyCapture_ &&
            winKeyCapture_->isActive())
            toggleWindowFrom(QStringLiteral("win-key"));
    });
    connect(&capture, &WinKeyCapture::statusChanged, this,
            [this](bool active, const QString &message, const QString &errorCode) {
                if (stopping_)
                    return;
                if (active && (!winKeyRequested_ || !recoveryAvailable_)) {
                    winKeyCapture_->stop();
                    return;
                }
                winKeyActive_ = active;
                winKeyPending_ = false;
                if (!active)
                    winKeyRequested_ = false;
                winKeyStatus_ = message;
                publishStatus();
                qInfo().noquote() << "Win key capture:"
                                  << QJsonDocument(QJsonObject{
                                         {QStringLiteral("active"), active},
                                         {QStringLiteral("errorCode"), errorCode}})
                                         .toJson(QJsonDocument::Compact);
            });
    publishStatus();
}

void DesktopWindowHost::toggleWinKeyCapture() {
    if (stopping_ || !started_ || !winKeyCapture_ || !winKeyCapture_->isSupported())
        return;
    if (winKeyRequested_ || winKeyActive_ || winKeyPending_) {
        winKeyRequested_ = false;
        winKeyPending_ = false;
        winKeyCapture_->stop();
        winKeyActive_ = false;
        winKeyStatus_ = QStringLiteral("Win 键唤起已暂停；系统开始菜单保持原生行为。");
        publishStatus();
    } else if (recoveryAvailable_) {
        winKeyRequested_ = true;
        winKeyPending_ = true;
        winKeyStatus_ = QStringLiteral("正在启用 Win 键唤起…开始按钮保持原生行为。");
        publishStatus();
        // Publishing tray state can synchronously report lost recovery. Recheck
        // the request after that notification before installing global input.
        if (!stopping_ && recoveryAvailable_ && winKeyRequested_ && winKeyPending_ && winKeyCapture_)
            winKeyCapture_->setEnabled(true);
    }
}

void DesktopWindowHost::showWindow() {
    if (stopping_ || !window_)
        return;
    dismissPending_ = false;
    dismissTimer_.stop();
    rememberVisibleWindowSize();
    if (intendedWindowSize_.isEmpty() && !window_->size().isEmpty())
        intendedWindowSize_ = window_->size();
    focusTimer_.stop();
    focusArmed_ = false;
    if (window_->visibility() == QWindow::Minimized)
        window_->hide();
    pendingReveal_ = !window_->isVisible();
    if (!pendingReveal_) {
        if (placement_.hasOwnedForegroundDialog(window_->winId())) {
            recordCaptureHostObservation(QStringLiteral("owned-dialog-protected"));
            startButtonTogglePending_ = false;
            scheduleFocusCheck();
            return;
        }
        prepareWindow("restorePresentationFocus");
        window_->raise();
        window_->requestActivate();
        activatePresentedWindow();
        scheduleFocusCheck();
        return;
    }
    requestPlacement();
}

void DesktopWindowHost::rememberVisibleWindowSize() {
    // A hidden or minimized native DPI transition is not a user resize. In
    // particular, cancellation must retain the size from before that move.
    if (!stopping_ && !pendingReveal_ && window_ && window_->isVisible() &&
        window_->visibility() != QWindow::Minimized && !window_->size().isEmpty())
        intendedWindowSize_ = window_->size();
}

void DesktopWindowHost::activatePresentedWindow() {
    if (stopping_ || !window_ || !window_->isVisible())
        return;
    const auto result = placement_.presentMenuWindow(window_->winId());
    recordCaptureHostObservation(QStringLiteral("presentation-result"), &result);
    startButtonTogglePending_ = false;
    if (result.supported)
        qInfo().noquote() << "Menu presentation:"
                          << QJsonDocument(
                                 QJsonObject{{QStringLiteral("nativeVisible"), result.visible},
                                             {QStringLiteral("nativeMinimized"), result.minimized},
                                             {QStringLiteral("foreground"), result.foreground},
                                             {QStringLiteral("errorCode"), result.errorCode}})
                                 .toJson(QJsonDocument::Compact);
}

void DesktopWindowHost::recordCaptureHostObservation(const QString& event,
                                                     const DesktopPresentationResult* result,
                                                     const QString& errorCode) {
    if (!startButtonTogglePending_ || !startButtonCapture_ || !window_)
        return;
    StartButtonHostObservation observation;
    observation.event = event;
    observation.visible = window_->isVisible();
    observation.minimized = window_->visibility() == QWindow::Minimized;
    observation.errorCode = errorCode;
    if (result && result->supported) {
        observation.nativeMeasured = true;
        observation.visible = result->visible;
        observation.minimized = result->minimized;
        observation.foreground = result->foreground;
        observation.errorCode = result->errorCode;
    }
    recordStartButtonHostObservation(*startButtonCapture_, observation);
}

void DesktopWindowHost::toggleWindow() {
    toggleWindowFrom(QStringLiteral("tray-toggle"));
}

void DesktopWindowHost::toggleWindowFrom(const QString &source) {
    if (stopping_ || !window_)
        return;
    qInfo().noquote() << "Menu toggle:"
                      << QJsonDocument(QJsonObject{
                             {QStringLiteral("source"), source},
                             {QStringLiteral("visible"), window_->isVisible()},
                             {QStringLiteral("pendingReveal"), pendingReveal_}})
                             .toJson(QJsonDocument::Compact);
    if (dismissPending_)
        showWindow();
    else if (pendingReveal_ ||
             (window_->isVisible() && window_->visibility() != QWindow::Minimized))
        dismiss(source);
    else
        showWindow();
}

bool DesktopWindowHost::eventFilter(QObject *object, QEvent *event) {
    if (object == window_ && event->type() == QEvent::Close) {
        if (started_ && !stopping_ && !recoveryAvailable_) {
            event->ignore();
            emit exitRequested();
            return true;
        }
        rememberVisibleWindowSize();
        if (pendingReveal_) {
            recordCaptureHostObservation(QStringLiteral("presentation-canceled"), nullptr,
                                         QStringLiteral("window-closed-before-presentation"));
            startButtonTogglePending_ = false;
            cancelRevealPreparation();
            pendingReveal_ = false;
            placementRequest_ = 0;
            shell_.invalidateStartButtonAnchor();
        }
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
    } else if (placement_.hasOwnedForegroundWindow(window_->winId())) {
        // An OS dialog may have no Qt focus window. Only while its actual
        // foreground owner chain matches do we poll: a later switch to another
        // app may emit no further Qt focus change for this native dialog.
        focusArmed_ = true;
        if (!focusTimer_.isActive())
            focusTimer_.start(100);
    } else if (focusArmed_) {
        // Coalesce popup/activation handoffs; never hide a window whose initial
        // activation was denied by Windows, or resurrect a previous dismissal.
        if (!focusTimer_.isActive())
            focusTimer_.start(100);
    }
}

void DesktopWindowHost::checkFocus() {
    if (stopping_ || !window_ || !window_->isVisible() ||
        window_->visibility() == QWindow::Minimized || trayMenuActive_ || !focusArmed_ ||
        !recoveryAvailable_)
        return;
    if (ownsFocus())
        return;
    if (placement_.hasOwnedForegroundWindow(window_->winId())) {
        focusTimer_.start(100);
        return;
    }
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
    if (!recoveryAvailable_) {
        if (reason != QStringLiteral("external-focus"))
            emit exitRequested();
        return;
    }
    if (dismissPending_)
        return;
    if (pendingReveal_) {
        recordCaptureHostObservation(QStringLiteral("presentation-canceled"), nullptr,
                                     QStringLiteral("reveal-canceled-before-show"));
        startButtonTogglePending_ = false;
    }
    // Resize notifications can still be queued when the user immediately
    // dismisses the menu. Capture the current visible DIP size before hiding.
    rememberVisibleWindowSize();
    focusTimer_.stop();
    cancelRevealPreparation();
    focusArmed_ = false;
    pendingReveal_ = false;
    placementRequest_ = 0;
    shell_.invalidateStartButtonAnchor();
    prepareWindow("prepareForDismissal");
    const int duration = window_->property("dismissAnimationDuration").toInt();
    if (window_->isVisible() && duration > 0 && duration <= 300 &&
        window_->metaObject()->indexOfMethod("animateDismissal()") >= 0) {
        dismissPending_ = true;
        prepareWindow("animateDismissal");
        dismissTimer_.start(duration);
    } else {
        window_->hide();
    }
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
    // A tool window has no taskbar fallback. Keep a visible retry/exit surface
    // when the notification area cannot provide a recovery entry.
    QTimer::singleShot(0, this, [this] {
        if (!stopping_ && !recoveryAvailable_ && !pendingReveal_ && window_ && !window_->isVisible())
            showRecoveryWindow();
    });
}

void DesktopWindowHost::shutdown() {
    if (stopping_)
        return;
    recordCaptureHostObservation(QStringLiteral("presentation-canceled"), nullptr,
                                 QStringLiteral("host-stopping"));
    startButtonTogglePending_ = false;
    stopping_ = true;
    dismissTimer_.stop();
    dismissPending_ = false;
    focusTimer_.stop();
    cancelRevealPreparation();
    focusArmed_ = false;
    pendingReveal_ = false;
    placementRequest_ = 0;
    shell_.invalidateStartButtonAnchor();
    winKeyRequested_ = false;
    winKeyPending_ = false;
    if (winKeyCapture_)
        winKeyCapture_->stop();
    winKeyActive_ = false;
    startButtonRequested_ = false;
    startButtonActive_ = false;
    if (startButtonCapture_)
        startButtonCapture_->stop();
    publishStartButtonCapture();
    publishWinKeyCapture();
    tray_.stop();
}

void DesktopWindowHost::requestPlacement() {
    cancelRevealPreparation();
    placementRequest_ = shell_.requestStartButtonAnchor();
}

void DesktopWindowHost::updateRecovery(bool available, const QString &message) {
    if (stopping_)
        return;
    recoveryAvailable_ = available;
    if (!available && startButtonCapture_ && (startButtonRequested_ || startButtonActive_)) {
        startButtonRequested_ = false;
        startButtonActive_ = false;
        startButtonCapture_->setEnabled(false);
        startButtonStatus_ = QStringLiteral("托盘入口不可用，开始按钮接管已暂停。");
    }
    if (!available && winKeyCapture_ && (winKeyRequested_ || winKeyActive_ || winKeyPending_)) {
        winKeyRequested_ = false;
        winKeyPending_ = false;
        winKeyCapture_->stop();
        winKeyActive_ = false;
        winKeyStatus_ = QStringLiteral("托盘恢复入口不可用，Win 键唤起已暂停；系统开始菜单保持原生行为。");
    }
    QGuiApplication::setQuitOnLastWindowClosed(!available);
    trayStatus_ =
        available
            ? QStringLiteral("托盘可显示、收起和退出 XDStarter。")
            : QStringLiteral("托盘不可用，请重试注册或退出 XDStarter。关闭此恢复窗口会退出程序。");
    if (!message.isEmpty())
        trayStatus_ += QLatin1Char(' ') + message;
    publishStatus();
    if (started_ && available && recoveryWindowLayout_ && window_) {
        // The emergency surface keeps its whole transparent HWND inside the
        // work area. Re-enter the normal hidden placement/region lifecycle so
        // retry restores both panel margins and direction-aware resize edges.
        recoveryWindowLayout_ = false;
        window_->hide();
        showWindow();
        return;
    }
    if (started_ && !available) {
        dismissTimer_.stop();
        dismissPending_ = false;
        if (window_ && (!window_->isVisible() || window_->visibility() == QWindow::Minimized) &&
            !pendingReveal_)
            showRecoveryWindow();
        else if (window_ && window_->isVisible())
            prepareWindow("restorePresentationFocus");
    }
}

void DesktopWindowHost::publishStatus() {
    publishWinKeyCapture();
    publishStartButtonCapture();
    if (window_)
        window_->setProperty("integrationStatus", trayStatus_ + QLatin1Char('\n') + anchorStatus_ +
                                                      QLatin1Char('\n') + appearanceStatus_ +
                                                      QLatin1Char('\n') + winKeyStatus_ +
                                                      QLatin1Char('\n') + startButtonStatus_);
    if (window_) {
        window_->setProperty("trayRecoveryUnavailable", !recoveryAvailable_);
        window_->setProperty("trayRecoveryMessage", trayStatus_);
    }
}

void DesktopWindowHost::retryTrayRegistration() {
    if (stopping_ || !started_ || recoveryAvailable_)
        return;
    const auto result = tray_.start(trayIcon_);
    updateRecovery(result.success && tray_.isAvailable(), result.message);
}

void DesktopWindowHost::showRecoveryWindow() {
    if (stopping_ || !started_ || recoveryAvailable_ || !window_ || pendingReveal_)
        return;
    dismissTimer_.stop();
    dismissPending_ = false;
    if (window_->isVisible() && window_->visibility() != QWindow::Minimized)
        return;
    cancelRevealPreparation();
    if (window_->isVisible())
        window_->hide();
    configureAppearance();
    recoveryWindowLayout_ = true;
    window_->setProperty("menuConservativeWorkAreaFallback", true);
    window_->setProperty("allowedResizeEdges", 0);
    const auto* screen = window_->screen();
    if (!screen) {
        emit exitRequested();
        return;
    }
    if (screen) {
        DesktopAnchorResult logical;
        logical.monitorPhysicalRect = screen->geometry();
        logical.workAreaPhysicalRect = screen->availableGeometry();
        const int gap = qRound(windowMetric(window_, "screenGapDip", 8));
        const QRect bounds = desktopMenuPanelBounds(logical, gap);
        if (!bounds.isValid()) {
            emit exitRequested();
            return;
        }
        if (bounds.isValid()) {
            // There may not yet be a trustworthy native placement/region.
            // Keep even the shadow HWND off the taskbar for this recovery
            // surface, and disable edge dragging until registration succeeds.
            const QSize maximum = bounds.size();
            window_->setMinimumSize(preferredMinimumSize_.boundedTo(maximum));
            window_->setMaximumSize(maximum);
            window_->resize(window_->size().boundedTo(maximum));
            const QRect outer = desktopPopupPhysicalRect(window_->size(), logical, gap);
            window_->setPosition(outer.topLeft());
        }
    }
    prepareWindow("stagePresentation");
    window_->showNormal();
    activatePresentedWindow();
    prepareWindow("prepareForPresentation");
}

void DesktopWindowHost::updateWindowConstraints() {
    if (!window_)
        return;
    const qreal scale = window_->devicePixelRatio();
    const int gap = qRound(windowMetric(window_, "screenGapDip", 8) * scale);
    const int inset = qRound(windowMetric(window_, "shadowInset", 0) * scale);
    const QRect panel = desktopMenuPanelBounds(revealAnchor_, gap);
    if (!panel.isValid())
        return;
    if (qint64(panel.left()) - inset < std::numeric_limits<int>::min() ||
        qint64(panel.top()) - inset < std::numeric_limits<int>::min() ||
        qint64(panel.right()) + inset > std::numeric_limits<int>::max() ||
        qint64(panel.bottom()) + inset > std::numeric_limits<int>::max())
        return;
    const QRect outer = panel.adjusted(-inset, -inset, inset, inset);
    window_->setProperty("menuOuterBoundsPhysical", outer);
    const QSize maximum(qMax(1, static_cast<int>(std::floor(outer.width() / scale))),
                        qMax(1, static_cast<int>(std::floor(outer.height() / scale))));
    window_->setMinimumSize(preferredMinimumSize_.boundedTo(maximum));
    window_->setMaximumSize(maximum);
    intendedWindowSize_ = intendedWindowSize_.boundedTo(maximum);
}

void DesktopWindowHost::publishStartButtonCapture() {
    const bool available = !stopping_ && recoveryAvailable_ && startButtonCapture_ &&
                           startButtonCapture_->isSupported();
    if (window_) {
        window_->setProperty("startButtonCaptureAvailable", available);
        window_->setProperty("startButtonCaptureActive", startButtonActive_);
        window_->setProperty("startButtonCaptureStatus", startButtonStatus_);
    }
    tray_.setStartButtonCaptureState(startButtonActive_, available, startButtonStatus_);
}

void DesktopWindowHost::publishWinKeyCapture() {
    const bool canEnable = !stopping_ && !winKeyPending_ && recoveryAvailable_ &&
                           winKeyCapture_ && winKeyCapture_->isSupported();
    if (window_) {
        window_->setProperty("winKeyCaptureAvailable", canEnable);
        window_->setProperty("winKeyCaptureActive", winKeyActive_);
        window_->setProperty("winKeyCaptureBusy", winKeyPending_);
        window_->setProperty("winKeyCaptureStatus", winKeyStatus_);
    }
    // A tray update may reenter updateRecovery(); its newer unavailable state
    // must be the last state published to QML as well as the notification area.
    tray_.setWinKeyCaptureState(winKeyActive_, canEnable, winKeyStatus_);
}
} // namespace xdstarter
