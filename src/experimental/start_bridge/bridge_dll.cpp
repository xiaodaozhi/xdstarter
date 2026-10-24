// src/experimental/start_bridge/bridge_dll.cpp

#include "experimental/start_bridge/win_support.h"

#include <windowsx.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace xdstarter::bridge_poc {
namespace {
struct Runtime {
    std::atomic<SharedSession*> shared{nullptr};
    std::atomic<bool> started{false};
    std::atomic<unsigned> inFlight{0};
    std::atomic<std::uint64_t> target{0};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<unsigned> bindingRevision{0};
    std::array<WireMessage, queueCapacity> queue{};
    std::atomic<unsigned> produced{0};
    std::atomic<unsigned> consumed{0};
    std::uint64_t nextSequence = 0;
};
Runtime runtime;

struct Gesture {
    bool owned = false;
    bool canceled = false;
    InputKind kind = InputKind::Mouse;
    UINT pointer = 0;
    HWND window = nullptr;
    std::uint64_t generation = 0;
    POINT origin{};
    ULONGLONG began = 0;
    bool compatibilityUsed = false;
};
// Only the fixture GUI thread accesses these fields.
Gesture gesture;
Gesture promotedGesture;
ULONGLONG lastOwnedPointer = 0;
HWND lastPointerWindow = nullptr;
std::uint64_t lastPointerGeneration = 0;
unsigned keyboardModifiers = 0;

void setReason(SharedSession* shared, Reason reason) {
    if (shared)
        InterlockedExchange(&shared->reason, static_cast<LONG>(reason));
}
bool queueHasRoom() {
    return runtime.produced.load(std::memory_order_relaxed) -
               runtime.consumed.load(std::memory_order_acquire) <
           queueCapacity;
}
bool pushEvent(const Gesture& completed, SharedSession* shared) {
    if (!queueHasRoom()) {
        InterlockedIncrement64(&shared->eventsDropped);
        setReason(shared, Reason::QueueFull);
        return false;
    }
    WireMessage event;
    event.type = WireType::Toggle;
    event.nonce = shared->configuration.nonce;
    event.sequence = ++runtime.nextSequence;
    event.tick = GetTickCount64();
    event.window = reinterpret_cast<std::uint64_t>(completed.window);
    event.windowGeneration = completed.generation;
    event.processId = GetCurrentProcessId();
    event.threadId = GetCurrentThreadId();
    event.inputKind = completed.kind;
    const unsigned index = runtime.produced.load(std::memory_order_relaxed);
    runtime.queue[index % queueCapacity] = event;
    runtime.produced.store(index + 1, std::memory_order_release);
    InterlockedIncrement64(&shared->eventsQueued);
    return true;
}
bool popEvent(WireMessage& event) {
    const unsigned index = runtime.consumed.load(std::memory_order_relaxed);
    if (index == runtime.produced.load(std::memory_order_acquire))
        return false;
    event = runtime.queue[index % queueCapacity];
    runtime.consumed.store(index + 1, std::memory_order_release);
    return true;
}
bool currentBinding(HWND& window, std::uint64_t& generation) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto revision = runtime.bindingRevision.load(std::memory_order_acquire);
        if (revision & 1)
            continue;
        window = reinterpret_cast<HWND>(runtime.target.load(std::memory_order_relaxed));
        generation = runtime.generation.load(std::memory_order_relaxed);
        if (revision == runtime.bindingRevision.load(std::memory_order_acquire))
            return true;
    }
    return false;
}
void bindWindow(SharedSession* shared, HWND window, std::uint64_t generation) {
    HWND previous = nullptr;
    std::uint64_t previousGeneration = 0;
    if (currentBinding(previous, previousGeneration) && previous == window &&
        previousGeneration == generation)
        return;
    InterlockedIncrement64(&shared->bindingRevision);
    InterlockedExchange(&shared->ready, 0);
    runtime.bindingRevision.fetch_add(1, std::memory_order_acq_rel);
    runtime.target.store(reinterpret_cast<std::uint64_t>(window), std::memory_order_relaxed);
    runtime.generation.store(generation, std::memory_order_relaxed);
    runtime.bindingRevision.fetch_add(1, std::memory_order_release);
    InterlockedExchange64(&shared->boundWindow, reinterpret_cast<LONG64>(window));
    InterlockedExchange64(&shared->boundGeneration, generation);
    InterlockedIncrement64(&shared->bindingRevision);
}
bool gate(SharedSession* shared, HWND window, std::uint64_t generation) {
    if (!shared || !readShared(&shared->ready))
        return false;
    const auto now = GetTickCount64();
    const auto heartbeat = static_cast<ULONGLONG>(readShared(&shared->lastHeartbeat));
    if (now < heartbeat || now - heartbeat > heartbeatLifetimeMs) {
        setReason(shared, Reason::HeartbeatExpired);
        return false;
    }
    return validFixtureWindow(window, shared->configuration.fixture, generation) &&
           reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)) ==
               GetModuleHandleW(nullptr);
}
bool modifiers(WPARAM mouseFlags = 0) {
    return keyboardModifiers || (mouseFlags & (MK_SHIFT | MK_CONTROL | MK_RBUTTON | MK_MBUTTON)) ||
           (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_CONTROL) & 0x8000) ||
           (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000) ||
           (GetKeyState(VK_RWIN) & 0x8000);
}
void releaseGesture(SharedSession* shared) {
    const bool mouse = gesture.owned && gesture.kind == InputKind::Mouse;
    const HWND window = gesture.window;
    gesture = {};
    if (shared)
        InterlockedExchange(&shared->owned, promotedGesture.owned ? 1 : 0);
    if (mouse && GetCapture() == window)
        ReleaseCapture();
}
void discardDestroyedGesture(SharedSession* shared) {
    HWND bound = nullptr;
    std::uint64_t boundGeneration = 0;
    if (!currentBinding(bound, boundGeneration) ||
        !validFixtureWindow(bound, shared->configuration.fixture, boundGeneration))
        InterlockedExchange(&shared->ready, 0);
    if (gesture.owned &&
        !validFixtureWindow(gesture.window, shared->configuration.fixture, gesture.generation)) {
        releaseGesture(shared);
        lastOwnedPointer = 0;
        lastPointerWindow = nullptr;
        InterlockedExchange(&shared->owned, promotedGesture.owned ? 1 : 0);
        setReason(shared, Reason::WindowChanged);
    }
    if (promotedGesture.owned &&
        !validFixtureWindow(promotedGesture.window, shared->configuration.fixture,
                            promotedGesture.generation)) {
        promotedGesture = {};
        InterlockedExchange(&shared->owned, gesture.owned ? 1 : 0);
    }
    if (lastPointerWindow && !validFixtureWindow(lastPointerWindow, shared->configuration.fixture,
                                                 lastPointerGeneration)) {
        lastPointerWindow = nullptr;
        lastOwnedPointer = 0;
    }
}
bool normalizeEmulatedFrame(MSG& message, bool& compatibility) {
    compatibility = false;
    switch (message.message) {
    case emulatedPointerDownMessage:
        message.message = WM_POINTERDOWN;
        return true;
    case emulatedPointerUpMessage:
        message.message = WM_POINTERUP;
        return true;
    case emulatedPointerUpdateMessage:
        message.message = WM_POINTERUPDATE;
        return true;
    case emulatedPointerCaptureLostMessage:
        message.message = WM_POINTERCAPTURECHANGED;
        return true;
    case emulatedCompatibilityDownMessage:
        message.message = WM_LBUTTONDOWN;
        break;
    case emulatedCompatibilityUpMessage:
        message.message = WM_LBUTTONUP;
        break;
    case emulatedCompatibilityMoveMessage:
        message.message = WM_MOUSEMOVE;
        break;
    default:
        return false;
    }
    compatibility = true;
    return true;
}
void terminateEmulatedPointer(SharedSession* shared) {
    const auto window = gesture.window;
    const auto generation = gesture.generation;
    releaseGesture(shared);
    if (promotedGesture.owned && promotedGesture.window == window &&
        promotedGesture.generation == generation)
        promotedGesture = {};
    lastOwnedPointer = 0;
    lastPointerWindow = nullptr;
    InterlockedExchange(&shared->owned, promotedGesture.owned ? 1 : 0);
}
bool processMessage(MSG& message, SharedSession* shared, bool promoted = false) {
    discardDestroyedGesture(shared);
    if (message.message == WM_POINTERCAPTURECHANGED) {
        // Pointer capture loss is terminal: Windows need not deliver any up.
        // Match the original receiving HWND and pointer ID, not lParam (which
        // identifies the new capture window). Real hardware is not enabled by
        // this experiment; this is exercised only by explicit emulated frames.
        if (gesture.owned && gesture.kind == InputKind::EmulatedPointer &&
            message.hwnd == gesture.window &&
            GET_POINTERID_WPARAM(message.wParam) == gesture.pointer) {
            terminateEmulatedPointer(shared);
        }
        return false;
    }
    if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN ||
        message.message == WM_KEYUP || message.message == WM_SYSKEYUP) {
        unsigned bit = 0;
        if (message.wParam == VK_SHIFT || message.wParam == VK_LSHIFT ||
            message.wParam == VK_RSHIFT)
            bit = 1;
        if (message.wParam == VK_CONTROL || message.wParam == VK_LCONTROL ||
            message.wParam == VK_RCONTROL)
            bit = 2;
        if (message.wParam == VK_MENU || message.wParam == VK_LMENU || message.wParam == VK_RMENU)
            bit = 4;
        if (message.wParam == VK_LWIN || message.wParam == VK_RWIN)
            bit = 8;
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            keyboardModifiers |= bit;
            gesture.canceled = true;
        } else {
            keyboardModifiers &= ~bit;
        }
        return false;
    }
    if (message.message == WM_RBUTTONDOWN || message.message == WM_MBUTTONDOWN ||
        message.message == WM_XBUTTONDOWN || message.message == WM_MOUSEWHEEL ||
        message.message == WM_CANCELMODE || message.message == WM_CAPTURECHANGED) {
        gesture.canceled = true;
        return false;
    }
    const bool mouseDown = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK;
    const bool mouseUp = message.message == WM_LBUTTONUP;
    const bool mouseMove = message.message == WM_MOUSEMOVE;
    const bool pointerDown = message.message == WM_POINTERDOWN;
    const bool pointerUp = message.message == WM_POINTERUP;
    const bool pointerMove = message.message == WM_POINTERUPDATE;
    if (!mouseDown && !mouseUp && !mouseMove && !pointerDown && !pointerUp && !pointerMove)
        return false;
    if ((mouseDown || mouseUp || mouseMove) && promoted) {
        const bool pointerOwnedHere = gesture.owned && gesture.kind == InputKind::EmulatedPointer &&
                                      message.hwnd == gesture.window;
        const bool completedHere = lastPointerWindow && message.hwnd == lastPointerWindow &&
                                   GetTickCount64() >= lastOwnedPointer &&
                                   GetTickCount64() - lastOwnedPointer < 500;
        if (mouseDown && !promotedGesture.owned &&
            ((pointerOwnedHere && !gesture.compatibilityUsed) || completedHere)) {
            promotedGesture.owned = true;
            promotedGesture.window = message.hwnd;
            promotedGesture.generation =
                pointerOwnedHere ? gesture.generation : lastPointerGeneration;
            if (pointerOwnedHere)
                gesture.compatibilityUsed = true;
            lastPointerWindow = nullptr;
            lastOwnedPointer = 0; // A pointer sequence grants one compatibility pair.
            InterlockedIncrement64(&shared->promotedDownsOwned);
        }
        if (promotedGesture.owned && message.hwnd == promotedGesture.window) {
            if (mouseUp)
                promotedGesture = {};
            InterlockedExchange(&shared->owned, gesture.owned || promotedGesture.owned ? 1 : 0);
            return true; // Only this target's complete compatibility pair is owned.
        }
        return false; // Never consume an unmatched up/move or another HWND's pair.
    }
    if (pointerDown) {
        lastOwnedPointer = 0;
        lastPointerWindow = nullptr;
    }
    const InputKind kind =
        pointerDown || pointerUp || pointerMove ? InputKind::EmulatedPointer : InputKind::Mouse;
    const UINT pointer =
        kind == InputKind::EmulatedPointer ? GET_POINTERID_WPARAM(message.wParam) : 0;
    POINT point{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
    if (kind == InputKind::EmulatedPointer)
        ScreenToClient(message.hwnd, &point);
    HWND target = nullptr;
    std::uint64_t generation = 0;
    const bool bound = currentBinding(target, generation);
    const bool available = bound && message.hwnd == target && gate(shared, target, generation);
    const bool modified = modifiers(kind == InputKind::Mouse ? message.wParam : 0);
    if (gesture.owned) {
        if (gesture.kind != kind || gesture.pointer != pointer || message.hwnd != gesture.window) {
            if (mouseDown || pointerDown)
                gesture.canceled = true;
            return false;
        }
        if (kind == InputKind::EmulatedPointer && IS_POINTER_CANCELED_WPARAM(message.wParam)) {
            terminateEmulatedPointer(shared);
            return true; // Explicit terminal frame: no later up is required.
        }
        if (!available || modified)
            gesture.canceled = true;
        RECT client{};
        if (!GetClientRect(gesture.window, &client) || !PtInRect(&client, point) ||
            std::abs(point.x - gesture.origin.x) > GetSystemMetrics(SM_CXDRAG) ||
            std::abs(point.y - gesture.origin.y) > GetSystemMetrics(SM_CYDRAG))
            gesture.canceled = true;
        if (mouseUp || pointerUp) {
            const auto completed = gesture;
            const bool dispatch =
                !gesture.canceled && available && GetTickCount64() - gesture.began <= 1500;
            releaseGesture(shared);
            if (kind == InputKind::EmulatedPointer && !completed.compatibilityUsed)
                lastOwnedPointer = GetTickCount64();
            if (kind == InputKind::EmulatedPointer && !completed.compatibilityUsed) {
                lastPointerWindow = completed.window;
                lastPointerGeneration = completed.generation;
            }
            if (dispatch)
                pushEvent(completed, shared);
        }
        return mouseDown || mouseUp || mouseMove || pointerDown || pointerUp || pointerMove;
    }
    if (!mouseDown && !pointerDown)
        return false;
    if (!available || modified || !queueHasRoom())
        return false; // No down ownership means its matching up remains native.
    if (kind == InputKind::EmulatedPointer && (!IS_POINTER_PRIMARY_WPARAM(message.wParam) ||
                                               !IS_POINTER_FIRSTBUTTON_WPARAM(message.wParam) ||
                                               IS_POINTER_CANCELED_WPARAM(message.wParam)))
        return false;
    if (kind == InputKind::Mouse) {
        if (GetCapture() && GetCapture() != target)
            return false;
        SetCapture(target);
        if (GetCapture() != target)
            return false;
    }
    // Cancellation of an earlier/unowned sequence must not poison this new
    // accepted down (including capture-change notifications between pairs).
    gesture = {};
    gesture.owned = true;
    gesture.kind = kind;
    gesture.pointer = pointer;
    gesture.window = target;
    gesture.generation = generation;
    gesture.origin = point;
    gesture.began = GetTickCount64();
    InterlockedExchange(&shared->owned, 1);
    return true;
}

