#include "windows_desktop_platform.h"
#include "native_picker_focus.h"

#include "platform/desktop_width_snap.h"

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
    void reset() {
        if (pointer_)
            pointer_->Release();
        pointer_ = nullptr;
    }

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
                     IUIAutomationElement** identified = nullptr,
                     IUIAutomationCacheRequest* properties = nullptr) {
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
        result = properties ? root->FindAllBuildCache(TreeScope_Descendants, condition.get(),
                                                     properties, elements.put())
                            : root->FindAll(TreeScope_Descendants, condition.get(), elements.put());
    }
    int count = 0;
    if (FAILED(result)) {
        error = nativeErrorCode(QStringLiteral("uia-start-query"), result);
        return false;
    }
    if (elements.get())
        result = elements->get_Length(&count);
    if (FAILED(result)) {
        error = nativeErrorCode(QStringLiteral("uia-start-count"), result);
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
        FAILED(properties ? element->get_CachedBoundingRectangle(&bounds)
                          : element->get_CurrentBoundingRectangle(&bounds)) ||
        FAILED(properties ? element->get_CachedIsOffscreen(&offscreen)
                          : element->get_CurrentIsOffscreen(&offscreen)) || offscreen ||
        FAILED(properties ? element->get_CachedProcessId(&providerProcess)
                          : element->get_CurrentProcessId(&providerProcess)) ||
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

bool applyMenuInputRegion(HWND window, QRect outer, QRect taskbar) {
    const QRegion desired = desktopMenuInputRegion(outer, taskbar);
    if (!outer.isValid())
        return false;
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region)
        return false;
    for (const auto& part : desired) {
        HRGN piece = CreateRectRgn(part.left(), part.top(), part.right() + 1, part.bottom() + 1);
        if (!piece || CombineRgn(region, region, piece, RGN_OR) == ERROR) {
            if (piece)
                DeleteObject(piece);
            DeleteObject(region);
            return false;
        }
        DeleteObject(piece);
    }
    HRGN existing = CreateRectRgn(0, 0, 0, 0);
    const bool unchanged =
        existing && GetWindowRgn(window, existing) != ERROR && EqualRgn(existing, region);
    if (existing)
        DeleteObject(existing);
    if (unchanged) {
        DeleteObject(region);
        return true;
    }
    if (!SetWindowRgn(window, region, IsWindowVisible(window))) {
        DeleteObject(region);
        return false;
    }
    return true; // SetWindowRgn owns region only after success.
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
    bool matchesHandle(HWND window) const { return installed_ && native_ == window; }
    bool isInstalled() const { return installed_; }
    bool setTaskbarRegion(QRect taskbar) {
        taskbar_ = taskbar;
        taskbarRegionKnown_ = true;
        return refreshInputRegion();
    }

  private:
    bool refreshInputRegion() {
        if (!installed_ || !native_ || !taskbarRegionKnown_ || applyingRegion_)
            return true;
        PhysicalCoordinates coordinates;
        RECT outer{};
        if (!coordinates.ready() || !GetWindowRect(native_, &outer))
            return false;
        applyingRegion_ = true;
        // An emergency/Qt fallback keeps its complete outer HWND inside rcWork
        // and disables resizing. Do not retain a stale exclusion from a prior
        // taskbar direction or monitor in that conservative presentation.
        const QRect taskbar =
            window_ && window_->property("menuConservativeWorkAreaFallback").toBool() ? QRect{}
                                                                                      : taskbar_;
        const bool applied = applyMenuInputRegion(native_, rectangle(outer), taskbar);
        applyingRegion_ = false;
        if (!applied)
            ShowWindow(native_, SW_HIDE); // Do not leave a taskbar-blocking surface visible.
        return applied;
    }
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
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        if (self && id == reinterpret_cast<UINT_PTR>(self) && message != WM_NCDESTROY &&
            (message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED ||
             message == WM_SHOWWINDOW))
            self->refreshInputRegion();
        return result;
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
            QRect bounds = window_->property("menuOuterBoundsPhysical").toRect();
            if (bounds.isValid() && window_->property("screenGapDip").isValid()) {
                // A display/work-area change while visible must not allow the
                // next sizing loop to use a larger rectangle from the reveal.
                // This is a short monitor query, with no Shell/UIA enumeration.
                DesktopAnchorResult current;
                setMonitor(current, MonitorFromWindow(message.hwnd, MONITOR_DEFAULTTONEAREST));
                const int gap = qRound(window_->property("screenGapDip").toDouble() * scale);
                const int inset = qRound(window_->property("shadowInset").toDouble() * scale);
                const QRect panel = desktopMenuPanelBounds(current, gap);
                if (panel.isValid()) {
                    const QRect currentBounds = panel.adjusted(-inset, -inset, inset, inset);
                    const QRect overlap = bounds.intersected(currentBounds);
                    bounds = overlap.isValid() ? overlap : currentBounds;
                }
            }
            QRect fixed =
                desktopResizePhysicalRect(initial_, rectangle(proposed), edges, minimum, bounds);
            int maximumWidth = qMax(1, qRound(window_->maximumWidth() * scale));
            if (bounds.isValid()) {
                const qint64 room = edges.testFlag(Qt::LeftEdge)
                                        ? qint64(initial_.right()) - bounds.left() + 1
                                        : qint64(bounds.right()) - initial_.left() + 1;
                maximumWidth =
                    static_cast<int>(std::min<qint64>(maximumWidth, qMax<qint64>(1, room)));
            }
            fixed = desktopSnapResizeWidth(initial_, fixed, edges,
                                           qMin(minimum.width(), maximumWidth), maximumWidth, scale,
                                           window_->property("resizeWidthBaseDip").toDouble(),
                                           window_->property("resizeWidthPitchDip").toInt(),
                                           qRound(window_->property("shadowInset").toDouble()));
            fixed = desktopResizePhysicalRect(initial_, fixed, edges, minimum, bounds);
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
    QRect taskbar_;
    bool taskbarRegionKnown_ = false;
    bool applyingRegion_ = false;
};

