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
#include <QLibrary>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScreen>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <QtGui/qscreen_platform.h>
#include <qt_windows.h>
#endif

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
                               (window->flags() & Qt::WindowType_Mask) == Qt::Tool &&
                               window->flags().testFlag(Qt::WindowStaysOnTopHint) &&
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

namespace {

#ifdef Q_OS_WIN
struct ProbeMonitor {
    HMONITOR handle = nullptr;
    QString name;
    QRect physicalRect;
    QRect workAreaPhysicalRect;
    unsigned int dpi = 0;
    QPointer<QScreen> screen;
};

HMONITOR probeScreenMonitor(QScreen* screen) {
    if (screen) {
        const auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        if (native)
            return native->handle();
    }
    return nullptr;
}

BOOL CALLBACK collectProbeMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) noexcept {
    try {
        reinterpret_cast<std::vector<HMONITOR>*>(context)->push_back(monitor);
        return TRUE;
    } catch (...) {
        return FALSE;
    }
}

struct ProbeAnchorState {
    std::vector<xdstarter::DesktopAnchorResult> anchors;
    std::atomic<size_t> selected{0};
};

class ScreenProbePlatform final : public xdstarter::DesktopPlatform {
  public:
    explicit ScreenProbePlatform(std::shared_ptr<ProbeAnchorState> state)
        : state_(std::move(state)) {}

    xdstarter::DesktopActionResult openDesktopAction(const QString&) override {
        return {false, QStringLiteral("Desktop actions are disabled in this probe."),
                QStringLiteral("probe-action-disabled")};
    }
    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override { return queryMenuAnchor(); }
    xdstarter::DesktopAnchorResult queryMenuAnchor() override {
        return state_->anchors.at(state_->selected.load());
    }

  private:
    std::shared_ptr<ProbeAnchorState> state_;
};

struct ProbeWindowSample {
    QJsonObject json;
    QRect physicalRect;
    QSize logicalSize;
    qreal renderDpr = 0;
    unsigned int nativeDpi = 0;
    QString screenName;
    HMONITOR monitor = nullptr;
    HMONITOR qtScreenMonitor = nullptr;
    bool valid = false;
    bool nativeVisible = false;
    bool qtVisible = false;
    bool qtExposed = false;
};

ProbeWindowSample sampleProbeWindow(QQuickWindow& window) {
    ProbeWindowSample sample;
    const auto handle = reinterpret_cast<HWND>(window.winId());
    RECT rectangle{};
    sample.valid = GetWindowRect(handle, &rectangle) != FALSE;
    if (sample.valid)
        sample.physicalRect = QRect(rectangle.left, rectangle.top, rectangle.right - rectangle.left,
                                    rectangle.bottom - rectangle.top);
    sample.monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONULL);
    sample.logicalSize = window.size();
    sample.renderDpr = window.devicePixelRatio();
    sample.nativeDpi = GetDpiForWindow(handle);
    sample.screenName = window.screen() ? window.screen()->name() : QString();
    sample.qtScreenMonitor = probeScreenMonitor(window.screen());
    sample.nativeVisible = IsWindowVisible(handle) != FALSE;
    sample.qtVisible = window.isVisible();
    sample.qtExposed = window.isExposed();
    sample.json = {{QStringLiteral("nativeRectRead"), sample.valid},
                   {QStringLiteral("nativeWindowVisible"), sample.nativeVisible},
                   {QStringLiteral("qtWindowVisible"), sample.qtVisible},
                   {QStringLiteral("qtWindowExposed"), sample.qtExposed},
                   {QStringLiteral("outerPhysicalRect"), rectangleJson(sample.physicalRect)},
                   {QStringLiteral("qtGeometryDip"), rectangleJson(window.geometry())},
                   {QStringLiteral("qtRenderDpr"), sample.renderDpr},
                   {QStringLiteral("nativeWindowDpi"), static_cast<int>(sample.nativeDpi)},
                   {QStringLiteral("qtScreenName"), sample.screenName},
                   {QStringLiteral("qtScreenNativeMonitorMatchesWindow"),
                    sample.qtScreenMonitor == sample.monitor && sample.monitor != nullptr}};
    return sample;
}

