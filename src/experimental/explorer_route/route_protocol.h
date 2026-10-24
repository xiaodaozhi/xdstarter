// src/experimental/explorer_route/route_protocol.h

#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <cwchar>

namespace xdstarter::route_probe {
constexpr DWORD magic = 0x52323558;
constexpr size_t capacity = 2048;
constexpr size_t maxThreads = 8;
constexpr wchar_t fixtureClass[] = L"XDStarter.RouteFixture52";
constexpr wchar_t downProperty[] = L"XDStarter.Route52.Down";
constexpr wchar_t upProperty[] = L"XDStarter.Route52.Up";
constexpr UINT fixtureTest = WM_APP + 714;

struct alignas(8) Entry {
    volatile LONG64 sequence = 0;
    volatile LONG64 at = 0, window = 0, extra = 0;
    volatile LONG message = 0, queueTime = 0, x = 0, y = 0, promoted = 0, source = 0;
    volatile LONG pointerId = 0, pointerFlags = 0;
};

struct alignas(8) Shared {
    DWORD signature = magic, version = 1, bytes = sizeof(Shared);
    DWORD process = 0, thread = 0, controller = 0, fixture = 0;
    ULONGLONG processCreated = 0, deadline = 0;
    std::uint64_t taskbar = 0;
    std::array<std::uint64_t, 2> nonce{};
    volatile LONG armed = 0, initialized = 0, callbacks = 0, peeks = 0, inFlight = 0;
    volatile LONG reentrantDropped = 0;
    volatile LONG64 sequence = 0, heartbeat = 0, initDurationMs = 0;
    std::array<Entry, capacity> entries;
};

inline ULONGLONG created(HANDLE process) {
    FILETIME start{}, stop{}, kernel{}, user{};
    return GetProcessTimes(process, &start, &stop, &kernel, &user)
               ? (ULONGLONG(start.dwHighDateTime) << 32) | start.dwLowDateTime
               : 0;
}

inline void mappingName(wchar_t (&name)[160], DWORD process, ULONGLONG start, DWORD thread) {
    swprintf_s(name, L"Local\\XDStarter.Route52.%lu.%llu.%lu", process, start, thread);
}

inline LONG64 read(volatile LONG64* value) { return InterlockedCompareExchange64(value, 0, 0); }
inline LONG read(volatile LONG* value) { return InterlockedCompareExchange(value, 0, 0); }

inline bool interesting(UINT message) {
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
           message == WM_RBUTTONUP || message == WM_MOUSEMOVE || message == WM_MOUSEACTIVATE ||
           message == WM_CAPTURECHANGED || message == WM_CANCELMODE ||
           (message >= WM_POINTERUPDATE && message <= WM_POINTERHWHEEL);
}
} // namespace xdstarter::route_probe
