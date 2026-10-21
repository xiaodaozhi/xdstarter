#include "windows_desktop_platform.h"

#include <QDir>
#include <QVector>

#include <algorithm>
#include <iterator>
#include <limits>

#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uiautomation.h>

namespace xdstarter {
namespace {

template <typename T> class ComPtr {
  public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() {
        if (pointer_) {
            pointer_->Release();
        }
    }
    T* operator->() const { return pointer_; }
    T* get() const { return pointer_; }
    T** put() { return &pointer_; }

  private:
    T* pointer_ = nullptr;
};

class ComApartment {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    bool ready() const { return SUCCEEDED(result_); }
    HRESULT result() const { return result_; }

  private:
    HRESULT result_;
};

class PhysicalCoordinates {
  public:
    PhysicalCoordinates()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalCoordinates() {
        if (previous_) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }
    bool ready() const { return previous_ != nullptr; }

  private:
    DPI_AWARENESS_CONTEXT previous_;
};

class KnownFolderPath {
  public:
    ~KnownFolderPath() { CoTaskMemFree(value); }
    PWSTR value = nullptr;
};

QString nativeErrorCode(const QString& prefix, HRESULT error) {
    return prefix +
           QStringLiteral("-0x%1").arg(static_cast<quint32>(error), 8, 16, QLatin1Char('0'));
}

QRect checkedRectangle(qint64 left, qint64 top, qint64 rightExclusive, qint64 bottomExclusive) {
    constexpr qint64 minimum = std::numeric_limits<int>::min();
    constexpr qint64 maximum = std::numeric_limits<int>::max();
    const qint64 width = rightExclusive - left;
    const qint64 height = bottomExclusive - top;
    if (left < minimum || top < minimum || left > maximum || top > maximum ||
        rightExclusive - 1 < minimum || bottomExclusive - 1 < minimum ||
        rightExclusive - 1 > maximum || bottomExclusive - 1 > maximum || width <= 0 ||
        height <= 0 || width > maximum || height > maximum) {
        return {};
    }
    return {QPoint(static_cast<int>(left), static_cast<int>(top)),
            QPoint(static_cast<int>(rightExclusive - 1), static_cast<int>(bottomExclusive - 1))};
}

QRect rectangle(const RECT& value) {
    return checkedRectangle(value.left, value.top, value.right, value.bottom);
}

void setMonitor(DesktopAnchorResult& result, HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info))) {
        result.monitorName = QString::fromWCharArray(info.szDevice);
        result.monitorPhysicalRect = rectangle(info.rcMonitor);
        result.workAreaPhysicalRect = rectangle(info.rcWork);
    }
}

bool isExplorerWindow(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return false;
    }
    wchar_t image[32768]{};
    DWORD length = static_cast<DWORD>(std::size(image));
    const bool read = QueryFullProcessImageNameW(process, 0, image, &length) != FALSE;
    CloseHandle(process);
    wchar_t windowsDirectory[MAX_PATH]{};
    const UINT windowsLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    return read && windowsLength > 0 && windowsLength < MAX_PATH &&
           QDir::cleanPath(QString::fromWCharArray(image, static_cast<int>(length)))
                   .compare(QDir::cleanPath(QString::fromWCharArray(windowsDirectory) +
                                            QStringLiteral("/explorer.exe")),
                            Qt::CaseInsensitive) == 0;
}

BOOL CALLBACK collectTaskbars(HWND window, LPARAM parameter) {
    try {
        auto* taskbars = reinterpret_cast<QVector<HWND>*>(parameter);
        if (taskbars->size() >= 32) {
            return FALSE;
        }
        wchar_t className[128]{};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
            (wcscmp(className, L"Shell_TrayWnd") == 0 ||
             wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) &&
            IsWindowVisible(window) && isExplorerWindow(window)) {
            taskbars->append(window);
        }
    } catch (...) {
        return FALSE;
    }
    return TRUE;
}