bool validateProbeSample(ProbeWindowSample& sample, const ProbeMonitor& target, qreal shadow,
                         qreal gap, const QSize& expectedLogicalSize) {
    const qreal expectedDpr = target.screen ? target.screen->devicePixelRatio() : 0;
    const int insetPhysical = qRound(shadow * expectedDpr);
    const int gapPhysical = qRound(gap * expectedDpr);
    const QRect panel(sample.physicalRect.x() + insetPhysical,
                      sample.physicalRect.y() + insetPhysical,
                      sample.physicalRect.width() - 2 * insetPhysical,
                      sample.physicalRect.height() - 2 * insetPhysical);
    const int leftGap = panel.left() - target.workAreaPhysicalRect.left();
    const int bottomGap = target.workAreaPhysicalRect.bottom() - panel.bottom();
    const bool margins =
        std::abs(leftGap - gapPhysical) <= 1 && std::abs(bottomGap - gapPhysical) <= 1;
    const bool size = sample.logicalSize == expectedLogicalSize &&
                      std::abs(sample.physicalRect.width() -
                               qRound(expectedLogicalSize.width() * expectedDpr)) <= 1 &&
                      std::abs(sample.physicalRect.height() -
                               qRound(expectedLogicalSize.height() * expectedDpr)) <= 1;
    const bool targetScale = expectedDpr > 0 && std::abs(sample.renderDpr - expectedDpr) < 0.001 &&
                             sample.nativeDpi == target.dpi && target.screen &&
                             sample.screenName == target.screen->name() &&
                             sample.qtScreenMonitor == target.handle &&
                             sample.monitor == target.handle;
    const bool contained = target.workAreaPhysicalRect.contains(panel);
    sample.json.insert(QStringLiteral("panelPhysicalRect"), rectangleJson(panel));
    sample.json.insert(QStringLiteral("leftGapPhysical"), leftGap);
    sample.json.insert(QStringLiteral("bottomGapPhysical"), bottomGap);
    sample.json.insert(QStringLiteral("expectedGapPhysical"), gapPhysical);
    sample.json.insert(QStringLiteral("shadowInsetPhysical"), insetPhysical);
    sample.json.insert(QStringLiteral("marginsPassed"), margins);
    sample.json.insert(QStringLiteral("defaultLogicalSizePreserved"), size);
    sample.json.insert(QStringLiteral("targetMonitorAndScalePassed"), targetScale);
    sample.json.insert(QStringLiteral("visiblePanelContained"), contained);
    return sample.valid && margins && size && targetScale && contained;
}
#endif

} // namespace

