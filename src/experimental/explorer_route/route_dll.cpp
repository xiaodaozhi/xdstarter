// src/experimental/explorer_route/route_dll.cpp

#include "route_protocol.h"

namespace xdstarter::route_probe {
namespace {
struct Runtime {
    volatile LONG thread = 0;
    HANDLE mapping = nullptr;
    Shared* shared = nullptr;
    std::array<ULONGLONG, 2> lastMotion{};
    bool attempted = false;
    bool recording = false;
};
std::array<Runtime, maxThreads> runtimes;

Runtime* local() {
    const auto thread = GetCurrentThreadId();
    for (auto& runtime : runtimes) {
        const auto previous = InterlockedCompareExchange(&runtime.thread, LONG(thread), 0);
        if (!previous || DWORD(previous) == thread)
            return &runtime;
    }
    return nullptr;
}

bool imageAllowed(bool fixture) {
    wchar_t image[512]{}, windows[512]{}, expected[512]{};
    const auto length = GetModuleFileNameW(nullptr, image, 512);
    if (!length || length >= 512)
        return false;
    const auto* filename = wcsrchr(image, L'\\');
    if (fixture)
        return filename && _wcsicmp(filename + 1, L"xdstarter_route_fixture.exe") == 0;
    const auto windowsLength = GetWindowsDirectoryW(windows, 512);
    if (!windowsLength || windowsLength >= 480)
        return false;
    swprintf_s(expected, L"%s\\explorer.exe", windows);
    return _wcsicmp(image, expected) == 0;
}

Shared* initialize(Runtime& runtime) {
    if (runtime.attempted)
        return runtime.shared;
    runtime.attempted = true;
    const auto began = GetTickCount64();
    wchar_t name[160]{};
    const auto start = created(GetCurrentProcess());
    mappingName(name, GetCurrentProcessId(), start, GetCurrentThreadId());
    runtime.mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    auto* shared = runtime.mapping
                       ? static_cast<Shared*>(MapViewOfFile(
                             runtime.mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Shared)))
                       : nullptr;
    if (!shared)
        return nullptr;
    DWORD owner = 0;
    const auto bar = reinterpret_cast<HWND>(shared->taskbar);
    GetWindowThreadProcessId(bar, &owner);
    wchar_t windowClass[128]{};
    GetClassNameW(bar, windowClass, 128);
    const bool classAllowed = shared->fixture
                                  ? wcscmp(windowClass, fixtureClass) == 0
                                  : wcscmp(windowClass, L"Shell_TrayWnd") == 0 ||
                                        wcscmp(windowClass, L"Shell_SecondaryTrayWnd") == 0;
    if (shared->signature != magic || shared->version != 1 || shared->bytes != sizeof(Shared) ||
        shared->process != GetCurrentProcessId() || shared->processCreated != start ||
        shared->thread != GetCurrentThreadId() || owner != GetCurrentProcessId() ||
        !shared->nonce[0] || !shared->nonce[1] || !classAllowed ||
        !imageAllowed(shared->fixture != 0) || shared->deadline < began ||
        shared->deadline - began > 125000) {
        UnmapViewOfFile(shared);
        return nullptr;
    }
    runtime.shared = shared;
    InterlockedExchange64(&shared->initDurationMs, GetTickCount64() - began);
    InterlockedExchange(&shared->initialized, 1);
    return shared;
}
void observe(int code, WPARAM removed, const MSG* message, int source) {
    if (code == HC_ACTION) {
        if (auto* runtime = local()) {
            if (auto* shared = initialize(*runtime)) {
                InterlockedIncrement(&shared->callbacks);
                if (runtime->recording) {
                    InterlockedIncrement(&shared->reentrantDropped);
                    return;
                }
                runtime->recording = true;
                InterlockedIncrement(&shared->inFlight);
                const auto now = GetTickCount64();
                const auto pulse = ULONGLONG(read(&shared->heartbeat));
                if (source == 1 && removed == PM_NOREMOVE)
                    InterlockedIncrement(&shared->peeks);
                if (removed == PM_REMOVE && message && read(&shared->armed) &&
                    now <= shared->deadline && now >= pulse && now - pulse <= 500) {
                    // MSG is const throughout. Return value and the full hook
                    // chain are preserved for every message, including pointers.
                    const auto bar = reinterpret_cast<HWND>(shared->taskbar);
                    const bool ownedWindow =
                        message->hwnd && (message->hwnd == bar || IsChild(bar, message->hwnd));
                    const bool motion =
                        message->message == WM_MOUSEMOVE || message->message == WM_POINTERUPDATE;
                    auto& lastMotion = runtime->lastMotion[source - 1];
                    if (ownedWindow && interesting(message->message) &&
                        (!motion || now - lastMotion >= 50)) {
                        if (motion)
                            lastMotion = now;
                        const auto sequence = read(&shared->sequence) + 1;
                        auto& slot = shared->entries[(sequence - 1) % capacity];
                        InterlockedExchange64(&slot.sequence, 0);
                        InterlockedExchange64(&slot.at, now);
                        InterlockedExchange64(&slot.window,
                                              reinterpret_cast<LONG64>(message->hwnd));
                        const auto extra = GetMessageExtraInfo();
                        InterlockedExchange64(&slot.extra, extra);
                        InterlockedExchange(&slot.message, LONG(message->message));
                        InterlockedExchange(&slot.queueTime, LONG(message->time));
                        InterlockedExchange(&slot.x, message->pt.x);
                        InterlockedExchange(&slot.y, message->pt.y);
                        InterlockedExchange(&slot.source, source);
                        InterlockedExchange(&slot.pointerId, LOWORD(message->wParam));
                        InterlockedExchange(&slot.pointerFlags, HIWORD(message->wParam));
                        InterlockedExchange(&slot.promoted,
                                            (ULONG_PTR(extra) & 0xffffff00u) == 0xff515700u);
                        InterlockedExchange64(&slot.sequence, sequence);
                        InterlockedExchange64(&shared->sequence, sequence);
                    }
                }
                InterlockedDecrement(&shared->inFlight);
                runtime->recording = false;
            }
        }
    }
}
} // namespace
} // namespace xdstarter::route_probe

extern "C" __declspec(dllexport) LRESULT CALLBACK
    XDStarterObserveQueue(int code, WPARAM removed, LPARAM parameter) {
    xdstarter::route_probe::observe(
        code, removed, parameter ? reinterpret_cast<const MSG*>(parameter) : nullptr, 1);
    return CallNextHookEx(nullptr, code, removed, parameter);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK
    XDStarterObserveWindowProcedure(int code, WPARAM currentThread, LPARAM parameter) {
    if (code == HC_ACTION && parameter) {
        const auto& original = *reinterpret_cast<const CWPSTRUCT*>(parameter);
        MSG message{};
        message.hwnd = original.hwnd;
        message.message = original.message;
        message.wParam = original.wParam;
        xdstarter::route_probe::observe(code, PM_REMOVE, &message, 2);
    }
    return CallNextHookEx(nullptr, code, currentThread, parameter);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    if (reason == DLL_PROCESS_DETACH) {
        for (auto& runtime : xdstarter::route_probe::runtimes) {
            if (runtime.shared)
                UnmapViewOfFile(runtime.shared);
            if (runtime.mapping)
                CloseHandle(runtime.mapping);
        }
    }
    return TRUE;
}