bool findStartButton(IUIAutomation* automation, HWND taskbar, QRect& button, QString& error) {
    ComPtr<IUIAutomationElement> root;
    HRESULT result = automation->ElementFromHandle(taskbar, root.put());
    if (FAILED(result)) {
        error = nativeErrorCode(QStringLiteral("uia-taskbar"), result);
        return false;
    }

    VARIANT identifier{};
    identifier.vt = VT_BSTR;
    identifier.bstrVal = SysAllocString(L"StartButton");
    if (!identifier.bstrVal) {
        error = QStringLiteral("uia-out-of-memory");
        return false;
    }
    ComPtr<IUIAutomationCondition> idCondition;
    result = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, identifier,
                                                 idCondition.put());
    VariantClear(&identifier);
    if (FAILED(result)) {
        error = nativeErrorCode(QStringLiteral("uia-id-condition"), result);
        return false;
    }
    VARIANT type{};
    type.vt = VT_I4;
    type.lVal = UIA_ButtonControlTypeId;
    ComPtr<IUIAutomationCondition> typeCondition;
    ComPtr<IUIAutomationCondition> condition;
    result =
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, typeCondition.put());
    if (SUCCEEDED(result)) {
        result =
            automation->CreateAndCondition(idCondition.get(), typeCondition.get(), condition.put());
    }
    ComPtr<IUIAutomationElementArray> elements;
    if (SUCCEEDED(result)) {
        result = root->FindAll(TreeScope_Descendants, condition.get(), elements.put());
    }
    int count = 0;
    if (FAILED(result) || FAILED(elements->get_Length(&count))) {
        error = nativeErrorCode(QStringLiteral("uia-start-query"), result);
        return false;
    }
    // Multiple candidates are ambiguous. Never choose by localized Name,
    // screen position or the first child of the taskbar.
    if (count != 1) {
        error = count == 0 ? QStringLiteral("start-button-not-identified")
                           : QStringLiteral("ambiguous-start-button");
        return false;
    }
    ComPtr<IUIAutomationElement> element;
    RECT bounds{};
    RECT taskbarBounds{};
    BOOL offscreen = TRUE;
    int providerProcess = 0;
    DWORD taskbarProcess = 0;
    GetWindowThreadProcessId(taskbar, &taskbarProcess);
    if (FAILED(elements->GetElement(0, element.put())) ||
        FAILED(element->get_CurrentBoundingRectangle(&bounds)) ||
        FAILED(element->get_CurrentIsOffscreen(&offscreen)) || offscreen ||
        FAILED(element->get_CurrentProcessId(&providerProcess)) ||
        providerProcess != static_cast<int>(taskbarProcess) ||
        !GetWindowRect(taskbar, &taskbarBounds)) {
        error = QStringLiteral("start-button-unavailable");
        return false;
    }
    const QRect candidate = rectangle(bounds);
    const QRect taskbarArea = rectangle(taskbarBounds);
    if (!candidate.isValid() || candidate.width() > 256 || candidate.height() > 256 ||
        !taskbarArea.isValid() || candidate.left() < static_cast<qint64>(taskbarArea.left()) - 2 ||
        candidate.top() < static_cast<qint64>(taskbarArea.top()) - 2 ||
        candidate.right() > static_cast<qint64>(taskbarArea.right()) + 2 ||
        candidate.bottom() > static_cast<qint64>(taskbarArea.bottom()) + 2) {
        error = QStringLiteral("invalid-start-button-bounds");
        return false;
    }
    button = candidate;
    return true;
}

DesktopActionResult shellOpen(const wchar_t* target) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = target;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        return {false, QStringLiteral("无法打开桌面快捷入口，请稍后重试。"),
                nativeErrorCode(QStringLiteral("shell-open"), HRESULT_FROM_WIN32(GetLastError()))};
    }
    return {true, QStringLiteral("已打开桌面快捷入口。"), {}};
}

} // namespace