class WindowsStartButtonQueryCache final {
  public:
    ComPtr<IUIAutomationElement> button;
    ComPtr<IUIAutomationCacheRequest> properties;
    IUIAutomation* client = nullptr; // The owner keeps this same MTA client alive.
    DesktopNativeWindowObservation native;
    QRect bounds;
    quint64 epoch = 0;
    quint64 boundAt = 0;
    void clearBinding() {
        button.reset();
        native = {};
        bounds = {};
        boundAt = 0;
    }
};

WindowsDesktopPlatform::WindowsDesktopPlatform() = default;
WindowsDesktopPlatform::~WindowsDesktopPlatform() = default;

StartButtonCaptureTarget WindowsDesktopPlatform::queryStartButtonCaptureTarget() {
    ComApartment apartment(COINIT_MULTITHREADED);
    ComPtr<IUIAutomation> automation;
    if (!apartment.ready() ||
        FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(automation.put()))))
        return {};
    ComPtr<IUIAutomation2> bounded;
    if (FAILED(automation->QueryInterface(IID_PPV_ARGS(bounded.put()))) ||
        FAILED(bounded->put_ConnectionTimeout(100)) || FAILED(bounded->put_TransactionTimeout(100)))
        return {};
    const auto result =
        queryStartButtonCaptureTarget(automation.get(), StartButtonCaptureMode::Point, 0, nullptr);
    captureQueryCache_.reset(); // Release its COM element before the local apartment exits.
    return result;
}

