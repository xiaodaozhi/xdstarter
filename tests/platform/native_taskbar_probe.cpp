// tests/platform/native_taskbar_probe.cpp

#include "platform/desktop_platform.h"

#include <QCryptographicHash>
#include <QDateTime>
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
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <iostream>
#include <thread>

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
    HWND window = nullptr;
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
    context.window = window;
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

QJsonObject windowIdentity(HWND window) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    RECT bounds{};
    GetWindowRect(window, &bounds);
    wchar_t className[128]{};
    GetClassNameW(window, className, 128);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    const int regionKind = region ? GetWindowRgn(window, region) : ERROR;
    if (region)
        DeleteObject(region);
    return {{QStringLiteral("handle"), QString::number(reinterpret_cast<quintptr>(window), 16)},
            {QStringLiteral("processId"), static_cast<qint64>(process)},
            {QStringLiteral("threadId"), static_cast<qint64>(thread)},
            {QStringLiteral("className"), QString::fromWCharArray(className)},
            {QStringLiteral("rect"), jsonRect(rect(bounds))},
            {QStringLiteral("style"), QString::number(GetWindowLongPtrW(window, GWL_STYLE), 16)},
            {QStringLiteral("extendedStyle"),
             QString::number(GetWindowLongPtrW(window, GWL_EXSTYLE), 16)},
            {QStringLiteral("visible"), IsWindowVisible(window) != FALSE},
            {QStringLiteral("minimized"), IsIconic(window) != FALSE},
            {QStringLiteral("regionKind"), regionKind}};
}

