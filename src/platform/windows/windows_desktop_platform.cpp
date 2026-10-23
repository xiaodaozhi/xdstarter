#include "windows_desktop_platform.h"

#include <QDir>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QThread>
#include <QVector>
#include <QWindow>
#include <QtGui/qscreen_platform.h>

#include <algorithm>
#include <iterator>
#include <limits>

#include <windows.h>

#include <commctrl.h>
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
    explicit ComApartment(DWORD model = COINIT_APARTMENTTHREADED)
        : result_(CoInitializeEx(nullptr, model)) {}
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

struct TaskbarQuery {
    QVector<HWND> windows;
    bool visibleOnly = true;
};

BOOL CALLBACK collectTaskbars(HWND window, LPARAM parameter) {
    try {
        auto& query = *reinterpret_cast<TaskbarQuery*>(parameter);
        if (query.windows.size() >= 32) {
            return FALSE;
        }
        wchar_t className[128]{};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
            (wcscmp(className, L"Shell_TrayWnd") == 0 ||
             wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) &&
            (!query.visibleOnly || IsWindowVisible(window)) && isExplorerWindow(window)) {
            query.windows.append(window);
        }
    } catch (...) {
        return FALSE;
    }
    return TRUE;
}

struct StartButtonDetails {
    HWND nativeWindow = nullptr;
    int providerProcess = 0;
    bool nativeHandleRead = false;
    QString nativeHandleError;
};

bool findStartButton(IUIAutomation* automation, HWND taskbar, QRect& button, QString& error,
                     StartButtonDetails* details = nullptr,
                     IUIAutomationElement** identified = nullptr) {
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
    if (identified) {
        *identified = element.get();
        (*identified)->AddRef();
    }
    if (details) {
        UIA_HWND nativeWindow = nullptr;
        const HRESULT nativeResult = element->get_CurrentNativeWindowHandle(&nativeWindow);
        details->providerProcess = providerProcess;
        details->nativeHandleRead = SUCCEEDED(nativeResult);
        if (SUCCEEDED(nativeResult)) {
            details->nativeWindow = reinterpret_cast<HWND>(nativeWindow);
        } else {
            details->nativeHandleError =
                nativeErrorCode(QStringLiteral("uia-native-window"), nativeResult);
        }
    }
    return true;
}

DesktopNativeWindowObservation observeWindow(HWND window) {
    DesktopNativeWindowObservation result;
    result.handle = reinterpret_cast<quintptr>(window);
    if (!window || !IsWindow(window)) {
        return result;
    }
    result.exists = true;
    result.visible = IsWindowVisible(window) != FALSE;
    result.rootHandle = reinterpret_cast<quintptr>(GetAncestor(window, GA_ROOT));
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    result.processId = process;
    wchar_t className[128]{};
    const int length = GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (length > 0) {
        result.className = QString::fromWCharArray(className, length);
    }
    RECT bounds{};
    if (GetWindowRect(window, &bounds)) {
        result.physicalRect = rectangle(bounds);
    }
    return result;
}

