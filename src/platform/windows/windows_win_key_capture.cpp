// src/platform/windows/windows_win_key_capture.cpp

#include "windows_win_key_capture.h"

#include "platform/win_key_capture_state.h"

#include <windows.h>

#include <atomic>
#include <cwchar>
#include <system_error>
#include <thread>

namespace xdstarter {
namespace {

constexpr UINT applyRequestMessage = WM_APP + 81;
constexpr UINT toggleMessage = WM_APP + 82;
constexpr ULONG_PTR injectionMarker = static_cast<ULONG_PTR>(0x58445357494E0001ULL);

bool supportedWindowsSession() {
    using VersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto versionFunction = module ? reinterpret_cast<VersionFunction>(
                                              GetProcAddress(module, "RtlGetVersion"))
                                        : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return versionFunction && versionFunction(&version) == 0 && version.dwMajorVersion >= 10 &&
           version.dwBuildNumber >= 22000 && !GetSystemMetrics(SM_REMOTESESSION);
}

bool defaultInputDesktopAvailable() {
    // Run on the message thread, never inside the low-level input callback.
    if (GetSystemMetrics(SM_REMOTESESSION)) {
        return false;
    }
    const auto inputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!inputDesktop) {
        return false;
    }
    wchar_t inputName[128]{};
    wchar_t threadName[128]{};
    DWORD bytes = 0;
    const bool valid = GetUserObjectInformationW(inputDesktop, UOI_NAME, inputName,
                                                sizeof(inputName), &bytes) &&
                       GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                                                threadName, sizeof(threadName), &bytes) &&
                       _wcsicmp(inputName, L"Default") == 0 &&
                       _wcsicmp(inputName, threadName) == 0;
    CloseDesktop(inputDesktop);
    return valid;
}

WinKeyCaptureState::HeldKeys heldKeyboardKeys() {
    WinKeyCaptureState::HeldKeys result{};
    for (int key = 1; key < static_cast<int>(result.size()); ++key) {
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) {
            // Generic modifier aliases duplicate the left/right keys and have
            // no matching LL key-up. Mouse buttons have their own LL observer.
            continue;
        }
        // Only an initial snapshot: GetAsyncKeyState is not consulted to decide
        // the current low-level keyboard event, whose async state is not set yet.
        result[static_cast<std::size_t>(key)] = (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    if (GetSystemMetrics(SM_SWAPBUTTON)) {
        const auto left = result[VK_LBUTTON];
        result[VK_LBUTTON] = result[VK_RBUTTON];
        result[VK_RBUTTON] = left;
    }
    return result;
}

} // namespace

class WindowsWinKeyCapture::Impl : private WinKeyInputDispatcher {
  public:
    explicit Impl(WindowsWinKeyCapture& owner) : owner_(owner) {}
    ~Impl() { stop(); }

    void setEnabled(bool enabled) {
        if (enabled && !supported_) {
            const auto generation = owner_.endEventSession();
            owner_.queueStatusChanged(
                generation, false, QStringLiteral("当前环境不支持接管；请使用本地 Windows 11 桌面。"),
                QStringLiteral("unsupported-session"));
            return;
        }
        if (enabled && (request_.load() & 1U)) {
            return;
        }
        const auto generation = enabled ? owner_.beginEventSession() : owner_.endEventSession();
        request_.store((generation << 1U) | static_cast<quint64>(enabled));
        if (worker_.joinable() && exited_.load()) {
            worker_.join();
        }
        if (!worker_.joinable()) {
            if (!enabled) {
                owner_.queueStatusChanged(generation, false,
                                          QStringLiteral("Win 键接管已暂停，使用系统开始菜单。"), {});
                return;
            }
            shutdown_.store(false);
            exited_.store(false);
            try {
                worker_ = std::thread([this] { run(); });
            } catch (const std::system_error&) {
                request_.fetch_and(~quint64{1});
                owner_.queueStatusChanged(generation, false,
                                          QStringLiteral("无法启动 Win 键消息线程，已保留系统行为。"),
                                          QStringLiteral("win-key-thread-start-failed"));
            }
        } else {
            wake();
        }
    }