QJsonObject observeStartOverlap(HWND menu, const xdstarter::DesktopAnchorResult& anchor) {
    using Microsoft::WRL::ComPtr;
    QJsonObject result{{QStringLiteral("readOnly"), true},
                       {QStringLiteral("inputInjected"), false},
                       {QStringLiteral("capturePermissionGranted"), false},
                       {QStringLiteral("observedAtUtc"),
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    struct ObservationContext {
        DPI_AWARENESS_CONTEXT previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ObservationContext() {
            if (SUCCEEDED(initialized))
                CoUninitialize();
            if (previous)
                SetThreadDpiAwarenessContext(previous);
        }
    } context;
    const auto finish = [&](QString error = {}) {
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("measurementCompleted"), error.isEmpty());
        return result;
    };
    if (FAILED(context.initialized))
        return finish(QStringLiteral("com-initialization-failed"));
    HWND taskbar = nullptr;
    HWND candidate = GetTopWindow(nullptr);
    for (int index = 0; candidate && index < 8192;
         candidate = GetWindow(candidate, GW_HWNDNEXT), ++index) {
        wchar_t name[64]{};
        RECT bounds{};
        if (GetClassNameW(candidate, name, 64) && IsWindowVisible(candidate) &&
            (wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) &&
            GetWindowRect(candidate, &bounds) && rect(bounds) == anchor.taskbarPhysicalRect) {
            taskbar = candidate;
            break;
        }
    }
    if (!taskbar)
        return finish(QStringLiteral("taskbar-window-unavailable"));
    result.insert(QStringLiteral("menu"), windowIdentity(menu));
    result.insert(QStringLiteral("taskbar"), windowIdentity(taskbar));
    result.insert(QStringLiteral("taskbarEdge"),
                  static_cast<int>(xdstarter::desktopTaskbarEdge(anchor)));
    bool menuBeforeTaskbar = false;
    candidate = GetTopWindow(nullptr);
    for (int index = 0; candidate && index < 8192;
         candidate = GetWindow(candidate, GW_HWNDNEXT), ++index) {
        if (candidate == taskbar)
            break;
        if (candidate == menu) {
            menuBeforeTaskbar = true;
            break;
        }
    }
    result.insert(QStringLiteral("menuBeforeTaskbarInZOrder"), menuBeforeTaskbar);
    ComPtr<IUIAutomation> automation;
    HRESULT status = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(automation.GetAddressOf()));
    if (FAILED(status))
        return finish(QStringLiteral("uia-unavailable"));
    ComPtr<IUIAutomation2> bounded;
    if (FAILED(automation.As(&bounded)) || FAILED(bounded->put_ConnectionTimeout(100)) ||
        FAILED(bounded->put_TransactionTimeout(100)))
        return finish(QStringLiteral("uia-timeout-configuration-failed"));
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(taskbar, root.GetAddressOf())))
        return finish(QStringLiteral("uia-taskbar-unavailable"));
    VARIANT identifier{};
    identifier.vt = VT_BSTR;
    identifier.bstrVal = SysAllocString(L"StartButton");
    if (!identifier.bstrVal)
        return finish(QStringLiteral("uia-out-of-memory"));
    ComPtr<IUIAutomationCondition> idCondition;
    status = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, identifier,
                                                 idCondition.GetAddressOf());
    VariantClear(&identifier);
    VARIANT type{};
    type.vt = VT_I4;
    type.lVal = UIA_ButtonControlTypeId;
    ComPtr<IUIAutomationCondition> typeCondition;
    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(status) ||
        FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type,
                                                   typeCondition.GetAddressOf())) ||
        FAILED(automation->CreateAndCondition(idCondition.Get(), typeCondition.Get(),
                                              condition.GetAddressOf())))
        return finish(QStringLiteral("uia-condition-failed"));
    ComPtr<IUIAutomationElementArray> starts;
    int count = 0;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), starts.GetAddressOf())) ||
        FAILED(starts->get_Length(&count)) || count != 1)
        return finish(QStringLiteral("start-button-not-uniquely-identified"));
    ComPtr<IUIAutomationElement> start;
    RECT rawStart{};
    BOOL offscreen = TRUE;
    int provider = 0;
    DWORD taskbarProcess = 0;
    GetWindowThreadProcessId(taskbar, &taskbarProcess);
    if (FAILED(starts->GetElement(0, start.GetAddressOf())) ||
        FAILED(start->get_CurrentBoundingRectangle(&rawStart)) ||
        FAILED(start->get_CurrentIsOffscreen(&offscreen)) || offscreen ||
        FAILED(start->get_CurrentProcessId(&provider)) || provider != int(taskbarProcess))
        return finish(QStringLiteral("start-button-unavailable"));
    const QRect button = rect(rawStart);
    if (!button.isValid() || button.width() > 256 || button.height() > 256 ||
        !anchor.taskbarPhysicalRect.contains(button))
        return finish(QStringLiteral("start-button-bounds-invalid"));
    result.insert(QStringLiteral("startButton"), jsonRect(button));
    QRect menuRect;
    RECT rawMenu{};
    if (GetWindowRect(menu, &rawMenu))
        menuRect = rect(rawMenu);
    result.insert(QStringLiteral("outerOverlap"), jsonRect(menuRect.intersected(button)));
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation->get_RawViewWalker(walker.GetAddressOf())))
        return finish(QStringLiteral("uia-walker-unavailable"));
    QList<QPair<QString, QPoint>> points{
        {QStringLiteral("center"), button.center()},
        {QStringLiteral("top-left"), button.topLeft() + QPoint(1, 1)},
        {QStringLiteral("top-right"), button.topRight() + QPoint(-1, 1)},
        {QStringLiteral("bottom-left"), button.bottomLeft() + QPoint(1, -1)},
        {QStringLiteral("bottom-right"), button.bottomRight() + QPoint(-1, -1)}};
    const qreal dpr = GetDpiForWindow(menu) / 96.0;
    for (int dip : {1, 4, 8, 16}) {
        const int offset = qMax(1, qRound(dip * dpr));
        QPoint point = button.center();
        switch (xdstarter::desktopTaskbarEdge(anchor)) {
        case xdstarter::TaskbarEdge::Bottom:
            point.setY(qMin(button.bottom(), button.top() + offset - 1));
            break;
        case xdstarter::TaskbarEdge::Top:
            point.setY(qMax(button.top(), button.bottom() - offset + 1));
            break;
        case xdstarter::TaskbarEdge::Left:
            point.setX(qMax(button.left(), button.right() - offset + 1));
            break;
        case xdstarter::TaskbarEdge::Right:
            point.setX(qMin(button.right(), button.left() + offset - 1));
            break;
        }
        points.append({QStringLiteral("taskbar-inner-edge-%1dip").arg(dip), point});
    }
    QJsonArray samples;
    int menuHits = 0;
    POINT cursorBefore{}, cursorAfter{};
    GetPhysicalCursorPos(&cursorBefore);
    for (const auto& namedPoint : points) {
        const auto point = namedPoint.second;
        const POINT nativePoint{point.x(), point.y()};
        const HWND hit = WindowFromPoint(nativePoint);
        const HWND hitRoot = GetAncestor(hit, GA_ROOT);
        const bool menuHit = hitRoot == menu;
        menuHits += menuHit ? 1 : 0;
        ComPtr<IUIAutomationElement> current;
        const HRESULT pointResult =
            automation->ElementFromPoint(nativePoint, current.GetAddressOf());
        BOOL isStart = FALSE;
        for (int depth = 0; SUCCEEDED(pointResult) && current && depth < 8; ++depth) {
            if (FAILED(automation->CompareElements(start.Get(), current.Get(), &isStart)) ||
                isStart)
                break;
            ComPtr<IUIAutomationElement> parent;
            if (FAILED(walker->GetParentElement(current.Get(), parent.GetAddressOf())))
                break;
            current = parent;
        }
        samples.append(QJsonObject{
            {QStringLiteral("name"), namedPoint.first},
            {QStringLiteral("point"), QJsonArray{point.x(), point.y()}},
            {QStringLiteral("insideMenuOuterRect"), menuRect.contains(point)},
            {QStringLiteral("windowHit"), windowIdentity(hit)},
            {QStringLiteral("rootHitHandle"),
             QString::number(reinterpret_cast<quintptr>(hitRoot), 16)},
            {QStringLiteral("menuHit"), menuHit},
            {QStringLiteral("taskbarRootHit"), hitRoot == taskbar},
            {QStringLiteral("uiaSucceeded"), SUCCEEDED(pointResult)},
            {QStringLiteral("uiaStartOrDescendant"), isStart != FALSE},
            {QStringLiteral("uiaResult"), QString::number(static_cast<quint32>(pointResult), 16)}});
    }
    GetPhysicalCursorPos(&cursorAfter);
    result.insert(QStringLiteral("samples"), samples);
    result.insert(QStringLiteral("menuHitCount"), menuHits);
    result.insert(QStringLiteral("cursorUnchangedDuringObservation"),
                  cursorBefore.x == cursorAfter.x && cursorBefore.y == cursorAfter.y);
    RECT finalMenu{}, finalTaskbar{}, finalStart{};
    const bool stable =
        IsWindowVisible(menu) && !IsIconic(menu) && GetWindowRect(menu, &finalMenu) &&
        GetWindowRect(taskbar, &finalTaskbar) && rect(finalMenu) == menuRect &&
        rect(finalTaskbar) == anchor.taskbarPhysicalRect &&
        SUCCEEDED(start->get_CurrentBoundingRectangle(&finalStart)) && rect(finalStart) == button;
    result.insert(QStringLiteral("geometryStable"), stable);
    return finish(stable ? QString{} : QStringLiteral("windows-changed-during-observation"));
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
        menu.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
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
            const auto extended = GetWindowLongPtrW(native, GWL_EXSTYLE);
            if (!(extended & WS_EX_TOOLWINDOW) || (extended & WS_EX_APPWINDOW) ||
                !(extended & WS_EX_TOPMOST) || IsWindowVisible(native))
                ++failures;
            {
                QWindow owned;
                owned.setFlags(Qt::Dialog | Qt::FramelessWindowHint);
                owned.setTransientParent(&menu);
                const HWND dialog = reinterpret_cast<HWND>(owned.winId());
                if (GetWindow(dialog, GW_OWNER) != native ||
                    !(GetWindowLongPtrW(native, GWL_EXSTYLE) & WS_EX_TOPMOST))
                    ++failures;
            }
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
            const QRect allowed = rect(origin).adjusted(-30, -40, 80, 90);
            menu.setProperty("menuOuterBoundsPhysical", allowed);
            for (UINT edge : {WMSZ_LEFT, WMSZ_RIGHT, WMSZ_TOP, WMSZ_BOTTOM, WMSZ_TOPLEFT,
                              WMSZ_TOPRIGHT, WMSZ_BOTTOMLEFT, WMSZ_BOTTOMRIGHT}) {
                SendMessageW(native, WM_ENTERSIZEMOVE, 0, 0);
                RECT proposed{origin.left - 1000, origin.top - 1000, origin.right + 1000,
                              origin.bottom + 1000};
                SendMessageW(native, WM_SIZING, edge, reinterpret_cast<LPARAM>(&proposed));
                if (!allowed.contains(rect(proposed)))
                    ++failures;
                SendMessageW(native, WM_EXITSIZEMOVE, 0, 0);
            }
            menu.setProperty("menuOuterBoundsPhysical", QRect{});
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
            for (int edge = 0; edge < 4; ++edge) {
                xdstarter::DesktopAnchorResult anchor;
                anchor.monitorPhysicalRect = QRect(-1200, -800, 2400, 1600);
                const auto screen = anchor.monitorPhysicalRect;
                constexpr int thickness = 72;
                if (edge == 0) {
                    anchor.taskbarPhysicalRect = QRect(
                        screen.left(), screen.bottom() - thickness + 1, screen.width(), thickness);
                    anchor.workAreaPhysicalRect = screen.adjusted(0, 0, 0, -thickness);
                } else if (edge == 1) {
                    anchor.taskbarPhysicalRect =
                        QRect(screen.topLeft(), QSize(screen.width(), thickness));
                    anchor.workAreaPhysicalRect = screen.adjusted(0, thickness, 0, 0);
                } else if (edge == 2) {
                    anchor.taskbarPhysicalRect =
                        QRect(screen.topLeft(), QSize(thickness, screen.height()));
                    anchor.workAreaPhysicalRect = screen.adjusted(thickness, 0, 0, 0);
                } else {
                    anchor.taskbarPhysicalRect = QRect(screen.right() - thickness + 1, screen.top(),
                                                       thickness, screen.height());
                    anchor.workAreaPhysicalRect = screen.adjusted(0, 0, -thickness, 0);
                }
                const auto placed = platform->placeWindow(menu.winId(), anchor, 12, false, 36);
                const auto checkRegion = [&] {
                    RECT actual{};
                    if (!GetWindowRect(native, &actual))
                        return false;
                    const QRect outer = rect(actual);
                    HRGN region = CreateRectRgn(0, 0, 0, 0);
                    if (!region)
                        return false;
                    bool passed = GetWindowRgn(native, region) != ERROR;
                    const QRect overlap = outer.intersected(anchor.taskbarPhysicalRect);
                    if (overlap.isValid()) {
                        const QPoint point = overlap.center() - outer.topLeft();
                        passed = passed && !PtInRegion(region, point.x(), point.y());
                    }
                    for (QPoint local : {QPoint(0, outer.height() / 2),
                                         QPoint(outer.width() - 1, outer.height() / 2),
                                         QPoint(outer.width() / 2, 0),
                                         QPoint(outer.width() / 2, outer.height() - 1),
                                         QPoint(outer.width() / 2, outer.height() / 2)}) {
                        const bool excluded =
                            anchor.taskbarPhysicalRect.contains(local + outer.topLeft());
                        passed = passed &&
                                 (PtInRegion(region, local.x(), local.y()) != FALSE) != excluded;
                    }
                    DeleteObject(region);
                    return passed && !IsWindowVisible(native);
                };
                if (!placed.success || !checkRegion())
                    ++failures;
                RECT positioned{};
                GetWindowRect(native, &positioned);
                SetWindowPos(native, nullptr, positioned.left + 7, positioned.top + 7,
                             positioned.right - positioned.left + 31,
                             positioned.bottom - positioned.top - 15,
                             SWP_NOACTIVATE | SWP_NOZORDER);
                if (!checkRegion())
                    ++failures;
                menu.setMask(QRegion(QRect(0, 0, 1, 1)));
                menu.setMask(QRegion{});
                if (!checkRegion())
                    ++failures;
            }
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
                                               {QStringLiteral("boundedSizingCases"), 8},
                                               {QStringLiteral("hiddenWindowPolicyCases"), 1},
                                               {QStringLiteral("ownedDialogPolicyCases"), 1},
                                               {QStringLiteral("taskbarInputRegionCases"), 12},
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
    const int overlapIndex = app.arguments().indexOf(QStringLiteral("--observe-start-overlap"));
    const int observeIndex = overlapIndex >= 0
                                 ? overlapIndex
                                 : app.arguments().indexOf(QStringLiteral("--observe-product"));
    if (observeIndex >= 0) {
        bool converted = false;
        const auto pid = app.arguments().value(observeIndex + 1).toUInt(&converted);
        ProductObservation observation{pid, &monitors, {}};
        if (converted && pid)
            EnumWindows(observeProduct, reinterpret_cast<LPARAM>(&observation));
        if (overlapIndex >= 0) {
            QJsonObject result{{QStringLiteral("measurementCompleted"), false},
                               {QStringLiteral("error"), "expected-one-visible-product-window"}};
            if (observation.samples.size() == 1 && observation.window) {
                const auto monitor = MonitorFromWindow(observation.window, MONITOR_DEFAULTTONULL);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info))) {
                    for (const auto& anchor : monitors) {
                        if (anchor.monitorName != QString::fromWCharArray(info.szDevice))
                            continue;
                        std::thread worker(
                            [&] { result = observeStartOverlap(observation.window, anchor); });
                        worker.join();
                        break;
                    }
                }
            }
            std::cout << QJsonDocument(result).toJson().constData();
            if (previous)
                SetThreadDpiAwarenessContext(previous);
            return result.value(QStringLiteral("measurementCompleted")).toBool() ? 0 : 1;
        }
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
        window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                        Qt::WindowStaysOnTopHint);
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