bool sameWindowObservation(const DesktopNativeWindowObservation& before,
                           const DesktopNativeWindowObservation& after) {
    return before.handle == after.handle && before.rootHandle == after.rootHandle &&
           before.processId == after.processId && before.className == after.className &&
           before.physicalRect == after.physicalRect && before.exists == after.exists &&
           before.visible == after.visible;
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

// The native sizing loop must preserve the menu's fixed outer edges, including
// transparent shadow space which Windows would otherwise snap inside rcWork.
class WindowsMenuResizeFilter final {
  public:
    explicit WindowsMenuResizeFilter(QWindow* window)
        : window_(window), native_(reinterpret_cast<HWND>(window->winId())) {
        installed_ = SetWindowSubclass(native_, windowProcedure, reinterpret_cast<UINT_PTR>(this),
                                       reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    }
    ~WindowsMenuResizeFilter() {
        if (installed_ && native_)
            RemoveWindowSubclass(native_, windowProcedure, reinterpret_cast<UINT_PTR>(this));
    }
    bool matches(QWindow* window) const { return installed_ && window_ == window; }
    bool isInstalled() const { return installed_; }

  private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<WindowsMenuResizeFilter*>(data);
        if (self && id == reinterpret_cast<UINT_PTR>(self)) {
            MSG event{};
            event.hwnd = window;
            event.message = message;
            event.wParam = wparam;
            event.lParam = lparam;
            LRESULT result = 0;
            if (self->filterMessage(event, &result))
                return result;
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }
    bool filterMessage(const MSG& message, LRESULT* result) {
        if (!window_ || !native_ || message.hwnd != native_)
            return false;
        if (message.message == WM_EXITSIZEMOVE || message.message == WM_NCDESTROY) {
            sizing_ = false;
            prepared_ = false;
            initial_ = {};
            lastSized_ = {};
            if (message.message == WM_NCDESTROY) {
                RemoveWindowSubclass(native_, windowProcedure, reinterpret_cast<UINT_PTR>(this));
                installed_ = false;
                native_ = nullptr;
                window_.clear();
            }
            return false;
        }
        if (prepared_ && !sizing_ && GetTickCount64() - preparedAt_ > 2000)
            prepared_ = false;
        const bool sizingCommand = message.message == WM_SYSCOMMAND &&
                                   (message.wParam & 0xfff0) == SC_SIZE &&
                                   (message.wParam & 0xf) >= 1 && (message.wParam & 0xf) <= 8;
        if (!sizingCommand && message.message != WM_ENTERSIZEMOVE && message.message != WM_SIZING &&
            message.message != WM_WINDOWPOSCHANGING)
            return false;
        PhysicalCoordinates coordinates;
        if (!coordinates.ready())
            return false;
        if (sizingCommand || (message.message == WM_ENTERSIZEMOVE && !prepared_)) {
            RECT bounds{};
            const bool read = GetWindowRect(message.hwnd, &bounds) != FALSE;
            initial_ = read ? rectangle(bounds) : QRect{};
            lastSized_ = initial_;
            prepared_ = sizingCommand && read;
            preparedAt_ = GetTickCount64();
            sizing_ = !sizingCommand && read;
        } else if (message.message == WM_ENTERSIZEMOVE) {
            sizing_ = initial_.isValid();
        } else if (message.message == WM_WINDOWPOSCHANGING && (prepared_ || sizing_) &&
                   lastSized_.isValid() && message.lParam) {
            auto& position = *reinterpret_cast<WINDOWPOS*>(message.lParam);
            if (!(position.flags & SWP_NOMOVE) || !(position.flags & SWP_NOSIZE)) {
                // SC_SIZE may clamp shadow space before WM_ENTERSIZEMOVE, or
                // normalize it after WM_SIZING. Preserve the last sizing rect
                // throughout this one native loop, including those phases.
                position.x = lastSized_.x();
                position.y = lastSized_.y();
                position.cx = lastSized_.width();
                position.cy = lastSized_.height();
                position.flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
                *result = 0;
                return true;
            }
        } else if (message.message == WM_SIZING && sizing_ && message.lParam) {
            Qt::Edges edges;
            switch (message.wParam) {
            case WMSZ_LEFT:
                edges = Qt::LeftEdge;
                break;
            case WMSZ_RIGHT:
                edges = Qt::RightEdge;
                break;
            case WMSZ_TOP:
                edges = Qt::TopEdge;
                break;
            case WMSZ_BOTTOM:
                edges = Qt::BottomEdge;
                break;
            case WMSZ_TOPLEFT:
                edges = Qt::TopEdge | Qt::LeftEdge;
                break;
            case WMSZ_TOPRIGHT:
                edges = Qt::TopEdge | Qt::RightEdge;
                break;
            case WMSZ_BOTTOMLEFT:
                edges = Qt::BottomEdge | Qt::LeftEdge;
                break;
            case WMSZ_BOTTOMRIGHT:
                edges = Qt::BottomEdge | Qt::RightEdge;
                break;
            default:
                return false;
            }
            const qreal scale = GetDpiForWindow(message.hwnd) / 96.0;
            const QSize minimum(qMax(1, qRound(window_->minimumWidth() * scale)),
                                qMax(1, qRound(window_->minimumHeight() * scale)));
            auto& proposed = *reinterpret_cast<RECT*>(message.lParam);
            const QRect fixed =
                desktopResizePhysicalRect(initial_, rectangle(proposed), edges, minimum);
            if (fixed.isValid() && fixed.right() < std::numeric_limits<LONG>::max() &&
                fixed.bottom() < std::numeric_limits<LONG>::max()) {
                proposed = {fixed.left(), fixed.top(), fixed.right() + 1, fixed.bottom() + 1};
                lastSized_ = fixed;
                *result = TRUE;
                return true;
            }
        }
        return false;
    }

  private:
    QPointer<QWindow> window_;
    HWND native_ = nullptr;
    QRect initial_;
    QRect lastSized_;
    bool sizing_ = false;
    bool prepared_ = false;
    ULONGLONG preparedAt_ = 0;
    bool installed_ = false;
};

WindowsDesktopPlatform::WindowsDesktopPlatform() = default;
WindowsDesktopPlatform::~WindowsDesktopPlatform() = default;

StartButtonCaptureTarget WindowsDesktopPlatform::queryStartButtonCaptureTarget() {
    StartButtonCaptureTarget result;
    PhysicalCoordinates coordinates;
    POINT point{};
    if (!coordinates.ready() || !GetPhysicalCursorPos(&point))
        return result;
    result.observedAt = GetTickCount64();
    result.qualifiedPoint = {point.x, point.y};
    const HWND hit = WindowFromPoint(point);
    const HWND taskbar = GetAncestor(hit, GA_ROOT);
    const auto before = observeWindow(taskbar);
    if (!before.visible || !before.physicalRect.contains(result.qualifiedPoint) ||
        (before.className != QStringLiteral("Shell_TrayWnd") &&
         before.className != QStringLiteral("Shell_SecondaryTrayWnd")) ||
        !isExplorerWindow(taskbar))
        return result;
    ComApartment apartment(COINIT_MULTITHREADED);
    ComPtr<IUIAutomation> automation;
    if (!apartment.ready() ||
        FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(automation.put()))))
        return result;
    ComPtr<IUIAutomation2> bounded;
    if (FAILED(automation->QueryInterface(IID_PPV_ARGS(bounded.put()))) ||
        FAILED(bounded->put_ConnectionTimeout(100)) || FAILED(bounded->put_TransactionTimeout(100)))
        return result;
    QRect button;
    QString error;
    ComPtr<IUIAutomationElement> expected;
    if (!findStartButton(automation.get(), taskbar, button, error, nullptr, expected.put()) ||
        !button.contains(result.qualifiedPoint))
        return result;
    ComPtr<IUIAutomationElement> atPoint;
    if (FAILED(automation->ElementFromPoint(point, atPoint.put())))
        return result;
    BOOL same = FALSE;
    // A provider may expose a text child at the point. Normalize to the known
    // button using at most eight raw parents, never by localized display name.
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation->get_RawViewWalker(walker.put())))
        return result;
    IUIAutomationElement* current = atPoint.get();
    current->AddRef();
    for (int depth = 0; current && depth < 8; ++depth) {
        const HRESULT compared = automation->CompareElements(expected.get(), current, &same);
        IUIAutomationElement* parent = nullptr;
        if (SUCCEEDED(compared) && !same)
            walker->GetParentElement(current, &parent);
        current->Release();
        current = parent;
        if (FAILED(compared) || same)
            break;
    }
    if (current)
        current->Release();
    RECT finalButton{};
    BOOL offscreen = TRUE;
    POINT afterPoint{};
    if (!same || FAILED(expected->get_CurrentBoundingRectangle(&finalButton)) ||
        rectangle(finalButton) != button || FAILED(expected->get_CurrentIsOffscreen(&offscreen)) ||
        offscreen || !sameWindowObservation(before, observeWindow(taskbar)) ||
        !GetPhysicalCursorPos(&afterPoint) || point.x != afterPoint.x || point.y != afterPoint.y ||
        WindowFromPoint(point) != hit || GetTickCount64() - result.observedAt > 200)
        return result;
    result.taskbar = before.handle;
    result.hitWindow = reinterpret_cast<quintptr>(hit);
    result.processId = before.processId;
    result.taskbarRect = before.physicalRect;
    result.buttonRect = button;
    const HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return {};
    result.monitor = reinterpret_cast<quintptr>(monitor);
    result.monitorRect = rectangle(info.rcMonitor);
    result.dpi = GetDpiForWindow(taskbar);
    result.identified = true;
    return result;
}

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
    TaskbarQuery taskbars;
    taskbars.visibleOnly = false; // Placement metadata is never input permission.
    EnumWindows(collectTaskbars, reinterpret_cast<LPARAM>(&taskbars));
    std::stable_partition(taskbars.windows.begin(), taskbars.windows.end(),
                          [](HWND window) { return IsWindowVisible(window) != FALSE; });
    for (HWND taskbar : taskbars.windows) {
        if (MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL) == preferredMonitor) {
            result.dpi = GetDpiForWindow(taskbar);
            RECT bounds{};
            if (GetWindowRect(taskbar, &bounds))
                result.taskbarPhysicalRect = rectangle(bounds);
            break;
        }
    }
    // A read-only Shell preference. Missing on Windows 10 means left aligned.
    // Never infer alignment from a potentially stale UIA hit-test rectangle.
    const QSettings explorer(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"),
        QSettings::NativeFormat);
    result.centeredStart = explorer.value(QStringLiteral("TaskbarAl"), 0).toInt() == 1;
    result.message = QStringLiteral("已按任务栏边缘、对齐与目标工作区定位。");
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

    TaskbarQuery taskbars;
    EnumWindows(collectTaskbars, reinterpret_cast<LPARAM>(&taskbars));
    // Read scale independently of UIA/COM success. An unavailable StartButton
    // must not change an 8-DIP margin from, for example, 12 pixels back to 8.
    for (HWND taskbar : taskbars.windows) {
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
    for (HWND taskbar : taskbars.windows) {
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

DesktopStartButtonHitResult WindowsDesktopPlatform::queryStartButtonHitTarget() {
    DesktopStartButtonHitResult result;
    result.anchor.dpi = 0;
    result.classification = DesktopStartButtonHitClassification::Unidentified;
    const auto finish = [&result]() {
        result.classification = classifyStartButtonHitTarget(result);
        const QString classification = startButtonHitClassificationName(result.classification);
        if (result.errorCode.isEmpty() &&
            result.classification != DesktopStartButtonHitClassification::IndependentCandidate) {
            result.errorCode = QStringLiteral("start-hit-") + classification;
        }
        result.message =
            result.classification == DesktopStartButtonHitClassification::IndependentCandidate
                ? QStringLiteral("本次只读采样发现独立原生命中候选；不代表可持续接管或允许消费输入。")
                : QStringLiteral("只读命中研究未取得独立候选（%1）；系统开始按钮保持原生行为。")
                      .arg(classification);
        return result;
    };
    PhysicalCoordinates coordinates;
    if (!coordinates.ready()) {
        result.errorCode = QStringLiteral("physical-coordinate-context-unavailable");
        return finish();
    }
    POINT cursor{};
    if (!GetPhysicalCursorPos(&cursor)) {
        result.errorCode = QStringLiteral("physical-cursor-unavailable");
        return finish();
    }
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    setMonitor(result.anchor, monitor);
    if (!result.anchor.monitorPhysicalRect.isValid() ||
        !result.anchor.workAreaPhysicalRect.isValid()) {
        result.errorCode = QStringLiteral("invalid-monitor-area");
        return finish();
    }
    TaskbarQuery taskbars;
    EnumWindows(collectTaskbars, reinterpret_cast<LPARAM>(&taskbars));
    HWND taskbar = nullptr;
    for (HWND candidate : taskbars.windows) {
        if (MonitorFromWindow(candidate, MONITOR_DEFAULTTONULL) == monitor) {
            if (taskbar) {
                result.errorCode = QStringLiteral("ambiguous-monitor-taskbar");
                return finish();
            }
            taskbar = candidate;
        }
    }
    if (!taskbar) {
        result.errorCode = QStringLiteral("visible-explorer-taskbar-unavailable");
        return finish();
    }
    result.taskbar = observeWindow(taskbar);
    result.taskbarVerifiedExplorer = isExplorerWindow(taskbar);
    result.anchor.dpi = GetDpiForWindow(taskbar);
    // This explicit research operation owns its apartment. No UIA pointers or
    // calls escape to normal product placement, the GUI, or an input callback.
    ComApartment apartment(COINIT_MULTITHREADED);
    if (!apartment.ready()) {
        result.errorCode = nativeErrorCode(QStringLiteral("com-initialize"), apartment.result());
        return finish();
    }
    ComPtr<IUIAutomation> automation;
    const HRESULT created = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(automation.put()));
    if (FAILED(created)) {
        result.errorCode = nativeErrorCode(QStringLiteral("uia-initialize"), created);
        return finish();
    }
    ComPtr<IUIAutomation2> automation2;
    if (SUCCEEDED(automation->QueryInterface(IID_PPV_ARGS(automation2.put())))) {
        automation2->put_ConnectionTimeout(1500);
        automation2->put_TransactionTimeout(1500);
    }
    StartButtonDetails details;
    if (!findStartButton(automation.get(), taskbar, result.anchor.startButtonPhysicalRect,
                          result.errorCode, &details)) {
        return finish();
    }
    result.anchor.trusted = true;
    result.nativeHandleRead = details.nativeHandleRead;
    result.uiaProviderProcessId = static_cast<quint32>(details.providerProcess);
    result.startWindow = observeWindow(details.nativeWindow);
    result.startWindowIsTaskbarChild =
        details.nativeWindow && details.nativeWindow != taskbar &&
        IsChild(taskbar, details.nativeWindow) != FALSE;
    if (!details.nativeHandleRead) {
        result.errorCode = details.nativeHandleError;
    }
    const QRect button = result.anchor.startButtonPhysicalRect;
    if (button.width() >= 5 && button.height() >= 5) {
        // Fixed five points, one physical pixel inside each corner. They are
        // diagnostic samples, not a claim that the whole rectangle is clickable.
        const std::array<QPoint, 5> points{
            button.center(), QPoint(button.left() + 1, button.top() + 1),
            QPoint(button.right() - 1, button.top() + 1),
            QPoint(button.left() + 1, button.bottom() - 1),
            QPoint(button.right() - 1, button.bottom() - 1)};
        for (const QPoint& point : points) {
            const HWND hit = WindowFromPoint(POINT{point.x(), point.y()});
            auto& sample = result.samples[result.sampleCount++];
            sample.physicalPoint = point;
            sample.window = observeWindow(hit);
            sample.withinStartWindow = details.nativeWindow && hit &&
                                       (hit == details.nativeWindow ||
                                        IsChild(details.nativeWindow, hit) != FALSE);
            sample.withinTaskbar = hit && (hit == taskbar || IsChild(taskbar, hit) != FALSE);
        }
    }
    // Re-read after sampling to reject observed movement/replacement. This does
    // not make the observations atomic or protect a later click from changes.
    QRect secondButton;
    QString secondError;
    StartButtonDetails secondDetails;
    DesktopAnchorResult secondMonitor;
    setMonitor(secondMonitor, monitor);
    result.observationsStable =
        findStartButton(automation.get(), taskbar, secondButton, secondError, &secondDetails) &&
        secondDetails.nativeHandleRead && details.nativeHandleRead &&
        secondDetails.nativeWindow == details.nativeWindow &&
        secondDetails.providerProcess == details.providerProcess && secondButton == button &&
        sameWindowObservation(result.taskbar, observeWindow(taskbar)) &&
        sameWindowObservation(result.startWindow, observeWindow(details.nativeWindow)) &&
        isExplorerWindow(taskbar) && GetDpiForWindow(taskbar) == result.anchor.dpi &&
        MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL) == monitor &&
        secondMonitor.monitorPhysicalRect == result.anchor.monitorPhysicalRect &&
        secondMonitor.workAreaPhysicalRect == result.anchor.workAreaPhysicalRect;
    return finish();
}