StartButtonCaptureTarget WindowsDesktopPlatform::queryStartButtonCaptureTarget(
    IUIAutomation* automation, StartButtonCaptureMode mode, quint64 epoch,
    StartButtonQueryObservation* observation, IUIAutomationElement** validatedButton) {
    if (validatedButton)
        *validatedButton = nullptr;
    using Reason = StartButtonCaptureReason;
    const auto began = GetTickCount64();
    struct Duration {
        StartButtonQueryObservation* observation;
        quint64 began;
        ~Duration() {
            if (observation)
                observation->durationMs = GetTickCount64() - began;
        }
    } duration{observation, began};
    if (observation)
        *observation = {};
    const auto failure = [&](Reason reason, const QString& stage, HRESULT result = S_OK) {
        if (mode == StartButtonCaptureMode::ExperimentalAreaCache && captureQueryCache_ &&
            reason != Reason::QueryTimeout)
            captureQueryCache_->clearBinding();
        if (observation) {
            observation->reason = reason;
            observation->stage = stage;
            observation->result = result;
        }
        return StartButtonCaptureTarget{};
    };
    PhysicalCoordinates coordinates;
    POINT cursor{};
    if (!automation || !coordinates.ready() || !GetPhysicalCursorPos(&cursor))
        return failure(Reason::QueryFailed, QStringLiteral("query-context"));
    HWND taskbar = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
    if (mode == StartButtonCaptureMode::ExperimentalAreaCache) {
        taskbar = nullptr;
        TaskbarQuery bars;
        EnumWindows(collectTaskbars, reinterpret_cast<LPARAM>(&bars));
        const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        for (const HWND candidate : bars.windows) {
            if (MonitorFromWindow(candidate, MONITOR_DEFAULTTONULL) == monitor) {
                if (taskbar)
                    return failure(Reason::Unidentified, QStringLiteral("ambiguous-taskbar"));
                taskbar = candidate;
            }
        }
    }
    const auto before = observeWindow(taskbar);
    if (!before.visible ||
        (mode == StartButtonCaptureMode::Point &&
         !before.physicalRect.contains(QPoint(cursor.x, cursor.y))) ||
        (before.className != QStringLiteral("Shell_TrayWnd") &&
         before.className != QStringLiteral("Shell_SecondaryTrayWnd")) ||
        !isExplorerWindow(taskbar))
        return failure(Reason::Unidentified, QStringLiteral("native-taskbar"));

    const bool bulk = mode == StartButtonCaptureMode::ExperimentalAreaCache;
    if (!bulk || !captureQueryCache_ || captureQueryCache_->client != automation)
        captureQueryCache_ = std::make_unique<WindowsStartButtonQueryCache>();
    auto& cache = *captureQueryCache_;
    cache.client = automation;
    if (bulk && !cache.properties.get()) {
        HRESULT configured = automation->CreateCacheRequest(cache.properties.put());
        if (SUCCEEDED(configured))
            configured = cache.properties->put_TreeScope(TreeScope_Element);
        if (SUCCEEDED(configured))
            configured = cache.properties->put_AutomationElementMode(AutomationElementMode_Full);
        ComPtr<IUIAutomationCondition> rawView;
        if (SUCCEEDED(configured))
            configured = automation->get_RawViewCondition(rawView.put());
        if (SUCCEEDED(configured))
            configured = cache.properties->put_TreeFilter(rawView.get());
        for (const PROPERTYID property : {UIA_BoundingRectanglePropertyId, UIA_IsOffscreenPropertyId,
                                          UIA_ProcessIdPropertyId, UIA_AutomationIdPropertyId,
                                          UIA_ControlTypePropertyId}) {
            if (SUCCEEDED(configured))
                configured = cache.properties->AddProperty(property);
        }
        if (FAILED(configured)) {
            cache.properties.reset();
            return failure(Reason::QueryFailed, QStringLiteral("uia-property-cache"), configured);
        }
    }
    const bool bound = bulk && cache.button.get() && cache.epoch == epoch &&
                       sameWindowObservation(cache.native, before);
    if (!bound) {
        cache.clearBinding();
        QString error;
        if (!findStartButton(automation, taskbar, cache.bounds, error, nullptr, cache.button.put(),
                             cache.properties.get()))
            return failure(Reason::QueryFailed, error);
        cache.native = before;
        cache.epoch = epoch;
        cache.boundAt = began;
    }
    if (observation)
        observation->reusedElement = bound;
    const QRect button = cache.bounds;
    const QPoint qualified =
        mode == StartButtonCaptureMode::Point ? QPoint(cursor.x, cursor.y) : button.center();
    if (!button.contains(qualified))
        return failure(Reason::OutsideButton, QStringLiteral("outside-start"));
    POINT point{qualified.x(), qualified.y()};
    const HWND hit = WindowFromPoint(point);
    if (observation) {
        const auto observedHit = observeWindow(hit);
        observation->details = {
            {QStringLiteral("bulkProperties"), bulk},
            {QStringLiteral("uniqueBindingAt"), qint64(cache.boundAt)},
            {QStringLiteral("buttonX"), button.x()},
            {QStringLiteral("buttonY"), button.y()},
            {QStringLiteral("buttonWidth"), button.width()},
            {QStringLiteral("buttonHeight"), button.height()},
            {QStringLiteral("taskbar"), QString::number(reinterpret_cast<quintptr>(taskbar), 16)},
            {QStringLiteral("hit"), QString::number(reinterpret_cast<quintptr>(hit), 16)},
            {QStringLiteral("hitRoot"), QString::number(observedHit.rootHandle, 16)},
            {QStringLiteral("hitClass"), observedHit.className},
            {QStringLiteral("hitPid"), static_cast<qint64>(observedHit.processId)},
            {QStringLiteral("taskbarPid"), static_cast<qint64>(before.processId)}};
    }
    if (GetAncestor(hit, GA_ROOT) != taskbar)
        return failure(Reason::NativeTargetChanged, QStringLiteral("start-occluded"));
    ComPtr<IUIAutomationElement> atPoint;
    HRESULT status = bulk ? automation->ElementFromPointBuildCache(point, cache.properties.get(),
                                                                  atPoint.put())
                          : automation->ElementFromPoint(point, atPoint.put());
    if (FAILED(status))
        return failure(Reason::QueryFailed, QStringLiteral("element-from-point"), status);
    BOOL same = FALSE;
    // A provider may expose a text child at the point. Normalize to the known
    // button using at most eight raw parents, never by localized display name.
    ComPtr<IUIAutomationTreeWalker> walker;
    status = automation->get_RawViewWalker(walker.put());
    if (FAILED(status))
        return failure(Reason::QueryFailed, QStringLiteral("raw-view-walker"), status);
    IUIAutomationElement* current = atPoint.get();
    current->AddRef();
    ComPtr<IUIAutomationElement> matched;
    HRESULT compareStatus = S_OK;
    HRESULT parentStatus = S_OK;
    int walked = 0;
    for (int depth = 0; current && depth < 8; ++depth) {
        ++walked;
        compareStatus = automation->CompareElements(cache.button.get(), current, &same);
        IUIAutomationElement* parent = nullptr;
        if (SUCCEEDED(compareStatus) && !same)
            parentStatus = bulk ? walker->GetParentElementBuildCache(current, cache.properties.get(),
                                                                    &parent)
                                : walker->GetParentElement(current, &parent);
        if (SUCCEEDED(compareStatus) && same && bulk) {
            *matched.put() = current;
            current->AddRef();
        }
        current->Release();
        current = parent;
        if (FAILED(compareStatus) || FAILED(parentStatus) || same)
            break;
    }
    if (current)
        current->Release();
    if (observation)
        observation->details.insert(QStringLiteral("rawParentsCompared"), walked);
    if (FAILED(compareStatus))
        return failure(Reason::QueryFailed, QStringLiteral("compare-elements"), compareStatus);
    if (FAILED(parentStatus))
        return failure(Reason::QueryFailed, QStringLiteral("raw-parent"), parentStatus);
    if (!same)
        return failure(Reason::NativeTargetChanged, QStringLiteral("uia-point-identity"));
    RECT finalButton{};
    BOOL offscreen = TRUE;
    POINT afterPoint{};
    int providerProcess = 0;
    // Use properties fetched with the point/parent identity observation itself;
    // a separate refresh would add another provider round trip after that match.
    if (bulk && !matched.get())
        return failure(Reason::QueryFailed, QStringLiteral("button-point-cache"), E_POINTER);
    status = bulk ? matched->get_CachedBoundingRectangle(&finalButton)
                  : cache.button->get_CurrentBoundingRectangle(&finalButton);
    if (FAILED(status))
        return failure(Reason::QueryFailed, QStringLiteral("button-bounds-read"), status);
    if (rectangle(finalButton) != button)
        return failure(Reason::NativeTargetChanged, QStringLiteral("button-bounds-changed"));
    status = bulk ? matched->get_CachedIsOffscreen(&offscreen)
                  : cache.button->get_CurrentIsOffscreen(&offscreen);
    if (FAILED(status))
        return failure(Reason::QueryFailed, QStringLiteral("button-offscreen-read"), status);
    if (offscreen)
        return failure(Reason::NativeTargetChanged, QStringLiteral("button-offscreen"));
    status = bulk ? matched->get_CachedProcessId(&providerProcess)
                  : cache.button->get_CurrentProcessId(&providerProcess);
    if (observation)
        observation->details.insert(QStringLiteral("providerPid"), providerProcess);
    if (FAILED(status))
        return failure(Reason::QueryFailed, QStringLiteral("button-process-read"), status);
    if (providerProcess != static_cast<int>(before.processId))
        return failure(Reason::NativeTargetChanged, QStringLiteral("provider-process-changed"));
    if (bulk) {
        BSTR identifier = nullptr;
        status = matched->get_CachedAutomationId(&identifier);
        QString name;
        if (identifier && SysStringLen(identifier) == 11)
            name = QString::fromWCharArray(identifier, 11);
        SysFreeString(identifier);
        if (FAILED(status))
            return failure(Reason::QueryFailed, QStringLiteral("button-id-read"), status);
        CONTROLTYPEID type = 0;
        status = matched->get_CachedControlType(&type);
        if (FAILED(status))
            return failure(Reason::QueryFailed, QStringLiteral("button-type-read"), status);
        const StartButtonElementObservation semantic{
            name, rectangle(finalButton), quint32(providerProcess), same != FALSE,
            type == UIA_ButtonControlTypeId, offscreen != FALSE};
        const auto semanticFailure = startButtonElementFailure(semantic, before.processId, button);
        if (semanticFailure != Reason::None)
            return failure(semanticFailure, QStringLiteral("bound-element-semantics"));
    }
    if (!sameWindowObservation(before, observeWindow(taskbar)))
        return failure(Reason::NativeTargetChanged, QStringLiteral("taskbar-native-changed"));
    if (WindowFromPoint(point) != hit)
        return failure(Reason::NativeTargetChanged, QStringLiteral("hit-window-changed"));
    if (mode == StartButtonCaptureMode::Point &&
        (!GetPhysicalCursorPos(&afterPoint) || point.x != afterPoint.x || point.y != afterPoint.y))
        return failure(Reason::QueryPointerMoved, QStringLiteral("pointer-moved"));
    if (GetTickCount64() - began > 200)
        return failure(Reason::QueryTimeout, QStringLiteral("qualification-expired"));
    StartButtonCaptureTarget result;
    result.observedAt = began;
    result.epoch = epoch;
    result.qualifiedPoint = qualified;
    result.mode = mode;
    result.areaValidated = mode == StartButtonCaptureMode::ExperimentalAreaCache;
    result.taskbar = before.handle;
    result.hitWindow = reinterpret_cast<quintptr>(hit);
    result.processId = before.processId;
    result.taskbarRect = before.physicalRect;
    result.buttonRect = button;
    const HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return failure(Reason::InvalidIdentity, QStringLiteral("monitor-query"));
    result.monitor = reinterpret_cast<quintptr>(monitor);
    result.monitorRect = rectangle(info.rcMonitor);
    result.dpi = GetDpiForWindow(taskbar);
    result.identified = true;
    if (validatedButton) {
        *validatedButton = cache.button.get();
        (*validatedButton)->AddRef();
    }
    if (observation) {
        observation->reason = Reason::None;
        observation->stage = QStringLiteral("qualified");
    }
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
    // Never raise the root over an owned file/settings dialog during a repeat
    // activation. Its owner chain is the same exact-process boundary as focus.
    if (hasOwnedForegroundDialog(nativeHandle)) {
        result.visible = IsWindowVisible(window) != FALSE;
        result.minimized = IsIconic(window) != FALSE;
        result.foreground = true;
        return result;
    }
    // Qt's first showNormal can inherit STARTUPINFO's SW_HIDE/SW_SHOWMINIMIZED.
    // The explicit user reveal restores the actual HWND, without moving it or
    // changing its flags, taskbar settings, other windows or global lock policy.
    if (IsIconic(window) || !IsWindowVisible(window))
        ShowWindow(window, SW_RESTORE);
    if (!SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
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
    // A background/hidden request can return success without granting the
    // topmost band. Reassert it after this user-requested foreground restore,
    // then measure the policy rather than treating the API BOOL as evidence.
    const bool topmostRequested =
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
    const bool topmost = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    result.minimized = IsIconic(window) != FALSE;
    result.foreground = GetForegroundWindow() == window;
    if (!topmostRequested || !topmost)
        result.errorCode = QStringLiteral("menu-topmost-state-denied");
    else if (result.errorCode.isEmpty() && (!result.visible || result.minimized))
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
    const auto extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const auto menuExtended = (extended | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
    if (extended != menuExtended) {
        if (IsWindowVisible(window))
            return {false, false, QStringLiteral("菜单窗口样式必须在隐藏阶段配置。"),
                    QStringLiteral("menu-style-must-be-hidden")};
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_EXSTYLE, menuExtended) && GetLastError())
            return {false, false, QStringLiteral("无法配置菜单窗口的任务栏行为。"),
                    QStringLiteral("menu-toolwindow-style-failed")};
    }
    if (!SetWindowPos(window, nullptr, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED))
        return {false, false, QStringLiteral("无法应用菜单窗口的任务栏样式。"),
                QStringLiteral("menu-frame-style-failed")};
    if (!SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
        return {false, false, QStringLiteral("无法配置菜单窗口置顶行为。"),
                QStringLiteral("menu-topmost-style-failed")};
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
        if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST))
            return {false, false,
                    QStringLiteral("隐藏阶段的窗口置顶未生效，将在用户唤起后重新请求并复核。"),
                    QStringLiteral("menu-topmost-state-denied")};
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
    // The taskbar exclusion above preserves WS_EX_LAYERED for per-pixel alpha.
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
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        result.success = false;
        result.message = QStringLiteral("隐藏阶段的窗口置顶未生效，将在用户唤起后重新请求并复核。");
        result.errorCode = QStringLiteral("menu-topmost-state-denied");
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
    // The layout picker is the only foreign process accepted here. Its live
    // process handle and exact owner chain are registered by the request that
    // created it; a matching title or a recycled PID is never sufficient.
    if (native_picker::isTrustedForeground(menu, foreground))
        return true;
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