DWORD WINAPI worker(void* moduleParameter) {
    const auto self = static_cast<HMODULE>(moduleParameter);
    const auto name = mappingName(GetCurrentProcessId(), processCreationTime(GetCurrentProcess()));
    Handle mapping(OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str()));
    auto* shared =
        mapping ? static_cast<SharedSession*>(MapViewOfFile(
                      mapping.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedSession)))
                : nullptr;
    Handle release;
    Handle disarm;
    Handle pipe;
    bool configured = false;
    if (shared && readShared(&shared->configured)) {
        const auto& config = shared->configuration;
        ProcessIdentity host{}, controller{};
        configured = config.magic == protocolMagic && config.version == protocolVersion &&
                     config.bytes == sizeof(SessionConfiguration) && config.fixture.threadId != 0 &&
                     (config.capabilities & ~emulatedFramesCapability) == 0 &&
                     config.fixture.path[511] == 0 && config.controller.path[511] == 0 &&
                     config.pipeName[159] == 0 && config.releaseEvent[159] == 0 &&
                     config.disarmEvent[159] == 0 &&
                     readProcessIdentity(GetCurrentProcessId(), config.fixture.threadId, host) &&
                     readProcessIdentity(config.controller.processId, config.controller.threadId,
                                         controller) &&
                     identitiesEqual(host, config.fixture) &&
                     identitiesEqual(controller, config.controller) && allowedFixture(host) &&
                     allowedController(controller) && host.machine == controller.machine &&
                     validFixtureWindow(reinterpret_cast<HWND>(config.window), host,
                                        config.windowGeneration);
        release.reset(OpenEventW(SYNCHRONIZE, FALSE, config.releaseEvent));
        disarm.reset(OpenEventW(SYNCHRONIZE, FALSE, config.disarmEvent));
        configured = configured && release && disarm;
        if (configured)
            runtime.shared.store(shared, std::memory_order_release);
        InterlockedExchange(
            &shared->workerState,
            static_cast<LONG>(configured ? WorkerState::Starting : WorkerState::Failed));
        setReason(shared, configured ? Reason::None : Reason::IdentityMismatch);
        if (configured) {
            pipe.reset(CreateFileW(
                config.pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
            DWORD server = 0, serverSession = 0, ownSession = 0;
            configured = pipe && GetNamedPipeServerProcessId(pipe.get(), &server) &&
                         server == config.controller.processId &&
                         ProcessIdToSessionId(server, &serverSession) &&
                         ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) &&
                         serverSession == ownSession;
            if (configured) {
                DWORD mode = PIPE_READMODE_MESSAGE;
                configured = SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr) != FALSE;
            }
            if (configured) {
                WireMessage hello;
                hello.nonce = config.nonce;
                hello.processId = GetCurrentProcessId();
                hello.threadId = config.fixture.threadId;
                hello.tick = GetTickCount64();
                configured = writeWire(pipe.get(), hello);
            }
        }
        bool connected = configured;
        std::uint64_t lastControl = 0;
        while (release && WaitForSingleObject(release.get(), 5) == WAIT_TIMEOUT) {
            const auto now = GetTickCount64();
            if (connected) {
                WireMessage control;
                const auto result = readWire(pipe.get(), control);
                if (result == ReadResult::Message) {
                    if (!validWire(control, config) || control.type != WireType::Heartbeat ||
                        control.sequence <= lastControl ||
                        control.processId != config.controller.processId || now < control.tick ||
                        now - control.tick > heartbeatLifetimeMs) {
                        connected = false;
                        setReason(shared, Reason::InvalidProtocol);
                    } else {
                        lastControl = control.sequence;
                        InterlockedExchange64(&shared->lastHeartbeat, static_cast<LONG64>(now));
                        const auto window = reinterpret_cast<HWND>(control.window);
                        const bool validBinding =
                            validFixtureWindow(window, config.fixture, control.windowGeneration);
                        bindWindow(shared, validBinding ? window : nullptr,
                                   validBinding ? control.windowGeneration : 0);
                        const bool enabled = control.flags & enabledFlag;
                        InterlockedExchange(&shared->ready,
                                            validBinding && enabled &&
                                                    WaitForSingleObject(disarm.get(), 0) ==
                                                        WAIT_TIMEOUT
                                                ? 1
                                                : 0);
                        InterlockedExchange(&shared->workerState,
                                            static_cast<LONG>(WorkerState::Connected));
                        setReason(shared, !validBinding ? Reason::WindowChanged
                                          : enabled     ? Reason::None
                                                        : Reason::Disabled);
                    }
                } else if (result == ReadResult::Closed || result == ReadResult::Invalid) {
                    connected = false;
                    setReason(shared, Reason::PipeFailure);
                }
            }
            const auto heartbeat = static_cast<ULONGLONG>(readShared(&shared->lastHeartbeat));
            if (!connected || WaitForSingleObject(disarm.get(), 0) == WAIT_OBJECT_0 ||
                now < heartbeat || now - heartbeat > heartbeatLifetimeMs) {
                InterlockedExchange(&shared->ready, 0);
                if (connected && now >= heartbeat && now - heartbeat > heartbeatLifetimeMs)
                    setReason(shared, Reason::HeartbeatExpired);
            }
            HWND window = nullptr;
            std::uint64_t generation = 0;
            if (readShared(&shared->owned) && currentBinding(window, generation) &&
                !validFixtureWindow(window, config.fixture, generation))
                PostThreadMessageW(config.fixture.threadId, WM_NULL, 0, 0);
            WireMessage event;
            while (popEvent(event)) {
                if (!connected || !readShared(&shared->ready) || now < event.tick ||
                    now - event.tick > heartbeatLifetimeMs || !writeWire(pipe.get(), event)) {
                    InterlockedIncrement64(&shared->eventsDropped);
                    if (connected && !readShared(&shared->ready))
                        setReason(shared, Reason::Disabled);
                }
            }
            if (!readShared(&shared->ready) && !readShared(&shared->owned))
                InterlockedExchange(&shared->workerState, static_cast<LONG>(WorkerState::Drained));
        }
        // Only the controller's successful unhook + fixture message-loop barrier
        // can signal release. Controller loss leaves a dormant, pinned module;
        // it never guesses that another thread has left a hook callback.
        InterlockedExchange(&shared->ready, 0);
        while (runtime.inFlight.load(std::memory_order_acquire))
            Sleep(1); // No forced unload through an in-flight CallNextHookEx chain.
        InterlockedExchange(&shared->workerState, static_cast<LONG>(WorkerState::Released));
        runtime.shared.store(nullptr, std::memory_order_release);
        UnmapViewOfFile(shared);
    }
    pipe.reset();
    mapping.reset();
    FreeLibraryAndExitThread(self, 0);
    return 0;
}
void ensureWorker() {
    bool expected = false;
    if (!runtime.started.compare_exchange_strong(expected, true))
        return;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&worker), &self))
        return;
    Handle thread(CreateThread(nullptr, 0, worker, self, 0, nullptr));
    if (!thread)
        FreeLibrary(self);
}
} // namespace
} // namespace xdstarter::bridge_poc

