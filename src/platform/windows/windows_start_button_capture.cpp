// src/platform/windows/windows_start_button_capture.cpp

#include "platform/start_button_capture.h"
#include "platform/start_button_capture_state.h"
#include "windows_desktop_platform.h"

#include <QDebug>

#include <objbase.h>
#include <uiautomation.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace xdstarter {
namespace {
constexpr UINT applyMessage = WM_APP + 91;
constexpr UINT toggleMessage = WM_APP + 92;

bool inputDesktopAvailable() {
    if (GetSystemMetrics(SM_REMOTESESSION) || GetSystemMetrics(SM_SWAPBUTTON))
        return false;
    const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    wchar_t name[128]{};
    wchar_t threadName[128]{};
    DWORD bytes = 0;
    const bool valid = desktop &&
                       GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes) &&
                       GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                                                 threadName, sizeof(threadName), &bytes) &&
                       _wcsicmp(name, L"Default") == 0 && _wcsicmp(name, threadName) == 0;
    if (desktop)
        CloseDesktop(desktop);
    return valid;
}

// Provider event callbacks invalidate only. Neither these nor input callbacks
// call UIA, wait for the GUI or perform file/process-image queries.
class TargetEvents final : public IUIAutomationPropertyChangedEventHandler,
                           public IUIAutomationStructureChangedEventHandler {
  public:
    explicit TargetEvents(std::shared_ptr<std::atomic<quint64>> epoch) : epoch_(std::move(epoch)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IUIAutomationPropertyChangedEventHandler)
            *value = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (iid == IID_IUIAutomationStructureChangedEventHandler)
            *value = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID,
                                                         VARIANT) override {
        ++*epoch_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*,
                                                          StructureChangeType,
                                                          SAFEARRAY*) override {
        ++*epoch_;
        return S_OK;
    }

  private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<std::atomic<quint64>> epoch_;
};

class WindowsStartButtonCapture final : public WinKeyCapture {
  public:
    ~WindowsStartButtonCapture() override { stop(); }
    bool isSupported() const override { return !GetSystemMetrics(SM_REMOTESESSION); }
    bool isActive() const override { return active_; }

    void setEnabled(bool enabled) override {
        if (!enabled) {
            requested_ = false;
            const auto generation = endEventSession();
            generation_ = generation;
            active_ = false;
            ++epoch_;
            wake();
            queueStatusChanged(generation, false, QStringLiteral("开始按钮接管已暂停。"), {});
            return;
        }
        if (needsRestart_.exchange(false))
            stop(); // A failed timer/UIA/event registration needs fresh threads.
        if (requested_.exchange(true))
            return;
        generation_ = beginEventSession();
        if (hookThread_.joinable() && hookExited_)
            hookThread_.join();
        if (!hookThread_.joinable()) {
            stopping_ = false;
            hookExited_ = false;
            try {
                if (!queryThread_.joinable())
                    queryThread_ = std::thread([this] { queryLoop(); });
                hookThread_ = std::thread([this] { hookLoop(); });
            } catch (const std::system_error&) {
                stopping_ = true;
                requested_ = false;
                if (queryThread_.joinable())
                    queryThread_.join();
                queueStatusChanged(generation_, false, QStringLiteral("开始按钮接管线程启动失败。"),
                                   QStringLiteral("start-capture-thread-failed"));
            }
        } else {
            wake();
        }
    }

    void stop() override {
        endEventSession();
        requested_ = false;
        active_ = false;
        stopping_ = true;
        ++epoch_;
        wake();
        if (queryThread_.joinable())
            queryThread_.join();
        if (hookThread_.joinable())
            hookThread_.join();
    }

  private:
    void wake() {
        if (const DWORD thread = threadId_.load())
            PostThreadMessageW(thread, applyMessage, 0, 0);
    }

