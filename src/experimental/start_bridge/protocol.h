// src/experimental/start_bridge/protocol.h

#pragma once

#ifndef _WIN32
#error The input bridge experiment is Windows-only.
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace xdstarter::bridge_poc {

constexpr std::uint32_t protocolMagic = 0x35324258;
constexpr std::uint32_t protocolVersion = 1;
constexpr DWORD imageVersionMs = 0x00010000;
constexpr DWORD imageVersionLs = 52;
constexpr DWORD heartbeatLifetimeMs = 300;
constexpr DWORD heartbeatPeriodMs = 40;
constexpr std::size_t queueCapacity = 16;
constexpr wchar_t fixtureClass[] = L"XDStarter.ControlledInputSite52";
constexpr wchar_t generationProperty[] = L"XDStarter.BridgePoC52.Generation";
constexpr wchar_t nativeMouseDownProperty[] = L"XDStarter.BridgePoC52.MouseDown";
constexpr wchar_t nativeMouseUpProperty[] = L"XDStarter.BridgePoC52.MouseUp";
constexpr wchar_t nativePointerDownProperty[] = L"XDStarter.BridgePoC52.PointerDown";
constexpr wchar_t nativePointerUpProperty[] = L"XDStarter.BridgePoC52.PointerUp";
constexpr wchar_t nativeEmulatedPointerDownProperty[] =
    L"XDStarter.BridgePoC52.EmulatedPointerDown";
constexpr wchar_t nativeEmulatedPointerUpProperty[] = L"XDStarter.BridgePoC52.EmulatedPointerUp";
constexpr wchar_t nativeEmulatedCaptureLostProperty[] =
    L"XDStarter.BridgePoC52.EmulatedCaptureLost";
constexpr wchar_t nativeEmulatedCompatibilityDownProperty[] =
    L"XDStarter.BridgePoC52.EmulatedCompatibilityDown";
constexpr wchar_t nativeEmulatedCompatibilityUpProperty[] =
    L"XDStarter.BridgePoC52.EmulatedCompatibilityUp";
constexpr wchar_t nativeRightProperty[] = L"XDStarter.BridgePoC52.RightDown";
constexpr wchar_t peekUnmodifiedProperty[] = L"XDStarter.BridgePoC52.PeekUnmodified";
constexpr wchar_t otherWindowProperty[] = L"XDStarter.BridgePoC52.OtherWindow";
constexpr wchar_t duplicateWindowProperty[] = L"XDStarter.BridgePoC52.DuplicateWindow";
constexpr wchar_t nativePromotedProperty[] = L"XDStarter.BridgePoC52.NativePromotedDown";
constexpr wchar_t pointerPostsProperty[] = L"XDStarter.BridgePoC52.PointerPosts";
constexpr wchar_t pointerPostsSucceededProperty[] = L"XDStarter.BridgePoC52.PointerPostSuccess";
constexpr wchar_t pointerPostErrorProperty[] = L"XDStarter.BridgePoC52.PointerPostError";
constexpr wchar_t lastExtraInformationProperty[] = L"XDStarter.BridgePoC52.LastExtraInformation";
constexpr UINT fixtureCommandMessage = WM_APP + 52;
constexpr UINT fixtureBarrierMessage = WM_APP + 53;
// Explicit controlled-fixture frames, never represented as hardware WM_POINTER
// or as an OS-generated promoted mouse signature. Disabled unless opted in.
constexpr UINT emulatedPointerDownMessage = WM_APP + 55;
constexpr UINT emulatedPointerUpMessage = WM_APP + 56;
constexpr UINT emulatedPointerUpdateMessage = WM_APP + 57;
constexpr UINT emulatedPointerCaptureLostMessage = WM_APP + 58;
constexpr UINT emulatedCompatibilityDownMessage = WM_APP + 59;
constexpr UINT emulatedCompatibilityUpMessage = WM_APP + 60;
constexpr UINT emulatedCompatibilityMoveMessage = WM_APP + 61;
constexpr std::uint32_t emulatedFramesCapability = 1;

enum class FixtureCommand : WPARAM {
    MousePair = 1,
    MouseDown,
    MouseUp,
    ModifiedMousePair,
    RightPair,
    EmulatedPointerPair,
    EmulatedPointerDown,
    EmulatedPointerUp,
    Recreate,
    Exit,
    MouseDragPair,
    KeyboardCancel,
    PeekMousePair,
    OtherEmulatedCompatibilityPair,
    EmulatedCompatibilityDown,
    EmulatedCompatibilityUp,
    DuplicateWindow,
    CloseDuplicateWindow,
    PointerTransportProbe,
    EmulatedPointerCaptureLost,
    OtherEmulatedPointerCaptureLost,
    WrongIdEmulatedPointerCaptureLost,
    OtherMousePair,
    OtherEmulatedPointerUp,
    EmulatedPointerCanceled,
};

struct ProcessIdentity {
    DWORD processId = 0;
    DWORD threadId = 0;
    USHORT machine = 0;
    USHORT reserved = 0;
    ULONGLONG creationTime = 0;
    DWORD versionMs = 0;
    DWORD versionLs = 0;
    wchar_t path[512]{};
    std::array<unsigned char, 32> digest{};
};

struct SessionConfiguration {
    std::uint32_t magic = protocolMagic;
    std::uint32_t version = protocolVersion;
    std::uint32_t bytes = sizeof(SessionConfiguration);
    std::uint32_t capabilities = 0;
    std::array<std::uint64_t, 2> nonce{};
    ProcessIdentity controller;
    ProcessIdentity fixture;
    std::uint64_t window = 0;
    std::uint64_t windowGeneration = 0;
    wchar_t pipeName[160]{};
    wchar_t disarmEvent[160]{};
    wchar_t releaseEvent[160]{};
    wchar_t barrierEvent[160]{};
};

enum class WorkerState : LONG { NotStarted, Starting, Connected, Drained, Released, Failed };
enum class Reason : LONG {
    None,
    InvalidConfiguration,
    IdentityMismatch,
    PipeFailure,
    InvalidProtocol,
    HeartbeatExpired,
    Disabled,
    QueueFull,
    WindowChanged,
};

// Interlocked operations, rather than process-local locks, publish this small
// diagnostic/control block. The fixed configuration is written only once.
struct SharedSession {
    SessionConfiguration configuration;
    volatile LONG configured = 0;
    volatile LONG workerState = static_cast<LONG>(WorkerState::NotStarted);
    volatile LONG ready = 0;
    volatile LONG owned = 0;
    volatile LONG reason = static_cast<LONG>(Reason::None);
    alignas(8) volatile LONG64 callbacks = 0;
    alignas(8) volatile LONG64 lastHeartbeat = 0;
    alignas(8) volatile LONG64 eventsQueued = 0;
    alignas(8) volatile LONG64 eventsDropped = 0;
    alignas(8) volatile LONG64 promotedDownsOwned = 0;
    alignas(8) volatile LONG64 pointerMessagesObserved = 0;
    alignas(8) volatile LONG64 emulatedFramesObserved = 0;
};

enum class WireType : std::uint32_t { Hello = 1, Heartbeat, Toggle };
constexpr std::uint32_t enabledFlag = 1;
enum class InputKind : std::uint32_t { Mouse = 1, EmulatedPointer = 2 };
inline bool inputKindAllowed(InputKind kind, std::uint32_t capabilities) {
    return kind == InputKind::Mouse ||
           (kind == InputKind::EmulatedPointer && (capabilities & emulatedFramesCapability));
}

struct WireMessage {
    std::uint32_t magic = protocolMagic;
    std::uint32_t version = protocolVersion;
    std::uint32_t bytes = sizeof(WireMessage);
    WireType type = WireType::Hello;
    std::array<std::uint64_t, 2> nonce{};
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::uint64_t window = 0;
    std::uint64_t windowGeneration = 0;
    DWORD processId = 0;
    DWORD threadId = 0;
    std::uint32_t flags = 0;
    InputKind inputKind = InputKind::Mouse;
};

static_assert(std::is_trivially_copyable_v<SessionConfiguration>);
static_assert(std::is_trivially_copyable_v<WireMessage>);
static_assert(sizeof(WireMessage) <= 128);

inline LONG readShared(volatile LONG* value) { return InterlockedCompareExchange(value, 0, 0); }
inline LONG64 readShared(volatile LONG64* value) {
    return InterlockedCompareExchange64(value, 0, 0);
}

} // namespace xdstarter::bridge_poc