bool WindowsDesktopPlatform::hasOwnedForegroundDialog(quintptr menuHandle) {
    const HWND foreground = GetForegroundWindow();
    return foreground && foreground != reinterpret_cast<HWND>(menuHandle) &&
           hasOwnedForegroundWindow(menuHandle) && GetForegroundWindow() == foreground;
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
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    RECT current{};
    if (!window || !IsWindow(window) || process != GetCurrentProcessId() ||
        thread != GetCurrentThreadId() || GetAncestor(window, GA_ROOT) != window ||
        !GetWindowRect(window, &current)) {
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
    QRect taskbarExclusion = anchor.taskbarPhysicalRect;
    if (!taskbarExclusion.isValid() && anchor.monitorPhysicalRect.isValid() &&
        anchor.workAreaPhysicalRect.isValid() &&
        anchor.monitorPhysicalRect.contains(anchor.workAreaPhysicalRect)) {
        const QRegion reserved =
            QRegion(anchor.monitorPhysicalRect).subtracted(QRegion(anchor.workAreaPhysicalRect));
        if (reserved.rectCount() == 1)
            taskbarExclusion = reserved.boundingRect();
    }
    const bool regionApplied =
        resizeFilter_ && resizeFilter_->matchesHandle(window)
            ? resizeFilter_->setTaskbarRegion(taskbarExclusion)
            : applyMenuInputRegion(window, nativePlacement, taskbarExclusion);
    if (!regionApplied)
        return {false,
                {},
                QStringLiteral("无法排除任务栏输入区域，本次显示已取消。"),
                QStringLiteral("menu-input-region-failed")};
    if (activate) {
        SetForegroundWindow(window);
    }
    return {true, placement, QStringLiteral("菜单已按任务栏方向、对齐与工作区定位。"), {}};
}

} // namespace xdstarter