DesktopActionResult WindowsDesktopPlatform::openDesktopAction(const QString& actionId) {
    if (!isSupportedDesktopAction(actionId)) {
        return {false, QStringLiteral("不支持此桌面快捷操作。"),
                QStringLiteral("unsupported-action")};
    }
    ComApartment apartment;
    if (!apartment.ready()) {
        return {false, QStringLiteral("无法初始化桌面快捷操作。"),
                nativeErrorCode(QStringLiteral("com-initialize"), apartment.result())};
    }
    if (actionId == QStringLiteral("settings")) {
        return shellOpen(L"ms-settings:");
    }
    if (actionId == QStringLiteral("account")) {
        return shellOpen(L"ms-settings:yourinfo");
    }

    KnownFolderPath path;
    const KNOWNFOLDERID& folder =
        actionId == QStringLiteral("documents") ? FOLDERID_Documents : FOLDERID_Downloads;
    const HRESULT result = SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &path.value);
    if (FAILED(result) || !path.value) {
        return {false, QStringLiteral("无法读取系统快捷目录。"),
                nativeErrorCode(QStringLiteral("known-folder"), result)};
    }
    return shellOpen(path.value);
}

DesktopAnchorResult WindowsDesktopPlatform::queryMenuAnchor() {
    PhysicalCoordinates coordinates;
    DesktopAnchorResult result;
    result.dpi = 0;
    if (!coordinates.ready()) {
        result.message = QStringLiteral("无法建立物理像素坐标上下文，保留当前窗口位置。");
        result.errorCode = QStringLiteral("physical-coordinate-context-unavailable");
        return result;
    }
    POINT cursor{};
    GetPhysicalCursorPos(&cursor);
    const HMONITOR preferredMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    setMonitor(result, preferredMonitor);
    if (!result.workAreaPhysicalRect.isValid()) {
        result.message = QStringLiteral("无法读取目标屏幕工作区，保留当前窗口位置。");
        result.errorCode = QStringLiteral("invalid-work-area");
        return result;
    }
    QVector<HWND> taskbars;
    EnumWindows(collectTaskbars, reinterpret_cast<LPARAM>(&taskbars));
    for (HWND taskbar : taskbars) {
        if (MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL) == preferredMonitor) {
            result.dpi = GetDpiForWindow(taskbar);
            break;
        }
    }
    result.message = QStringLiteral("已读取目标屏幕工作区；菜单按左侧与底部等距定位。");
    return result;
}

DesktopAnchorResult WindowsDesktopPlatform::queryStartButtonAnchor() {
    PhysicalCoordinates coordinates;
    DesktopAnchorResult result;
    result.dpi = 0;
    if (!coordinates.ready()) {
        result.message = QStringLiteral("无法建立物理像素坐标上下文，保留普通窗口位置。");
        result.errorCode = QStringLiteral("physical-coordinate-context-unavailable");
        return result;
    }
    POINT cursor{};
    GetPhysicalCursorPos(&cursor);
    const HMONITOR preferredMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    setMonitor(result, preferredMonitor);
    result.message = QStringLiteral("未可信识别开始按钮，使用目标屏幕工作区回退位置。");
    result.errorCode = QStringLiteral("start-button-not-identified");

    QVector<HWND> taskbars;
    EnumWindows(collectTaskbars, reinterpret_cast<LPARAM>(&taskbars));
    // Read scale independently of UIA/COM success. An unavailable StartButton
    // must not change an 8-DIP margin from, for example, 12 pixels back to 8.
    for (HWND taskbar : taskbars) {
        if (MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL) == preferredMonitor) {
            const UINT taskbarDpi = GetDpiForWindow(taskbar);
            if (taskbarDpi > 0) {
                result.dpi = taskbarDpi;
            }
            break;
        }
    }

    ComApartment apartment;
    if (!apartment.ready()) {
        result.errorCode = nativeErrorCode(QStringLiteral("com-initialize"), apartment.result());
        return result;
    }
    ComPtr<IUIAutomation> automation;
    const HRESULT created = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(automation.put()));
    if (FAILED(created)) {
        result.errorCode = nativeErrorCode(QStringLiteral("uia-initialize"), created);
        return result;
    }
    ComPtr<IUIAutomation2> automation2;
    if (SUCCEEDED(automation->QueryInterface(IID_PPV_ARGS(automation2.put())))) {
        automation2->put_ConnectionTimeout(1500);
        automation2->put_TransactionTimeout(1500);
    }
    // Select a real taskbar on the pointer's monitor. A secondary monitor
    // without a visible taskbar gets its own work area. StartButton trust is
    // diagnostic only; placement always uses equal work-area margins.
    for (HWND taskbar : taskbars) {
        if (MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL) != preferredMonitor) {
            continue;
        }
        const UINT taskbarDpi = GetDpiForWindow(taskbar);
        if (taskbarDpi > 0) {
            result.dpi = taskbarDpi;
        }
        QRect button;
        if (findStartButton(automation.get(), taskbar, button, result.errorCode)) {
            setMonitor(result, preferredMonitor);
            const UINT dpi = GetDpiForWindow(taskbar);
            if (dpi == 0 || !result.monitorPhysicalRect.contains(button) ||
                !result.workAreaPhysicalRect.isValid()) {
                result.errorCode = QStringLiteral("invalid-taskbar-monitor");
                return result;
            }
            result.trusted = true;
            result.startButtonPhysicalRect = button;
            result.dpi = dpi;
            result.errorCode.clear();
            result.message =
                QStringLiteral("已通过任务栏 UI Automation 标识与按钮类型定位开始按钮。");
            return result;
        }
    }
    return result;
}