extern "C" __declspec(dllexport) LRESULT CALLBACK
    XDStarterGetMessageBridge(int code, WPARAM removed, LPARAM parameter) {
    using namespace xdstarter::bridge_poc;
    runtime.inFlight.fetch_add(1, std::memory_order_acq_rel);
    if (code == HC_ACTION) {
        ensureWorker();
        if (auto* shared = runtime.shared.load(std::memory_order_acquire)) {
            InterlockedIncrement64(&shared->callbacks);
            if (removed == PM_REMOVE && parameter &&
                GetCurrentThreadId() == shared->configuration.fixture.threadId) {
                auto& message = *reinterpret_cast<MSG*>(parameter);
                const bool physicalPointer =
                    message.message >= WM_POINTERUPDATE && message.message <= WM_POINTERHWHEEL;
                if (physicalPointer)
                    InterlockedIncrement64(&shared->pointerMessagesObserved);
                MSG normalized = message;
                bool compatibility = false;
                const bool emulated = normalizeEmulatedFrame(normalized, compatibility);
                const auto extra = static_cast<ULONG_PTR>(GetMessageExtraInfo());
                const bool physicalCompatibility = (extra & 0xffffff00u) == 0xff515700u;
                bool consumed = false;
                if (!physicalPointer && emulated &&
                    (shared->configuration.capabilities & emulatedFramesCapability)) {
                    InterlockedIncrement64(&shared->emulatedFramesObserved);
                    consumed = processMessage(normalized, shared, compatibility);
                } else if (!physicalPointer && !emulated && !physicalCompatibility) {
                    consumed = processMessage(message, shared);
                }
                // Genuine WM_POINTER and promoted-mouse streams are unsupported
                // and untouched. Explicit WM_APP frames are a separate opt-in
                // controlled-fixture normalization test, never hardware proof.
                if (consumed) {
                    message.message = WM_NULL;
                    message.wParam = 0;
                    message.lParam = 0;
                }
            }
        }
    }
    const auto result = CallNextHookEx(nullptr, code, removed, parameter);
    runtime.inFlight.fetch_sub(1, std::memory_order_release);
    return result;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
