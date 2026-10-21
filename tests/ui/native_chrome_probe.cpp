// tests/ui/native_chrome_probe.cpp

#include "native_chrome_probe.h"

#include "app/desktop_window_host.h"
#include "app/menu_controller.h"
#include "core/tile_grid.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/platform_integration.h"
#include "services/desktop_shell_service.h"
#include "services/prototype_service.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScreen>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

class MemoryTray final : public xdstarter::DesktopTray {
  public:
    xdstarter::DesktopActionResult start(const QImage&) override {
        available_ = true;
        beginEventSession();
        return {true, QStringLiteral("模拟托盘信号；未注册真实通知区图标。"), {}};
    }
    void stop() override {
        available_ = false;
        endEventSession();
    }
    bool isAvailable() const override { return available_; }
    void showContextMenu() override {}
    void toggle() { emit toggleRequested(); }

  private:
    bool available_ = false;
};

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 2500) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

void settle(int durationMs) {
    QElapsedTimer timer;
    timer.start();
    waitUntil([&timer, durationMs] { return timer.elapsed() >= durationMs; }, durationMs + 100);
}

QJsonObject rectangleJson(const QRectF& rect) {
    return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}};
}

QJsonArray colorJson(const QColor& color) {
    return {color.red(), color.green(), color.blue(), color.alpha()};
}

QString apiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    default: return QStringLiteral("Other (%1)").arg(static_cast<int>(api));
    }
}

} // namespace

