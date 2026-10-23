// tests/platform/native_taskbar_probe.cpp

#include "platform/desktop_platform.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QWindow>

#include <windows.h>

#include <commctrl.h>

#include <iostream>

namespace {
LRESULT CALLBACK blockFixtureSizingCommand(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR, DWORD_PTR) {
    if (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_SIZE)
        return 0; // Unit fixture: do not enter an OS modal input loop.
    return DefSubclassProc(window, message, wparam, lparam);
}
QRect rect(const RECT& value) {
    return {value.left, value.top, value.right - value.left, value.bottom - value.top};
}
QJsonArray jsonRect(QRect value) { return {value.x(), value.y(), value.width(), value.height()}; }
void settle(int milliseconds = 80) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
}
BOOL CALLBACK findTaskbar(HWND window, LPARAM parameter) {
    auto& anchor = *reinterpret_cast<xdstarter::DesktopAnchorResult*>(parameter);
    wchar_t name[64]{};
    wchar_t monitorName[32]{};
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    const auto handle = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    RECT bounds{};
    if ((!anchor.taskbarPhysicalRect.isValid() || IsWindowVisible(window)) &&
        GetClassNameW(window, name, 64) &&
        (wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) &&
        GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&monitor)) &&
        QString::fromWCharArray(monitor.szDevice) == anchor.monitorName &&
        GetWindowRect(window, &bounds)) {
        anchor.taskbarPhysicalRect = rect(bounds);
        anchor.dpi = GetDpiForWindow(window);
    }
    Q_UNUSED(monitorName);
    return TRUE;
}
BOOL CALLBACK collectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM parameter) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&info))) {
        xdstarter::DesktopAnchorResult anchor;
        anchor.monitorName = QString::fromWCharArray(info.szDevice);
        anchor.monitorPhysicalRect = rect(info.rcMonitor);
        anchor.workAreaPhysicalRect = rect(info.rcWork);
        const QSettings explorer(
            QStringLiteral(
                "HKEY_CURRENT_"
                "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"),
            QSettings::NativeFormat);
        anchor.centeredStart = explorer.value(QStringLiteral("TaskbarAl"), 0).toInt() == 1;
        EnumWindows(findTaskbar, reinterpret_cast<LPARAM>(&anchor));
        reinterpret_cast<QList<xdstarter::DesktopAnchorResult>*>(parameter)->append(anchor);
    }
    return TRUE;
}