    bool liveTarget(const StartButtonCaptureTarget& target, POINT point) const {
        const HWND bar = reinterpret_cast<HWND>(target.taskbar);
        const HWND hit = WindowFromPoint(point);
        RECT bounds{};
        DWORD pid = 0;
        GetWindowThreadProcessId(bar, &pid);
        const HMONITOR monitor = MonitorFromWindow(bar, MONITOR_DEFAULTTONULL);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        return target.epoch == epoch_ && IsWindowVisible(bar) && pid == target.processId &&
               monitor && reinterpret_cast<quintptr>(monitor) == target.monitor &&
               GetDpiForWindow(bar) == target.dpi && GetMonitorInfoW(monitor, &info) &&
               QRect(info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top) == target.monitorRect &&
               GetWindowRect(bar, &bounds) &&
               QRect(bounds.left, bounds.top, bounds.right - bounds.left,
                     bounds.bottom - bounds.top) == target.taskbarRect &&
               GetAncestor(hit, GA_ROOT) == bar &&
               reinterpret_cast<quintptr>(hit) == target.hitWindow;
    }

    static LRESULT CALLBACK mouseCallback(int code, WPARAM message, LPARAM parameter) {
        auto* self = current_;
        if (code != HC_ACTION || !self)
            return CallNextHookEx(nullptr, code, message, parameter);
        const auto& native = *reinterpret_cast<const MSLLHOOKSTRUCT*>(parameter);
        const auto generation = self->generation_.load();
        StartMouseEvent event = StartMouseEvent::OtherButton;
        switch (message) {
        case WM_MOUSEMOVE:
            event = StartMouseEvent::Move;
            break;
        case WM_LBUTTONDOWN:
            event = StartMouseEvent::LeftDown;
            break;
        case WM_LBUTTONUP:
            event = StartMouseEvent::LeftUp;
            break;
        default:
            break;
        }
        StartButtonCaptureTarget target;
        {
            std::unique_lock lock(self->snapshotMutex_, std::try_to_lock);
            if (lock.owns_lock())
                target = self->snapshot_;
        }
        const bool modifiers =
            (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000) ||
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
            (GetAsyncKeyState(VK_MBUTTON) & 0x8000);
        const bool eligible = self->active_ && self->requested_ && !self->stopping_ && !modifiers &&
                              self->liveTarget(target, native.pt);
        const auto decision =
            self->gesture_.process(event, {native.pt.x, native.pt.y}, GetTickCount64(), target,
                                   eligible, (native.flags & LLMHF_INJECTED) != 0);
        if (decision.toggle && !PostThreadMessageW(GetCurrentThreadId(), toggleMessage,
                                                   static_cast<WPARAM>(generation), 0)) {
            self->requested_ = false;
            self->active_ = false;
            self->wake();
        }
        return decision.consume ? 1 : CallNextHookEx(nullptr, code, message, parameter);
    }

    static LRESULT CALLBACK keyboardCallback(int code, WPARAM message, LPARAM parameter) {
        if (code == HC_ACTION && current_)
            current_->gesture_.cancel();
        return CallNextHookEx(nullptr, code, message, parameter);
    }