int runNativeChromeProbe() {
    const auto outputDirectory = qEnvironmentVariable("XDSTARTER_CHROME_VALIDATION_DIR");
    if (outputDirectory.isEmpty() || !QDir::isAbsolutePath(outputDirectory) ||
        !QDir().mkpath(outputDirectory)) {
        qCritical() << "Set XDSTARTER_CHROME_VALIDATION_DIR to an absolute writable directory.";
        return 1;
    }
    QJsonObject evidence{
        {QStringLiteral("scope"), QStringLiteral("真实 Windows QPA/RHI 渲染与模拟托盘信号；未点击或注册真实系统托盘。")},
        {QStringLiteral("noUserLayoutIO"), true},
        {QStringLiteral("noApplicationCatalog"), true},
        {QStringLiteral("noGlobalInput"), true},
        {QStringLiteral("anchorQuery"), QStringLiteral("真实 Windows 工作区查询；不使用 UIA。")},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platformName"), QGuiApplication::platformName()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("buildAbi"), QSysInfo::buildAbi()}};
    QStringList errors;
    QStringList qmlWarnings;
    QJsonArray cycles;
    QJsonArray captures;
    int completedCycles = 0;
    const auto require = [&errors](bool condition, const QString& message) {
        if (!condition)
            errors.append(message);
        return condition;
    };
    const auto execute = [&]() -> bool {
        if (!require(QGuiApplication::platformName() == QStringLiteral("windows"),
                     QStringLiteral("The opt-in probe requires the real Windows QPA.")))
            return false;
        QQuickWindow::setDefaultAlphaBuffer(true);
        QFile tokensFile(QStringLiteral(":/xdstarter/tokens/visual.json"));
        if (!require(tokensFile.open(QIODevice::ReadOnly),
                     QStringLiteral("Cannot open bundled visual tokens.")))
            return false;
        const auto tokenDocument = QJsonDocument::fromJson(tokensFile.readAll());
        if (!require(tokenDocument.isObject(), QStringLiteral("Invalid bundled visual tokens.")))
            return false;
        const auto tokens = tokenDocument.object().toVariantMap();
        const xdstarter::TileGrid grid;
        xdstarter::PrototypeService service(grid, {}, true);
        const auto platform = xdstarter::createPlatformIntegration();
        xdstarter::DesktopShellService shell(xdstarter::createDesktopPlatform());
        xdstarter::MenuController controller(service, *platform, tokens, nullptr, nullptr, &shell);
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [&qmlWarnings](const QList<QQmlError>& warnings) {
                             for (const auto& warning : warnings)
                                 qmlWarnings.append(warning.toString());
                         });
        engine.setInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue(&controller)},
             {QStringLiteral("visible"), false}});
        engine.loadFromModule("XDStarter", "Main");
        if (!require(!engine.rootObjects().isEmpty(), QStringLiteral("Main QML did not load.")))
            return false;
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!require(window != nullptr, QStringLiteral("Main is not a QQuickWindow.")))
            return false;
        const auto placement = xdstarter::createDesktopPlatform();
        MemoryTray tray;
        xdstarter::DesktopWindowHost host(*window, shell, *placement, tray);
        int currentCycle = -1;
        QRect revealGeometry;
        QObject::connect(window, &QWindow::visibleChanged, window, [&](bool visible) {
            if (visible && currentCycle >= 0)
                revealGeometry = window->geometry();
        });
        host.start(QImage(QStringLiteral(":/xdstarter/app/xdstarter-app-icon-32.png")));
        if (!require(waitUntil([window] { return window->isVisible() && window->isExposed(); }),
                     QStringLiteral("Initial native reveal timed out.")))
            return false;
        const auto api = window->rendererInterface()->graphicsApi();
        evidence.insert(QStringLiteral("graphicsApi"), apiName(api));
        if (!require(api != QSGRendererInterface::Software && api != QSGRendererInterface::Null &&
                         api != QSGRendererInterface::Unknown,
                     QStringLiteral("The probe requires an RHI graphics backend.")))
            return false;
        tray.toggle();
        if (!require(waitUntil([window] { return !window->isVisible(); }),
                     QStringLiteral("Initial simulated tray hide timed out.")))
            return false;

        QString firstScreen;
        QRect firstGeometry;
        const qreal shadow = window->property("shadowInset").toReal();
        const qreal gap = window->property("screenGapDip").toReal();
        for (int cycle = 0; cycle < 12; ++cycle) {
            currentCycle = cycle;
            revealGeometry = {};
            controller.setDarkTheme(cycle < 6);
            tray.toggle();
            if (!require(waitUntil([window] { return window->isVisible() && window->isExposed(); }),
                         QStringLiteral("Cycle %1 reveal timed out.").arg(cycle)))
                return false;
            settle(120);
            if (!require(window->isVisible(),
                         QStringLiteral("Cycle %1 unexpectedly dismissed.").arg(cycle)))
                return false;
            auto* screen = window->screen();
            if (!require(screen != nullptr, QStringLiteral("Window has no screen.")))
                return false;
            const auto geometry = window->geometry();
            const auto available = screen->availableGeometry();
            const qreal dpr = window->devicePixelRatio();
            const QRectF panel(geometry.x() + shadow, geometry.y() + shadow,
                               geometry.width() - 2 * shadow, geometry.height() - 2 * shadow);
            const qreal leftGap = panel.left() - available.left();
            const qreal bottomGap = available.bottom() + 1 - (panel.y() + panel.height());
            const bool equalGaps = std::abs(leftGap - gap) * dpr <= 1.01 &&
                                   std::abs(bottomGap - gap) * dpr <= 1.01;
            if (cycle == 0) {
                firstScreen = screen->name();
                firstGeometry = geometry;
            }
            const bool stable = screen->name() == firstScreen && geometry == firstGeometry &&
                                revealGeometry == geometry;
            const bool flags = window->flags().testFlag(Qt::FramelessWindowHint) &&
                               window->flags().testFlag(Qt::NoDropShadowWindowHint) &&
                               window->color().alpha() == 0;
            cycles.append(QJsonObject{
                {QStringLiteral("cycle"), cycle + 1},
                {QStringLiteral("screenName"), screen->name()},
                {QStringLiteral("windowDevicePixelRatio"), dpr},
                {QStringLiteral("screenAvailableGeometryDip"), rectangleJson(available)},
                {QStringLiteral("firstVisibleGeometryDip"), rectangleJson(revealGeometry)},
                {QStringLiteral("settledWindowGeometryDip"), rectangleJson(geometry)},
                {QStringLiteral("panelGeometryDip"), rectangleJson(panel)},
                {QStringLiteral("leftGapDip"), leftGap},
                {QStringLiteral("bottomGapDip"), bottomGap},
                {QStringLiteral("symmetricGapPassed"), equalGaps},
                {QStringLiteral("sameMonitorPositionStable"), stable},
                {QStringLiteral("customChromeFlagsPassed"), flags}});
            require(equalGaps,
                    QStringLiteral("Cycle %1 left/bottom margins differ from 8 DIP.").arg(cycle));
            require(stable,
                    QStringLiteral("Cycle %1 geometry moved between reveals or after visibility.")
                        .arg(cycle));
            require(flags, QStringLiteral("Cycle %1 chrome flags/alpha are incorrect.").arg(cycle));

            if (cycle == 0 || cycle == 6) {
                const auto image = window->grabWindow();
                if (!require(!image.isNull(), QStringLiteral("Native frame capture failed.")))
                    return false;
                const auto pixel = [&](qreal x, qreal y) {
                    return image.pixelColor(
                        std::clamp(qFloor(x * image.width() / window->width()), 0, image.width() - 1),
                        std::clamp(qFloor(y * image.height() / window->height()), 0, image.height() - 1));
                };
                bool cornersTransparent = image.hasAlphaChannel();
                for (const auto& corner : {QPoint(0, 0), QPoint(image.width() - 1, 0),
                                           QPoint(0, image.height() - 1),
                                           QPoint(image.width() - 1, image.height() - 1)})
                    cornersTransparent &= image.pixelColor(corner).alpha() == 0;
                const auto nearAlpha = pixel(shadow - 2, window->height() / 2).alpha();
                const auto farAlpha = pixel(shadow - 16, window->height() / 2).alpha();
                const bool shadowPassed = nearAlpha > farAlpha && farAlpha > 0 && nearAlpha < 128;
                const bool panelOpaque = pixel(shadow + 100, shadow + 12).alpha() == 255;
                const auto theme = cycle == 0 ? QStringLiteral("dark") : QStringLiteral("light");
                const auto fileName = theme + QStringLiteral("-native-chrome.png");
                const bool saved = image.save(QDir(outputDirectory).filePath(fileName));
                captures.append(QJsonObject{
                    {QStringLiteral("theme"), theme}, {QStringLiteral("screenshot"), fileName},
                    {QStringLiteral("widthPx"), image.width()},
                    {QStringLiteral("heightPx"), image.height()},
                    {QStringLiteral("outerCornersAlphaZero"), cornersTransparent},
                    {QStringLiteral("nearShadowAlpha"), nearAlpha},
                    {QStringLiteral("farShadowAlpha"), farAlpha},
                    {QStringLiteral("shadowGradientPassed"), shadowPassed},
                    {QStringLiteral("panelOpaque"), panelOpaque},
                    {QStringLiteral("panelSampleRgba"),
                     colorJson(pixel(shadow + 100, shadow + 12))},
                    {QStringLiteral("saved"), saved}});
                require(cornersTransparent && shadowPassed && panelOpaque && saved,
                        QStringLiteral("%1 native pixel/capture validation failed.").arg(theme));
            }
            tray.toggle();
            if (!require(waitUntil([window] { return !window->isVisible(); }),
                         QStringLiteral("Cycle %1 simulated tray hide timed out.").arg(cycle)))
                return false;
            ++completedCycles;
        }
        host.shutdown();
        window->close();
        require(qmlWarnings.isEmpty(), QStringLiteral("QML emitted warnings."));
        return errors.isEmpty();
    };
    const bool passed = execute();
    evidence.insert(QStringLiteral("requestedRevealHideCycles"), 12);
    evidence.insert(QStringLiteral("completedCycles"), completedCycles);
    evidence.insert(QStringLiteral("cycles"), cycles);
    evidence.insert(QStringLiteral("captures"), captures);
    evidence.insert(QStringLiteral("qmlWarnings"), QJsonArray::fromStringList(qmlWarnings));
    evidence.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
    evidence.insert(QStringLiteral("passed"), passed);
    QFile output(QDir(outputDirectory).filePath(QStringLiteral("native-chrome-probe.json")));
    const auto serialized = QJsonDocument(evidence).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(serialized) != serialized.size()) {
        qCritical() << "Cannot write native chrome probe evidence:" << output.errorString();
        return 1;
    }
    qInfo().noquote() << "Native chrome probe:" << (passed ? "PASS" : "FAIL")
                      << "(real Windows rendering; simulated tray signals; no real tray clicks)";
    return passed ? 0 : 1;
}