int runNativeScreenTransitionProbe() {
    const auto outputDirectory = qEnvironmentVariable("XDSTARTER_CHROME_VALIDATION_DIR");
    if (outputDirectory.isEmpty() || !QDir::isAbsolutePath(outputDirectory) ||
        !QDir().mkpath(outputDirectory)) {
        qCritical() << "Set XDSTARTER_CHROME_VALIDATION_DIR to an absolute writable directory.";
        return 1;
    }
    QJsonObject evidence{
        {QStringLiteral("scope"), QStringLiteral("Real Windows QPA/RHI, real monitor geometry, "
                                                 "simulated tray signals and anchor selection.")},
        {QStringLiteral("noRealTrayRegistrationOrClicks"), true},
        {QStringLiteral("noUserLayoutIO"), true},
        {QStringLiteral("noApplicationCatalog"), true},
        {QStringLiteral("noGlobalInput"), true},
        {QStringLiteral("nativeMeasurement"),
         QStringLiteral("GetWindowRect; not placement output")},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platformName"), QGuiApplication::platformName()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("buildAbi"), QSysInfo::buildAbi()}};
    QJsonArray monitorsJson;
    QJsonArray qtScreensJson;
    QJsonArray cycles;
    QStringList errors;
    QStringList qmlWarnings;
    bool prerequisitesAvailable = true;
    const auto require = [&errors](bool condition, const QString& message) {
        if (!condition)
            errors.append(message);
        return condition;
    };
    const auto execute = [&]() -> bool {
#ifndef Q_OS_WIN
        prerequisitesAvailable = false;
        return require(false, QStringLiteral("The opt-in probe requires Windows."));
#else
        if (!require(QGuiApplication::platformName() == QStringLiteral("windows"),
                     QStringLiteral("The opt-in probe requires the real Windows QPA.")))
            return false;
        if (!require(!qEnvironmentVariableIsSet("QT_SCALE_FACTOR") &&
                         !qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS"),
                     QStringLiteral("Remove Qt scale overrides before measuring native DPI.")))
            return false;
        for (auto* screen : QGuiApplication::screens()) {
            const auto monitor = probeScreenMonitor(screen);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            const bool nativeInfoRead =
                monitor && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info)) != FALSE;
            qtScreensJson.append(
                QJsonObject{{QStringLiteral("name"), screen->name()},
                            {QStringLiteral("geometryDip"), rectangleJson(screen->geometry())},
                            {QStringLiteral("availableGeometryDip"),
                             rectangleJson(screen->availableGeometry())},
                            {QStringLiteral("devicePixelRatio"), screen->devicePixelRatio()},
                            {QStringLiteral("nativeMonitorInfoRead"), nativeInfoRead},
                            {QStringLiteral("nativeDeviceName"),
                             nativeInfoRead ? QString::fromWCharArray(info.szDevice) : QString()}});
        }
        std::vector<HMONITOR> handles;
        if (!require(EnumDisplayMonitors(nullptr, nullptr, collectProbeMonitor,
                                         reinterpret_cast<LPARAM>(&handles)) != FALSE,
                     QStringLiteral("Cannot enumerate native monitors.")))
            return false;
        QLibrary scalingLibrary(QStringLiteral("Shcore"));
        using ReadMonitorDpi = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        const auto readDpi =
            reinterpret_cast<ReadMonitorDpi>(scalingLibrary.resolve("GetDpiForMonitor"));
        if (!require(readDpi != nullptr, QStringLiteral("GetDpiForMonitor is unavailable.")))
            return false;
        std::vector<ProbeMonitor> monitors;
        const QSize expectedPanelSize(980, 680);
        const qreal expectedShadow = 24;
        const qreal expectedGap = 8;
        for (const auto handle : handles) {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            UINT dpiX = 0;
            UINT dpiY = 0;
            if (!require(GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&info)) != FALSE &&
                             SUCCEEDED(readDpi(handle, 0, &dpiX, &dpiY)) && dpiX > 0 &&
                             dpiX == dpiY,
                         QStringLiteral("Cannot read native monitor geometry or effective DPI.")))
                return false;
            ProbeMonitor monitor;
            monitor.handle = handle;
            monitor.name = QString::fromWCharArray(info.szDevice);
            monitor.physicalRect = QRect(info.rcMonitor.left, info.rcMonitor.top,
                                         info.rcMonitor.right - info.rcMonitor.left,
                                         info.rcMonitor.bottom - info.rcMonitor.top);
            monitor.workAreaPhysicalRect =
                QRect(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                      info.rcWork.bottom - info.rcWork.top);
            monitor.dpi = dpiX;
            int matchingQtScreens = 0;
            for (auto* screen : QGuiApplication::screens()) {
                if (probeScreenMonitor(screen) == monitor.handle) {
                    monitor.screen = screen;
                    ++matchingQtScreens;
                }
            }
            const qreal renderDpr = monitor.screen ? monitor.screen->devicePixelRatio() : 0;
            const bool fits =
                renderDpr > 0 &&
                monitor.workAreaPhysicalRect.width() >=
                    qRound((expectedPanelSize.width() + 2 * expectedGap) * renderDpr) &&
                monitor.workAreaPhysicalRect.height() >=
                    qRound((expectedPanelSize.height() + 2 * expectedGap) * renderDpr);
            monitorsJson.append(QJsonObject{
                {QStringLiteral("name"), monitor.name},
                {QStringLiteral("monitorPhysicalRect"), rectangleJson(monitor.physicalRect)},
                {QStringLiteral("workAreaPhysicalRect"),
                 rectangleJson(monitor.workAreaPhysicalRect)},
                {QStringLiteral("nativeEffectiveDpi"), static_cast<int>(monitor.dpi)},
                {QStringLiteral("matchingQtScreens"), matchingQtScreens},
                {QStringLiteral("qtScreenName"),
                 monitor.screen ? monitor.screen->name() : QString()},
                {QStringLiteral("qtScreenDpr"), renderDpr},
                {QStringLiteral("defaultPanelFits"), fits}});
            if (!require(matchingQtScreens == 1,
                         QStringLiteral("Native monitor %1 has %2 matching QScreen native handles; "
                                        "a unique mapping is required.")
                             .arg(monitor.name)
                             .arg(matchingQtScreens)))
                continue;
            if (fits)
                monitors.push_back(monitor);
        }
        if (!errors.isEmpty())
            return false;
        std::sort(monitors.begin(), monitors.end(), [](const auto& left, const auto& right) {
            return left.screen->devicePixelRatio() < right.screen->devicePixelRatio();
        });
        if (monitors.size() < 2 ||
            std::abs(monitors.front().screen->devicePixelRatio() -
                     monitors.back().screen->devicePixelRatio()) < 0.001 ||
            monitors.front().dpi == monitors.back().dpi) {
            prerequisitesAvailable = false;
            return require(false,
                           QStringLiteral("Two real monitors with different native DPI and "
                                          "Qt DPR, both fitting the default panel, are required."));
        }
        const std::vector<ProbeMonitor> targets{monitors.front(), monitors.back()};
        const auto anchorState = std::make_shared<ProbeAnchorState>();
        for (const auto& monitor : targets) {
            xdstarter::DesktopAnchorResult anchor;
            anchor.monitorName = monitor.name;
            anchor.monitorPhysicalRect = monitor.physicalRect;
            anchor.workAreaPhysicalRect = monitor.workAreaPhysicalRect;
            anchor.dpi = monitor.dpi;
            anchor.message = QStringLiteral("Probe-selected real monitor; no pointer movement.");
            anchorState->anchors.push_back(anchor);
        }
        // The HWND starts hidden on the low-DPR screen. The first host reveal
        // targets the high-DPR monitor, reproducing the stale hidden-window DPR.
        anchorState->selected.store(1);
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
        xdstarter::DesktopShellService shell(std::make_unique<ScreenProbePlatform>(anchorState));
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
        const qreal shadow = window->property("shadowInset").toReal();
        const qreal gap = window->property("screenGapDip").toReal();
        const QSize expectedLogicalSize(expectedPanelSize.width() + 2 * qRound(shadow),
                                        expectedPanelSize.height() + 2 * qRound(shadow));
        if (!require(shadow == expectedShadow && gap == expectedGap &&
                         window->size() == expectedLogicalSize,
                     QStringLiteral("The default 980x680 DIP panel/chrome contract changed.")))
            return false;
        window->setScreen(targets.front().screen);
        window->setPosition(targets.front().screen->geometry().center() -
                            QPoint(window->width() / 2, window->height() / 2));
        window->winId();
        if (!require(waitUntil([&] {
                         return !window->isVisible() &&
                                sampleProbeWindow(*window).monitor == targets.front().handle &&
                                std::abs(window->devicePixelRatio() -
                                         targets.front().screen->devicePixelRatio()) < 0.001;
                     }),
                     QStringLiteral("Cannot prepare a hidden low-DPR source HWND.")))
            return false;
        evidence.insert(QStringLiteral("hiddenSource"), sampleProbeWindow(*window).json);
        const auto placement = xdstarter::createDesktopPlatform();
        MemoryTray tray;
        xdstarter::DesktopWindowHost host(*window, shell, *placement, tray);
        ProbeWindowSample firstVisible;
        int visibleSignals = 0;
        QObject::connect(window, &QWindow::visibleChanged, window, [&](bool visible) {
            if (visible) {
                ++visibleSignals;
                firstVisible = sampleProbeWindow(*window);
            }
        });
        for (int cycle = 0; cycle < 6; ++cycle) {
            const size_t targetIndex = cycle % 2 == 0 ? 1 : 0;
            const auto& target = targets[targetIndex];
            anchorState->selected.store(targetIndex);
            firstVisible = {};
            const int previousSignals = visibleSignals;
            const auto before = sampleProbeWindow(*window);
            const bool crossedDpi =
                before.valid && before.monitor != target.handle &&
                std::abs(before.renderDpr - target.screen->devicePixelRatio()) > 0.001;
            if (cycle == 0)
                host.start(QImage(QStringLiteral(":/xdstarter/app/xdstarter-app-icon-32.png")));
            else
                tray.toggle();
            if (!require(waitUntil(
                             [&] {
                                 return visibleSignals > previousSignals && window->isVisible() &&
                                        window->isExposed();
                             },
                             5000),
                         QStringLiteral("Transition %1 reveal timed out.").arg(cycle + 1)))
                return false;
            settle(1000);
            auto settled = sampleProbeWindow(*window);
            const bool firstPassed =
                validateProbeSample(firstVisible, target, shadow, gap, expectedLogicalSize);
            const bool settledVisible =
                settled.nativeVisible && settled.qtVisible && settled.qtExposed;
            const bool settledPassed =
                validateProbeSample(settled, target, shadow, gap, expectedLogicalSize) &&
                settledVisible;
            const bool stable = firstVisible.physicalRect == settled.physicalRect &&
                                firstVisible.logicalSize == settled.logicalSize &&
                                std::abs(firstVisible.renderDpr - settled.renderDpr) < 0.001;
            const auto api = window->rendererInterface()->graphicsApi();
            const bool rhi = api != QSGRendererInterface::Software &&
                             api != QSGRendererInterface::Null &&
                             api != QSGRendererInterface::Unknown;
            cycles.append(
                QJsonObject{{QStringLiteral("cycle"), cycle + 1},
                            {QStringLiteral("targetMonitor"), target.name},
                            {QStringLiteral("targetWorkAreaPhysicalRect"),
                             rectangleJson(target.workAreaPhysicalRect)},
                            {QStringLiteral("targetNativeDpi"), static_cast<int>(target.dpi)},
                            {QStringLiteral("targetQtDpr"), target.screen->devicePixelRatio()},
                            {QStringLiteral("beforeReveal"), before.json},
                            {QStringLiteral("hiddenSourceUsesDifferentMonitorAndDpr"), crossedDpi},
                            {QStringLiteral("firstVisible"), firstVisible.json},
                            {QStringLiteral("settledAfter1000ms"), settled.json},
                            {QStringLiteral("firstVisiblePassed"), firstPassed},
                            {QStringLiteral("settledPassed"), settledPassed},
                            {QStringLiteral("settledVisiblePassed"), settledVisible},
                            {QStringLiteral("revealAndSettledGeometryMatch"), stable},
                            {QStringLiteral("graphicsApi"), apiName(api)},
                            {QStringLiteral("rhiPassed"), rhi}});
            require(crossedDpi && firstPassed && settledPassed && stable && rhi &&
                        window->isVisible(),
                    QStringLiteral("Transition %1 placement/DPR/size/RHI validation failed.")
                        .arg(cycle + 1));
            tray.toggle();
            if (!require(waitUntil([window] { return !window->isVisible(); }),
                         QStringLiteral("Transition %1 hide timed out.").arg(cycle + 1)))
                return false;
        }
        host.shutdown();
        window->close();
        require(qmlWarnings.isEmpty(), QStringLiteral("QML emitted warnings."));
        return errors.isEmpty();