DesktopAppearanceResult WindowsDesktopPlatform::configureMenuWindow(quintptr nativeHandle) {
    const HWND window = reinterpret_cast<HWND>(nativeHandle);
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (!window || !IsWindow(window) || process != GetCurrentProcessId() ||
        thread != GetCurrentThreadId() || GetAncestor(window, GA_ROOT) != window) {
        return {false,
                false,
                QStringLiteral("无法配置当前程序的菜单窗口外观。"),
                QStringLiteral("invalid-owned-window")};
    }
    BOOL composition = FALSE;
    const HRESULT compositionResult = DwmIsCompositionEnabled(&composition);
    if (FAILED(compositionResult) || !composition) {
        return {false,
                false,
                QStringLiteral("系统合成不可用，菜单继续使用自绘外观。"),
                FAILED(compositionResult)
                    ? nativeErrorCode(QStringLiteral("dwm-composition"), compositionResult)
                    : QStringLiteral("dwm-composition-disabled")};
    }

    // SDK 10.0.19041 predates the Windows 11 enum names. These are the public
    // DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_DONOTROUND ABI values, supported from
    // build 22000. Older systems return a structured fallback instead of
    // requiring a newer project SDK or a guessed OS version check.
    constexpr DWORD cornerAttribute = 33;
    constexpr DWORD roundPreference = 1;
    const HRESULT cornerResult = DwmSetWindowAttribute(
        window, cornerAttribute, &roundPreference, sizeof(roundPreference));

    // The transparent Qt Quick surface owns all visible decoration. Disable
    // the compositor's non-client surface and remove any previous frame
    // extension rather than stacking a second radius/shadow on the QML panel.
    // No Qt style is changed, including WS_EX_LAYERED for per-pixel alpha.
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
    const HRESULT policyResult =
        DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    const MARGINS margins{0, 0, 0, 0};
    const HRESULT frameResult = DwmExtendFrameIntoClientArea(window, &margins);
    // DWMWA_BORDER_COLOR / DWMWA_COLOR_NONE are public Windows 11 values; the
    // pinned 19041 SDK does not yet expose their enum names.
    constexpr DWORD borderAttribute = 34;
    constexpr DWORD noBorderColor = 0xfffffffe;
    const HRESULT borderResult =
        DwmSetWindowAttribute(window, borderAttribute, &noBorderColor, sizeof(noBorderColor));

    DesktopAppearanceResult result;
    result.nativeDecorationsDisabled = SUCCEEDED(cornerResult) && SUCCEEDED(policyResult) &&
                                       SUCCEEDED(frameResult) && SUCCEEDED(borderResult);
    result.success = result.nativeDecorationsDisabled;
    if (result.success) {
        result.message = QStringLiteral("已禁用 Windows 原生边框、圆角和阴影；菜单使用自绘外观。");
    } else {
        result.message = QStringLiteral("系统未接受完整原生装饰禁用请求；菜单继续使用自绘外观。");
        const HRESULT failure = FAILED(cornerResult) ? cornerResult
                                : FAILED(policyResult) ? policyResult
                                : FAILED(frameResult) ? frameResult
                                                      : borderResult;
        result.errorCode = nativeErrorCode(QStringLiteral("dwm-menu-appearance"), failure);
    }
    return result;
}