struct ProductObservation {
    DWORD processId;
    const QList<xdstarter::DesktopAnchorResult>* monitors;
    QJsonArray samples;
};
BOOL CALLBACK observeProduct(HWND window, LPARAM parameter) {
    auto& context = *reinterpret_cast<ProductObservation*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t title[64]{};
    if (process != context.processId || !IsWindowVisible(window) ||
        GetAncestor(window, GA_ROOT) != window || !GetWindowTextW(window, title, 64) ||
        wcscmp(title, L"XDStarter") != 0)
        return TRUE;
    const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    RECT bounds{};
    if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info)) ||
        !GetWindowRect(window, &bounds))
        return TRUE;
    for (const auto& anchor : *context.monitors) {
        if (anchor.monitorName != QString::fromWCharArray(info.szDevice))
            continue;
        const qreal dpr = GetDpiForWindow(window) / 96.0;
        const int inset = qRound(24 * dpr);
        const QRect actual = rect(bounds).adjusted(inset, inset, -inset, -inset);
        const QRect expected =
            xdstarter::desktopPopupPhysicalRect(actual.size(), anchor, qRound(8 * dpr));
        context.samples.append(QJsonObject{
            {QStringLiteral("kind"), "live-product-window"},
            {QStringLiteral("monitor"), anchor.monitorName},
            {QStringLiteral("taskbarRect"), jsonRect(anchor.taskbarPhysicalRect)},
            {QStringLiteral("taskbarObserved"), anchor.taskbarPhysicalRect.isValid()},
            {QStringLiteral("workArea"), jsonRect(anchor.workAreaPhysicalRect)},
            {QStringLiteral("dpr"), dpr},
            {QStringLiteral("centered"), anchor.centeredStart},
            {QStringLiteral("edge"), static_cast<int>(xdstarter::desktopTaskbarEdge(anchor))},
            {QStringLiteral("actualPanel"), jsonRect(actual)},
            {QStringLiteral("expectedPanel"), jsonRect(expected)},
            {QStringLiteral("minimized"), IsIconic(window) != FALSE},
            {QStringLiteral("foreground"), GetForegroundWindow() == window},
            {QStringLiteral("positionPassed"), actual == expected && !IsIconic(window)}});
    }
    return TRUE;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (!app.arguments().contains(QStringLiteral("--run"))) {
        std::cerr << "Explicit --run required; this probe moves only its own hidden test window.\n";
        return 2;
    }
    if (app.arguments().contains(QStringLiteral("--resize-filter-test"))) {
        QWindow menu;
        menu.setFlags(Qt::Window | Qt::FramelessWindowHint);
        menu.setMinimumSize(QSize(528, 528));
        menu.resize(1028, 728);
        const HWND native = reinterpret_cast<HWND>(menu.winId());
        settle();
        const auto previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetWindowPos(native, nullptr, -24, 48, 1542, 1092, SWP_NOACTIVATE | SWP_NOZORDER);
        RECT origin{};
        GetWindowRect(native, &origin);
        int failures = 0;
        SetWindowSubclass(native, blockFixtureSizingCommand, 1, 0);
        {
            auto platform = xdstarter::createDesktopPlatform();
            const auto appearance = platform->configureMenuWindow(menu.winId());
            std::cerr << "Native fixture window=" << IsWindow(native)
                      << " configure=" << appearance.errorCode.toStdString() << '\n';
            for (UINT edge : {WMSZ_LEFT, WMSZ_RIGHT, WMSZ_TOP, WMSZ_BOTTOM, WMSZ_TOPLEFT,
                              WMSZ_TOPRIGHT, WMSZ_BOTTOMLEFT, WMSZ_BOTTOMRIGHT}) {
                SendMessageW(native, WM_ENTERSIZEMOVE, 0, 0);
                RECT proposed{origin.left + 24, origin.top + 24, origin.right + 100,
                              origin.bottom + 100};
                SendMessageW(native, WM_SIZING, edge, reinterpret_cast<LPARAM>(&proposed));
                const bool left =
                    edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT;
                const bool right =
                    edge == WMSZ_RIGHT || edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMRIGHT;
                const bool top = edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT;
                const bool bottom =
                    edge == WMSZ_BOTTOM || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT;
                if ((!left && proposed.left != origin.left) ||
                    (!right && proposed.right != origin.right) ||
                    (!top && proposed.top != origin.top) ||
                    (!bottom && proposed.bottom != origin.bottom))
                    ++failures;
                SendMessageW(native, WM_EXITSIZEMOVE, 0, 0);
            }
            SendMessageW(native, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOM, 0);
            SetWindowPos(native, nullptr, origin.left + 24, origin.top + 24,
                         origin.right - origin.left, origin.bottom - origin.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            RECT beforeEnter{};
            GetWindowRect(native, &beforeEnter);
            if (rect(beforeEnter) != rect(origin))
                ++failures;
            SendMessageW(native, WM_ENTERSIZEMOVE, 0, 0);
            RECT early{origin.left + 24, origin.top + 24, origin.right, origin.bottom + 100};
            SendMessageW(native, WM_SIZING, WMSZ_BOTTOM, reinterpret_cast<LPARAM>(&early));
            if (early.top != origin.top || early.left != origin.left || early.right != origin.right)
                ++failures;
            SetWindowPos(native, nullptr, origin.left + 24, origin.top + 24,
                         origin.right - origin.left, origin.bottom - origin.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            RECT afterSizing{};
            GetWindowRect(native, &afterSizing);
            if (rect(afterSizing) != rect(early))
                ++failures;
            SendMessageW(native, WM_EXITSIZEMOVE, 0, 0);
            RECT outside{0, 0, 800, 800};
            SendMessageW(native, WM_SIZING, WMSZ_BOTTOM, reinterpret_cast<LPARAM>(&outside));
            if (outside.left != 0 || outside.top != 0 || outside.right != 800 ||
                outside.bottom != 800)
                ++failures;
            QWindow unrelated;
            const HWND other = reinterpret_cast<HWND>(unrelated.winId());
            SendMessageW(other, WM_ENTERSIZEMOVE, 0, 0);
            SendMessageW(other, WM_SIZING, WMSZ_BOTTOM, reinterpret_cast<LPARAM>(&outside));
            SendMessageW(other, WM_EXITSIZEMOVE, 0, 0);
            if (outside.left != 0 || outside.top != 0 || outside.right != 800 ||
                outside.bottom != 800)
                ++failures;
        }
        RECT released{0, 0, 800, 800};
        SendMessageW(native, WM_ENTERSIZEMOVE, 0, 0);
        SendMessageW(native, WM_SIZING, WMSZ_BOTTOM, reinterpret_cast<LPARAM>(&released));
        SendMessageW(native, WM_EXITSIZEMOVE, 0, 0);
        if (released.left != 0 || released.top != 0 || released.right != 800 ||
            released.bottom != 800)
            ++failures;
        if (previous)
            SetThreadDpiAwarenessContext(previous);
        RemoveWindowSubclass(native, blockFixtureSizingCommand, 1);
        std::cout << QJsonDocument(QJsonObject{{QStringLiteral("failures"), failures},
                                               {QStringLiteral("ownWindowSizingCases"), 8},
                                               {QStringLiteral("inactiveAndUnrelatedCases"), 3},
                                               {QStringLiteral("sizingPhaseCases"), 3},
                                               {QStringLiteral("scope"),
                                                "WM_SIZING unit messages target only fixture-owned "
                                                "HWNDs; no global input."}})
                         .toJson()
                         .constData();
        return failures ? 1 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--launch-show-states"))) {
        QJsonArray launches;
        int failed = 0;
        for (const WORD mode : {WORD(SW_HIDE), WORD(SW_SHOWMINIMIZED)}) {
            QProcess child;
            child.setCreateProcessArgumentsModifier([mode](QProcess::CreateProcessArguments* args) {
                args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
                args->startupInfo->wShowWindow = mode;
            });
            child.start(QCoreApplication::applicationFilePath(),
                        {QStringLiteral("--run"), QStringLiteral("--show-state")});
            const bool finished = child.waitForFinished(25000);
            const auto result = QJsonDocument::fromJson(child.readAllStandardOutput());
            const bool passed = finished && child.exitStatus() == QProcess::NormalExit &&
                                child.exitCode() == 0 && result.isObject();
            if (!passed)
                ++failed;
            if (!finished) {
                child.kill();
                child.waitForFinished(1000);
            }
            launches.append(QJsonObject{{QStringLiteral("startupShowMode"), mode},
                                        {QStringLiteral("passed"), passed},
                                        {QStringLiteral("result"), result.object()}});
        }
        std::cout << QJsonDocument(QJsonObject{{QStringLiteral("failures"), failed},
                                               {QStringLiteral("launches"), launches}})
                         .toJson()
                         .constData();
        return failed ? 1 : 0;
    }
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QList<xdstarter::DesktopAnchorResult> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));
    const int observeIndex = app.arguments().indexOf(QStringLiteral("--observe-product"));
    if (observeIndex >= 0) {
        bool converted = false;
        const auto pid = app.arguments().value(observeIndex + 1).toUInt(&converted);
        ProductObservation observation{pid, &monitors, {}};
        if (converted && pid)
            EnumWindows(observeProduct, reinterpret_cast<LPARAM>(&observation));
        std::cout << QJsonDocument(
                         QJsonObject{{QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
                                     {QStringLiteral("samples"), observation.samples}})
                         .toJson()
                         .constData();
        if (previous)
            SetThreadDpiAwarenessContext(previous);
        return observation.samples.size() == 1 ? 0 : 1;
    }
    auto platform = xdstarter::createDesktopPlatform();
    QJsonArray samples;
    int failures = 0;
    for (const auto& observed : monitors) {
        auto* screen = platform->screenForAnchor(observed);
        if (!screen) {
            ++failures;
            continue;
        }
        QWindow window;
        window.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        window.setSurfaceType(QSurface::RasterSurface);
        window.setScreen(screen);
        window.setGeometry(
            QRect(screen->availableGeometry().center() - QPoint(264, 264), QSize(528, 528)));
        window.create();
        settle(300);
        const qreal dpr = window.devicePixelRatio();
        for (int edge = -1; edge < 4; ++edge) {
            for (const bool centered : {false, true}) {
                if (edge == -1 && centered != observed.centeredStart)
                    continue;
                auto anchor = observed;
                if (edge >= 0) {
                    const auto monitor = anchor.monitorPhysicalRect;
                    const int thickness = qRound(48 * dpr);
                    anchor.centeredStart = centered;
                    if (edge == 0) {
                        anchor.taskbarPhysicalRect =
                            QRect(monitor.left(), monitor.bottom() - thickness + 1, monitor.width(),
                                  thickness);
                        anchor.workAreaPhysicalRect = monitor.adjusted(0, 0, 0, -thickness);
                    } else if (edge == 1) {
                        anchor.taskbarPhysicalRect =
                            QRect(monitor.topLeft(), QSize(monitor.width(), thickness));
                        anchor.workAreaPhysicalRect = monitor.adjusted(0, thickness, 0, 0);
                    } else if (edge == 2) {
                        anchor.taskbarPhysicalRect =
                            QRect(monitor.topLeft(), QSize(thickness, monitor.height()));
                        anchor.workAreaPhysicalRect = monitor.adjusted(thickness, 0, 0, 0);
                    } else {
                        anchor.taskbarPhysicalRect =
                            QRect(monitor.right() - thickness + 1, monitor.top(), thickness,
                                  monitor.height());
                        anchor.workAreaPhysicalRect = monitor.adjusted(0, 0, -thickness, 0);
                    }
                }
                for (const QSize panelDip : {QSize(480, 480), QSize(980, 680)}) {
                    window.resize(panelDip + QSize(48, 48));
                    settle();
                    const int inset = qRound(24 * dpr);
                    const int gap = qRound(8 * dpr);
                    const auto before = platform->queryWindowMetrics(window.winId());
                    const QSize panelSize = before.physicalSize - QSize(inset * 2, inset * 2);
                    const auto expected =
                        xdstarter::desktopPopupPhysicalRect(panelSize, anchor, gap);
                    const auto placed =
                        platform->placeWindow(window.winId(), anchor, gap, false, inset);
                    settle();
                    RECT native{};
                    const bool read =
                        GetWindowRect(reinterpret_cast<HWND>(window.winId()), &native);
                    const QRect actual = rect(native).adjusted(inset, inset, -inset, -inset);
                    const bool passed = before.success && placed.success && read &&
                                        actual == expected && actual == placed.physicalRect &&
                                        anchor.workAreaPhysicalRect.contains(actual) &&
                                        !window.isVisible() &&
                                        qFuzzyCompare(dpr, screen->devicePixelRatio());
                    if (!passed)
                        ++failures;
                    samples.append(QJsonObject{
                        {QStringLiteral("kind"),
                         edge < 0 ? "observed-taskbar" : "native-window-synthetic-dock"},
                        {QStringLiteral("monitor"), anchor.monitorName},
                        {QStringLiteral("edge"), edge},
                        {QStringLiteral("centered"), centered},
                        {QStringLiteral("dpr"), dpr},
                        {QStringLiteral("taskbarDpi"), static_cast<int>(observed.dpi)},
                        {QStringLiteral("workArea"), jsonRect(anchor.workAreaPhysicalRect)},
                        {QStringLiteral("expectedPanel"), jsonRect(expected)},
                        {QStringLiteral("actualPanel"), jsonRect(actual)},
                        {QStringLiteral("resizeEdges"),
                         static_cast<int>(xdstarter::desktopResizeEdges(anchor))},
                        {QStringLiteral("passed"), passed}});
                    if (app.arguments().contains(QStringLiteral("--show-state")) &&
                        samples.size() == 1) {
                        window.showNormal();
                        settle();
                        const bool firstVisible =
                            IsWindowVisible(reinterpret_cast<HWND>(window.winId()));
                        const bool firstIconic = IsIconic(reinterpret_cast<HWND>(window.winId()));
                        const auto shown = platform->presentMenuWindow(window.winId());
                        settle();
                        const bool showPassed = shown.supported && shown.visible &&
                                                !shown.minimized && shown.foreground;
                        if (!showPassed)
                            ++failures;
                        samples.append(
                            QJsonObject{{QStringLiteral("kind"), "explicit-native-presentation"},
                                        {QStringLiteral("firstQtShowVisible"), firstVisible},
                                        {QStringLiteral("firstQtShowMinimized"), firstIconic},
                                        {QStringLiteral("nativeVisible"), shown.visible},
                                        {QStringLiteral("nativeMinimized"), shown.minimized},
                                        {QStringLiteral("foreground"), shown.foreground},
                                        {QStringLiteral("passed"), showPassed}});
                        window.hide();
                        settle();
                    }
                }
            }
        }
    }
    QFile executable(QCoreApplication::applicationFilePath());
    executable.open(QIODevice::ReadOnly);
    const QJsonObject result{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("qtVersion"), QT_VERSION_STR},
        {QStringLiteral("probeSha256"),
         QString::fromLatin1(
             QCryptographicHash::hash(executable.readAll(), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("scope"),
         "Own hidden HWND measurements at actual monitor DPR; synthetic docks do not change "
         "Explorer or prove physical border dragging."},
        {QStringLiteral("failures"), failures},
        {QStringLiteral("samples"), samples}};
    std::cout << QJsonDocument(result).toJson().constData();
    if (previous)
        SetThreadDpiAwarenessContext(previous);
    return failures || samples.isEmpty() ? 1 : 0;
}