#endif
    };
    const bool passed = execute();
    evidence.insert(QStringLiteral("prerequisitesAvailable"), prerequisitesAvailable);
    evidence.insert(QStringLiteral("requestedTransitions"), 6);
    evidence.insert(QStringLiteral("completedTransitions"), cycles.size());
    evidence.insert(QStringLiteral("monitors"), monitorsJson);
    evidence.insert(QStringLiteral("qtScreens"), qtScreensJson);
    evidence.insert(QStringLiteral("cycles"), cycles);
    evidence.insert(QStringLiteral("qmlWarnings"), QJsonArray::fromStringList(qmlWarnings));
    evidence.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
    evidence.insert(QStringLiteral("passed"), passed);
    QFile output(
        QDir(outputDirectory).filePath(QStringLiteral("native-screen-transition-probe.json")));
    const auto serialized = QJsonDocument(evidence).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(serialized) != serialized.size()) {
        qCritical() << "Cannot write native screen transition evidence:" << output.errorString();
        return 1;
    }
    qInfo().noquote() << "Native screen transition probe:"
                      << (passed                   ? "PASS"
                          : prerequisitesAvailable ? "FAIL"
                                                   : "UNAVAILABLE")
                      << "(real monitors and placement; simulated tray/anchor; no global input)";
    return passed ? 0 : prerequisitesAvailable ? 1 : 2;
}