DesktopPlacementResult WindowsDesktopPlatform::placeWindow(quintptr nativeHandle,
                                                           const DesktopAnchorResult& anchor,
                                                           int gapPhysicalPixels, bool activate,
                                                           int shadowInsetPhysicalPixels) {
    PhysicalCoordinates coordinates;
    if (!coordinates.ready()) {
        return {false,
                {},
                QStringLiteral("无法建立物理像素坐标上下文，保留普通窗口位置。"),
                QStringLiteral("physical-coordinate-context-unavailable")};
    }
    const HWND window = reinterpret_cast<HWND>(nativeHandle);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    RECT current{};
    if (!window || !IsWindow(window) || process != GetCurrentProcessId() ||
        GetAncestor(window, GA_ROOT) != window || !GetWindowRect(window, &current)) {
        return {false,
                {},
                QStringLiteral("无法定位当前程序的菜单窗口。"),
                QStringLiteral("invalid-owned-window")};
    }
    const QRect outer = rectangle(current);
    const qint64 inset = shadowInsetPhysicalPixels;
    if (!outer.isValid() || inset < 0 || 2 * inset >= outer.width() ||
        2 * inset >= outer.height()) {
        return {false,
                {},
                QStringLiteral("菜单阴影留白与窗口尺寸不匹配。"),
                QStringLiteral("invalid-shadow-inset")};
    }
    // GetWindowRect describes the complete transparent Qt surface. DWM's
    // extended-frame bounds do not describe the QML panel and can change
    // during show/hide, so they must not participate in this geometry.
    const QSize panelSize(static_cast<int>(outer.width() - 2 * inset),
                          static_cast<int>(outer.height() - 2 * inset));
    const QRect placement = desktopPopupPhysicalRect(panelSize, anchor, gapPhysicalPixels);
    if (!placement.isValid()) {
        return {false,
                {},
                QStringLiteral("无法读取可用的屏幕工作区。"),
                QStringLiteral("invalid-work-area")};
    }
    const QRect nativePlacement =
        checkedRectangle(static_cast<qint64>(placement.left()) - inset,
                         static_cast<qint64>(placement.top()) - inset,
                         static_cast<qint64>(placement.right()) + inset + 1,
                         static_cast<qint64>(placement.bottom()) + inset + 1);
    if (!nativePlacement.isValid()) {
        return {false,
                {},
                QStringLiteral("菜单窗口坐标超出可表示范围。"),
                QStringLiteral("window-coordinate-overflow")};
    }
    const UINT flags =
        SWP_NOOWNERZORDER | (activate ? SWP_SHOWWINDOW : SWP_NOACTIVATE | SWP_NOZORDER);
    if (!SetWindowPos(window, HWND_TOP, nativePlacement.x(), nativePlacement.y(),
                      nativePlacement.width(), nativePlacement.height(), flags)) {
        return {
            false,
            {},
            QStringLiteral("菜单窗口定位失败。"),
            nativeErrorCode(QStringLiteral("window-position"), HRESULT_FROM_WIN32(GetLastError()))};
    }
    if (activate) {
        SetForegroundWindow(window);
    }
    return {true, placement, QStringLiteral("菜单主体按工作区左侧与底部等距定位。"), {}};
}

} // namespace xdstarter