DesktopPresentationResult WindowsDesktopPlatform::presentMenuWindow(quintptr nativeHandle) {
    DesktopPresentationResult result;
    result.supported = true;
    const HWND window = reinterpret_cast<HWND>(nativeHandle);
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (!window || !IsWindow(window) || process != GetCurrentProcessId() ||
        thread != GetCurrentThreadId() || GetAncestor(window, GA_ROOT) != window) {
        result.errorCode = QStringLiteral("invalid-owned-window");
        return result;
    }
    // Qt's first showNormal can inherit STARTUPINFO's SW_HIDE/SW_SHOWMINIMIZED.
    // The explicit user reveal restores the actual HWND, without moving it or
    // changing its flags, taskbar settings, other windows or global lock policy.
    if (IsIconic(window) || !IsWindowVisible(window))
        ShowWindow(window, SW_RESTORE);
    if (!SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER)) {
        result.errorCode = QStringLiteral("menu-native-show-failed");
    }
    SetForegroundWindow(window);
    result.visible = IsWindowVisible(window) != FALSE;
    if (GetForegroundWindow() != window) {
        const HWND foreground = GetForegroundWindow();
        const DWORD foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
        DWORD_PTR response = 0;
        // Follow Qt's explicit activation approach for this user-requested
        // reveal. No global focus-policy changes or synthesized Alt/Win keys.
        if (foreground && foregroundThread && foregroundThread != thread &&
            SendMessageTimeoutW(foreground, WM_NULL, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, 50,
                                &response) &&
            GetForegroundWindow() == foreground &&
            AttachThreadInput(thread, foregroundThread, TRUE)) {
            BringWindowToTop(window);
            SetActiveWindow(window);
            SetFocus(window);
            SetForegroundWindow(window);
            AttachThreadInput(thread, foregroundThread, FALSE);
        }
    }
    result.minimized = IsIconic(window) != FALSE;
    result.foreground = GetForegroundWindow() == window;
    if (result.errorCode.isEmpty() && (!result.visible || result.minimized))
        result.errorCode = QStringLiteral("menu-native-show-state-invalid");
    else if (result.errorCode.isEmpty() && !result.foreground)
        result.errorCode = QStringLiteral("menu-foreground-denied");
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
    for (auto* candidate : QGuiApplication::allWindows()) {
        if (candidate->handle() && candidate->winId() == nativeHandle) {
            if (!resizeFilter_ || !resizeFilter_->matches(candidate))
                resizeFilter_ = std::make_unique<WindowsMenuResizeFilter>(candidate);
            if (!resizeFilter_->isInstalled())
                return {false, false, QStringLiteral("菜单原生缩放约束安装失败。"),
                        QStringLiteral("menu-resize-subclass-failed")};
            break;
        }
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

QScreen *WindowsDesktopPlatform::screenForAnchor(const DesktopAnchorResult &anchor) {
    const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application || QThread::currentThread() != application->thread() ||
        anchor.monitorName.isEmpty())
        return nullptr;
    PhysicalCoordinates coordinates;
    if (!coordinates.ready())
        return nullptr;
    QScreen *match = nullptr;
    for (auto *screen : QGuiApplication::screens()) {
        // QScreen::name() may be an EDID model name, not \\.\DISPLAYn.
        // Qt's public Windows native interface supplies the actual HMONITOR.
        const auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!native || !GetMonitorInfoW(native->handle(), reinterpret_cast<MONITORINFO *>(&info)) ||
            QString::fromWCharArray(info.szDevice) != anchor.monitorName ||
            (anchor.monitorPhysicalRect.isValid() &&
             rectangle(info.rcMonitor) != anchor.monitorPhysicalRect))
            continue;
        if (match)
            return nullptr;
        match = screen;
    }
    return match;
}