    static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG, LONG, DWORD,
                                     DWORD) {
        auto* self = current_;
        if (!self || (event != EVENT_OBJECT_DESTROY && event != EVENT_OBJECT_SHOW &&
                      event != EVENT_OBJECT_HIDE && event != EVENT_OBJECT_REORDER &&
                      event != EVENT_OBJECT_LOCATIONCHANGE))
            return;
        const HWND bar = reinterpret_cast<HWND>(self->observedTaskbar_.load());
        if (window == bar || (bar && GetAncestor(window, GA_ROOT) == bar))
            ++self->epoch_;
    }

    void queryLoop() {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IUIAutomation* automation = nullptr;
        IUIAutomationElement* root = nullptr;
        auto* events = new TargetEvents(epochStorage_);
        HWND subscribed = nullptr;
        if (SUCCEEDED(initialized))
            CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&automation));
        if (automation) {
            IUIAutomation2* bounded = nullptr;
            const bool configured = SUCCEEDED(automation->QueryInterface(IID_PPV_ARGS(&bounded))) &&
                                    SUCCEEDED(bounded->put_ConnectionTimeout(100)) &&
                                    SUCCEEDED(bounded->put_TransactionTimeout(100));
            if (bounded)
                bounded->Release();
            if (!configured) {
                automation->Release();
                automation = nullptr;
            }
        }
        WindowsDesktopPlatform platform;
        quint64 qualifiedGeneration = 0;
        while (!stopping_) {
            if (requested_ && active_ && !automation) {
                needsRestart_ = true;
                requested_ = false;
                active_ = false;
                queueStatusChanged(generation_, false,
                                   QStringLiteral("UIA 初始化不可用，已保留系统入口。"),
                                   QStringLiteral("start-capture-uia-unavailable"));
                wake();
            }
            if (requested_ && active_ && automation) {
                const auto epoch = epoch_.load();
                auto target = platform.queryStartButtonCaptureTarget();
                const HWND bar = reinterpret_cast<HWND>(target.taskbar);
                if (target.identified && bar != subscribed) {
                    automation->RemoveAllEventHandlers();
                    if (root) {
                        root->Release();
                        root = nullptr;
                    }
                    subscribed = nullptr;
                    PROPERTYID properties[] = {UIA_BoundingRectanglePropertyId,
                                               UIA_IsOffscreenPropertyId,
                                               UIA_AutomationIdPropertyId,
                                               UIA_ControlTypePropertyId, UIA_ProcessIdPropertyId};
                    if (SUCCEEDED(automation->ElementFromHandle(bar, &root)) &&
                        SUCCEEDED(automation->AddStructureChangedEventHandler(
                            root, TreeScope_Subtree, nullptr, events)) &&
                        SUCCEEDED(automation->AddPropertyChangedEventHandlerNativeArray(
                            root, TreeScope_Subtree, nullptr, events, properties,
                            static_cast<int>(std::size(properties)))))
                        subscribed = bar;
                    observedTaskbar_ = reinterpret_cast<quintptr>(subscribed);
                    ++epoch_;
                }
                target.epoch = epoch;
                target.identified = target.identified && epoch == epoch_ && bar == subscribed;
                if (target.identified && qualifiedGeneration != generation_) {
                    qualifiedGeneration = generation_;
                    qInfo() << "Start button point qualified in"
                            << GetTickCount64() - target.observedAt
                            << "ms; native input can be captured.";
                }
                std::lock_guard lock(snapshotMutex_);
                snapshot_ = target;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        if (automation) {
            automation->RemoveAllEventHandlers();
            automation->Release();
        }
        if (root)
            root->Release();
        events->Release();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    }

    void hookLoop() {
        current_ = this;
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId_ = GetCurrentThreadId();
        const auto timer = SetTimer(nullptr, 0, 100, nullptr);
        HHOOK hook = nullptr;
        HHOOK keyboard = nullptr;
        const auto events =
            SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, windowEvent,
                            0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        bool announced = false;
        bool announcedActive = false;
        quint64 announcedGeneration = 0;
        ULONGLONG drainingSince = 0;
        ULONGLONG desktopCheckedAt = 0;
        for (;;) {
            if (generation_ != announcedGeneration)
                announced = false;
            if (requested_ && !hook && timer && events && inputDesktopAvailable()) {
                keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardCallback,
                                             GetModuleHandleW(nullptr), 0);
                if (keyboard)
                    hook =
                        SetWindowsHookExW(WH_MOUSE_LL, mouseCallback, GetModuleHandleW(nullptr), 0);
                active_ = hook && keyboard;
                if (!active_) {
                    needsRestart_ = true;
                    requested_ = false;
                    if (keyboard) {
                        UnhookWindowsHookEx(keyboard);
                        keyboard = nullptr;
                    }
                }
                announced = false;
            }
            if (hook && requested_ && generation_ != announcedGeneration) {
                active_ = true;
                announced = false;
            }
            const auto now = GetTickCount64();
            if (active_ && now - desktopCheckedAt >= 100 && !inputDesktopAvailable()) {
                requested_ = false;
                active_ = false;
                ++epoch_;
            }
            if (now - desktopCheckedAt >= 100)
                desktopCheckedAt = now;
            if (!requested_ || stopping_)
                gesture_.cancel();
            // Drain an owned release on pause/normal exit. The native Shell has
            // never seen its down, so forwarding just its up is not a fallback.
            if (hook && (!requested_ || stopping_) && !gesture_.ownsPress()) {
                UnhookWindowsHookEx(hook);
                hook = nullptr;
                active_ = false;
                if (keyboard) {
                    UnhookWindowsHookEx(keyboard);
                    keyboard = nullptr;
                }
            }
            if (stopping_ && !gesture_.ownsPress())
                break;
            // A disconnected device must not hang process shutdown forever.
            // No action is dispatched after stop; a late unmatched up is left
            // to Windows after the bounded drain expires.
            if (stopping_ && !drainingSince)
                drainingSince = GetTickCount64();
            if (drainingSince && GetTickCount64() - drainingSince >= 2000)
                break;
            if (!announced || announcedActive != active_) {
                announced = true;
                announcedActive = active_;
                announcedGeneration = generation_;
                queueStatusChanged(
                    generation_, active_,
                    active_ ? QStringLiteral("开始按钮接管已启用；未命中或识别失效时使用系统入口。")
                            : QStringLiteral("开始按钮接管不可用，已保留系统入口。"),
                    active_ ? QString() : QStringLiteral("start-capture-unavailable"));
                if (!active_)
                    requested_ = false;
                if (!timer || !events)
                    needsRestart_ = true;
            }
            const int received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0)
                break;
            if (message.message == toggleMessage && active_ && requested_ && !stopping_)
                queueToggleRequested(static_cast<quint64>(message.wParam));
            else if (message.message != WM_TIMER && message.message != applyMessage) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        active_ = false;
        if (hook)
            UnhookWindowsHookEx(hook);
        if (keyboard)
            UnhookWindowsHookEx(keyboard);
        if (events)
            UnhookWinEvent(events);
        if (timer)
            KillTimer(nullptr, timer);
        threadId_ = 0;
        if (!stopping_) {
            requested_ = false;
            ++epoch_;
            queueStatusChanged(generation_, false,
                               QStringLiteral("开始按钮消息线程已停止，使用系统入口。"),
                               QStringLiteral("start-capture-message-loop-ended"));
        }
        current_ = nullptr;
        hookExited_ = true;
    }

    std::atomic_bool requested_{false}, active_{false}, stopping_{false};
    std::atomic_bool hookExited_{false};
    std::atomic_bool needsRestart_{false};
    std::atomic<quint64> generation_{0};
    std::shared_ptr<std::atomic<quint64>> epochStorage_ = std::make_shared<std::atomic<quint64>>(0);
    std::atomic<quint64>& epoch_ = *epochStorage_;
    std::atomic<quintptr> observedTaskbar_{0};
    std::atomic<DWORD> threadId_{0};
    std::thread queryThread_, hookThread_;
    std::mutex snapshotMutex_;
    StartButtonCaptureTarget snapshot_;
    StartButtonCaptureState gesture_; // hook thread only
    static thread_local WindowsStartButtonCapture* current_;
};
thread_local WindowsStartButtonCapture* WindowsStartButtonCapture::current_ = nullptr;
} // namespace

std::unique_ptr<WinKeyCapture> createStartButtonCapture() {
    return std::make_unique<WindowsStartButtonCapture>();
}
} // namespace xdstarter