    void stop() {
        const auto generation = owner_.endEventSession();
        request_.store(generation << 1U);
        shutdown_.store(true);
        wake();
        // The worker never waits for GUI work. Joining releases both hooks before
        // the QObject can be destroyed; Qt discards receiver-bound queued calls.
        if (worker_.joinable()) {
            worker_.join();
        }
        active_.store(false);
    }

    bool isActive() const { return active_.load(); }
    bool isSupported() const { return supported_; }

  private:
    enum class CallbackFailure { None, InputDispatch, EventDispatch };

    void wake() const {
        const auto id = threadId_.load();
        if (id) {
            // If the queue is not ready yet, startup reads the latest request.
            // A running queue also has a timer to observe a failed post.
            (void)PostThreadMessageW(id, applyRequestMessage, 0, 0);
        }
    }

    static LRESULT CALLBACK keyboardCallback(int code, WPARAM message, LPARAM data) {
        auto* self = current_;
        if (code != HC_ACTION || !self || !self->active_.load() ||
            self->callbackFailure_ != CallbackFailure::None ||
            !self->owner_.isCurrentEventSession(self->installedGeneration_) ||
            (self->request_.load() & 1U) == 0) {
            return CallNextHookEx(nullptr, code, message, data);
        }
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (!down && !up) {
            return CallNextHookEx(nullptr, code, message, data);
        }
        const auto& native = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const bool injected = (native.flags & LLKHF_INJECTED) != 0;
        auto virtualKey = static_cast<std::uint16_t>(native.vkCode);
        if (virtualKey == VK_SHIFT) {
            virtualKey = static_cast<std::uint16_t>(
                MapVirtualKeyW(native.scanCode, MAPVK_VSC_TO_VK_EX));
        } else if (virtualKey == VK_CONTROL) {
            virtualKey = (native.flags & LLKHF_EXTENDED) ? VK_RCONTROL : VK_LCONTROL;
        } else if (virtualKey == VK_MENU) {
            virtualKey = (native.flags & LLKHF_EXTENDED) ? VK_RMENU : VK_LMENU;
        }
        const WinKeyEvent event{virtualKey, down, injected,
                                injected && native.dwExtraInfo == injectionMarker};
        if (!GetForegroundWindow()) {
            // During an unavailable foreground/input desktop, keep bookkeeping
            // but never carry an alone-Win candidate into a restored desktop.
            (void)self->state_.process(event);
            self->state_.cancelCandidate();
            return CallNextHookEx(nullptr, code, message, data);
        }
        if (self->state_.process(event) != WinKeyDecision::ReplaceRelease) {
            return CallNextHookEx(nullptr, code, message, data);
        }

        // Original Win-down and every combination were forwarded unchanged. A
        // bounded transaction marks this as a used Win gesture before releasing
        // that same Win key. Only a complete transaction suppresses the real up.
        if (!dispatchStandaloneWinRelease(*self, event.key)) {
            self->active_.store(false);
            self->callbackFailure_ = CallbackFailure::InputDispatch;
            (void)PostThreadMessageW(self->threadId_.load(), applyRequestMessage, 0, 0);
            return CallNextHookEx(nullptr, code, message, data);
        }
        if (!PostThreadMessageW(self->threadId_.load(), toggleMessage,
                                static_cast<WPARAM>(self->installedGeneration_), 0)) {
            self->callbackError_ = GetLastError();
            self->active_.store(false);
            self->callbackFailure_ = CallbackFailure::EventDispatch;
        }
        return 1;
    }

    unsigned dispatchMarkAndRelease(std::uint16_t winKey) noexcept override {
        INPUT inputs[3]{};
        for (auto& input : inputs) {
            input.type = INPUT_KEYBOARD;
            input.ki.dwExtraInfo = injectionMarker;
        }
        // Undocumented cancellation key also used by PowerToys. Its behavior
        // requires explicit native compatibility validation, not an API promise.
        inputs[0].ki.wVk = 0xFF;
        inputs[1].ki.wVk = 0xFF;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[2].ki.wVk = static_cast<WORD>(winKey);
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
        const auto inserted = SendInput(3, inputs, sizeof(INPUT));
        if (inserted != 3) {
            callbackError_ = GetLastError();
        }
        return inserted;
    }