DesktopWindowMetricsResult WindowsDesktopPlatform::queryWindowMetrics(quintptr nativeHandle) {
    PhysicalCoordinates coordinates;
    const HWND window = reinterpret_cast<HWND>(nativeHandle);
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    RECT current{};
    if (!coordinates.ready() || !window || !IsWindow(window) ||
        process != GetCurrentProcessId() || thread != GetCurrentThreadId() ||
        GetAncestor(window, GA_ROOT) != window || !GetWindowRect(window, &current)) {
        return {true, false, {}};
    }
    const QRect bounds = rectangle(current);
    return {true, bounds.isValid(), bounds.size()};
}

bool WindowsDesktopPlatform::hasOwnedForegroundWindow(quintptr menuHandle) {
    const HWND menu = reinterpret_cast<HWND>(menuHandle);
    DWORD menuProcess = 0;
    const DWORD menuThread = GetWindowThreadProcessId(menu, &menuProcess);
    if (!menu || !IsWindow(menu) || !IsWindowVisible(menu) ||
        menuProcess != GetCurrentProcessId() || menuThread != GetCurrentThreadId() ||
        GetAncestor(menu, GA_ROOT) != menu)
        return false;

    // Qt's native IFileDialog runs on another thread and has no QWindow. Read
    // the real foreground rather than this thread's GetFocus(); only a precise
    // owner chain to our menu counts, never another same-process top-level.
    const HWND foreground = GetForegroundWindow();
    // IFileDialog can own foreground before its HWND becomes visible. Keep the
    // exact current-foreground and owner checks during this initialization gap;
    // visibility is required for our menu, not for the OS-owned foreground.
    if (!foreground || !IsWindow(foreground))
        return false;
    HWND candidate = GetAncestor(foreground, GA_ROOT);
    for (int depth = 0; candidate && depth < 16; ++depth) {
        DWORD process = 0;
        if (!IsWindow(candidate) || !GetWindowThreadProcessId(candidate, &process) ||
            process != GetCurrentProcessId())
            return false;
        if (candidate == menu)
            return GetForegroundWindow() == foreground;
        const HWND owner = GetWindow(candidate, GW_OWNER);
        if (owner == candidate)
            return false;
        candidate = owner;
    }
    return false;
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
    return {true, placement, QStringLiteral("菜单已按任务栏方向、对齐与工作区定位。"), {}};
}

} // namespace xdstarter