    void releaseDummy() noexcept override {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 0xFF;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        input.ki.dwExtraInfo = injectionMarker;
        (void)SendInput(1, &input, sizeof(INPUT));
    }

    static LRESULT CALLBACK mouseCallback(int code, WPARAM message, LPARAM data) {
        auto* self = current_;
        if (code == HC_ACTION && self && message != WM_MOUSEMOVE) {
            // Track only buttons/wheels so a button held before Win also blocks
            // a standalone gesture. Coordinates are never read or recorded.
            std::uint16_t key = 0;
            bool down = false;
            switch (message) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                key = VK_LBUTTON;
                down = message == WM_LBUTTONDOWN;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                key = VK_RBUTTON;
                down = message == WM_RBUTTONDOWN;
                break;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                key = VK_MBUTTON;
                down = message == WM_MBUTTONDOWN;
                break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                const auto& native = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
                key = HIWORD(native.mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                down = message == WM_XBUTTONDOWN;
                break;
            }
            default:
                break;
            }
            if (key) {
                const auto& native = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
                (void)self->state_.process({key, down, (native.flags & LLMHF_INJECTED) != 0,
                                            false});
            } else {
                self->state_.cancelCandidate();
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
    }

    void removeHooks() {
        active_.store(false);
        if (keyboardHook_) {
            UnhookWindowsHookEx(keyboardHook_);
            keyboardHook_ = nullptr;
        }
        if (mouseHook_) {
            UnhookWindowsHookEx(mouseHook_);
            mouseHook_ = nullptr;
        }
        state_.reset();
    }

    void failRequest(quint64 request, const QString& message, const QString& errorCode) {
        removeHooks();
        callbackFailure_ = CallbackFailure::None;
        auto expected = request;
        // A failure from an older installation must not cancel a newer request.
        if (request_.compare_exchange_strong(expected, request & ~quint64{1})) {
            owner_.queueStatusChanged(request >> 1U, false, message, errorCode);
        }
    }

    void applyLatestRequest() {
        auto request = request_.load();
        if (callbackFailure_ != CallbackFailure::None) {
            const auto failure = callbackFailure_;
            const auto error = callbackError_;
            if ((request >> 1U) == installedGeneration_ && (request & 1U)) {
                failRequest(
                    request,
                    failure == CallbackFailure::InputDispatch
                        ? QStringLiteral("Win 键输入派发失败（%1），已透传真实释放并停止接管。")
                              .arg(error)
                        : QStringLiteral("Win 键已释放但菜单通知失败（%1），接管已停止。")
                              .arg(error),
                    failure == CallbackFailure::InputDispatch
                        ? QStringLiteral("win-key-input-dispatch-failed")
                        : QStringLiteral("win-key-event-dispatch-failed"));
                return;
            }
            removeHooks();
            callbackFailure_ = CallbackFailure::None;
        }
        if (!(request & 1U)) {
            const bool wasActive = active_.load();
            removeHooks();
            if (wasActive || lastAppliedRequest_ != request) {
                owner_.queueStatusChanged(request >> 1U, false,
                                          QStringLiteral("Win 键接管已暂停，使用系统开始菜单。"), {});
            }
            lastAppliedRequest_ = request;
            return;
        }
        if (active_.load() && installedGeneration_ == (request >> 1U)) {
            return;
        }
        removeHooks();
        if (!timerId_ || !defaultInputDesktopAvailable()) {
            failRequest(request, QStringLiteral("当前输入桌面不可接管，已保留系统开始菜单。"),
                        QStringLiteral("win-key-input-desktop-unavailable"));
            return;
        }
        installedGeneration_ = request >> 1U;
        keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardCallback,
                                          GetModuleHandleW(nullptr), 0);
        const auto keyboardError = keyboardHook_ ? ERROR_SUCCESS : GetLastError();
        if (keyboardHook_) {
            mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseCallback,
                                          GetModuleHandleW(nullptr), 0);
        }
        if (!keyboardHook_ || !mouseHook_) {
            const auto error = keyboardError ? keyboardError : GetLastError();
            failRequest(request,
                        QStringLiteral("无法安装 Win 键观察 Hook（%1），已保留系统行为。").arg(error),
                        QStringLiteral("win-key-hook-install-failed"));
            return;
        }
        if (shutdown_.load() || request_.load() != request) {
            removeHooks();
            return;
        }
        state_.reset(heldKeyboardKeys());
        active_.store(true);
        lastAppliedRequest_ = request;
        owner_.queueStatusChanged(installedGeneration_, true,
                                  QStringLiteral("单独 Win 键接管已启用；系统组合键保留。"), {});
    }

    void run() {
        current_ = this;
        MSG message{};
        // Ensure a message queue exists before publishing the thread id.
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId_.store(GetCurrentThreadId());
        timerId_ = SetTimer(nullptr, 0, 500, nullptr);
        applyLatestRequest();
        int result = 0;
        while (!shutdown_.load() && (result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (message.message == toggleMessage) {
                if (active_.load() && callbackFailure_ == CallbackFailure::None &&
                    (request_.load() & 1U)) {
                    owner_.queueToggleRequested(static_cast<quint64>(message.wParam));
                }
            } else if (message.message == WM_TIMER && message.wParam == timerId_) {
                if (active_.load() && !defaultInputDesktopAvailable()) {
                    state_.cancelCandidate();
                    const auto request = request_.load();
                    failRequest(request, QStringLiteral("输入桌面已切换，接管已暂停；请手动重新启用。"),
                                QStringLiteral("win-key-input-desktop-changed"));
                } else {
                    applyLatestRequest();
                }
            } else if (message.message == applyRequestMessage) {
                applyLatestRequest();
            } else {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (!shutdown_.load()) {
            const auto request = request_.load();
            failRequest(request,
                        result < 0 ? QStringLiteral("Win 键消息循环失败，已恢复系统入口。")
                                   : QStringLiteral("Win 键消息线程已停止，已恢复系统入口。"),
                        QStringLiteral("win-key-message-loop-stopped"));
        }
        removeHooks();
        if (timerId_) {
            KillTimer(nullptr, timerId_);
            timerId_ = 0;
        }
        threadId_.store(0);
        current_ = nullptr;
        exited_.store(true);
    }

    WindowsWinKeyCapture& owner_;
    const bool supported_ = supportedWindowsSession();
    std::atomic<quint64> request_{0}; // generation << 1 | requested enabled
    std::atomic_bool active_{false};
    std::atomic_bool shutdown_{false};
    std::atomic_bool exited_{false};
    std::atomic<DWORD> threadId_{0};
    std::thread worker_;
    // Everything below is used only on the dedicated message/hook thread.
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    UINT_PTR timerId_ = 0;
    quint64 installedGeneration_ = 0;
    quint64 lastAppliedRequest_ = 0;
    WinKeyCaptureState state_;
    CallbackFailure callbackFailure_ = CallbackFailure::None;
    DWORD callbackError_ = ERROR_SUCCESS;
    static thread_local Impl* current_;
};

thread_local WindowsWinKeyCapture::Impl* WindowsWinKeyCapture::Impl::current_ = nullptr;

WindowsWinKeyCapture::WindowsWinKeyCapture(QObject* parent)
    : WinKeyCapture(parent), impl_(std::make_unique<Impl>(*this)) {}

WindowsWinKeyCapture::~WindowsWinKeyCapture() = default;

void WindowsWinKeyCapture::setEnabled(bool enabled) {
    impl_->setEnabled(enabled);
}

void WindowsWinKeyCapture::stop() {
    impl_->stop();
}

bool WindowsWinKeyCapture::isActive() const {
    return impl_->isActive();
}

bool WindowsWinKeyCapture::isSupported() const {
    return impl_->isSupported();
}

} // namespace xdstarter
