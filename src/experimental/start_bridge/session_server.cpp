// src/experimental/start_bridge/session_server.cpp

#include "experimental/start_bridge/session_server.h"

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace xdstarter::bridge_poc {
namespace {
std::atomic<unsigned> retainedDrains{0};
template <std::size_t Size>
bool copyText(wchar_t (&destination)[Size], const std::wstring& source) {
    if (source.empty() || source.size() >= Size)
        return false;
    std::copy(source.begin(), source.end(), destination);
    destination[source.size()] = 0;
    return true;
}
bool hasVersion(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<unsigned char> data(size);
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT count = 0;
    return size && GetFileVersionInfoW(path.c_str(), 0, size, data.data()) &&
           VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &count) &&
           count >= sizeof(*fixed) && fixed->dwFileVersionMS == imageVersionMs &&
           fixed->dwFileVersionLS == imageVersionLs;
}
} // namespace

SessionServer::~SessionServer() {
    if (!stop(500))
        retireForDrain();
}
unsigned retainedDrainCount() { return retainedDrains.load(); }
void SessionServer::setError(std::wstring value) {
    std::lock_guard lock(mutex_);
    error_ = std::move(value);
}
std::wstring SessionServer::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}
std::wstring SessionServer::sharedMappingName() const {
    return mappingName(configuration_.fixture.processId, configuration_.fixture.creationTime);
}
HWND SessionServer::window() const {
    std::uint64_t generation = 0;
    return findFixtureWindow(configuration_.fixture, generation);
}
bool SessionServer::start(const std::wstring& dllPath, DWORD fixtureProcess, DWORD fixtureThread,
                          ConfigurationFault fault, bool enableEmulatedFrames) {
    if (hook_ || shared_) {
        setError(L"A controlled session is already installed.");
        return false;
    }
    modulePath_ = canonicalPath(dllPath);
    if (_wcsicmp(fileName(modulePath_).c_str(), L"xdstarter_start_bridge_poc.dll") != 0 ||
        !hasVersion(modulePath_) ||
        !readProcessIdentity(GetCurrentProcessId(), GetCurrentThreadId(),
                             configuration_.controller) ||
        !readProcessIdentity(fixtureProcess, fixtureThread, configuration_.fixture) ||
        !allowedController(configuration_.controller) || !allowedFixture(configuration_.fixture) ||
        configuration_.controller.machine != configuration_.fixture.machine) {
        setError(L"Unsupported target/module/version/architecture. Explorer InputSite adaptation "
                 L"is not implemented.");
        return false;
    }
    std::uint64_t generation = 0;
    const HWND target = findFixtureWindow(configuration_.fixture, generation);
    if (!target) {
        setError(L"A unique controlled InputSite with the expected GUI thread is required.");
        return false;
    }
    configuration_.window = reinterpret_cast<std::uint64_t>(target);
    configuration_.windowGeneration = generation;
    configuration_.capabilities = enableEmulatedFrames ? emulatedFramesCapability : 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(configuration_.nonce.data()),
                        static_cast<ULONG>(sizeof(configuration_.nonce)),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        setError(L"Session randomness unavailable.");
        return false;
    }
    const std::wstring suffix = std::to_wstring(configuration_.controller.processId) + L"." +
                                std::to_wstring(configuration_.nonce[0]) + L"." +
                                std::to_wstring(configuration_.nonce[1]);
    if (!copyText(configuration_.pipeName, L"\\\\.\\pipe\\XDStarter.BridgePoC52." + suffix) ||
        !copyText(configuration_.disarmEvent, L"Local\\XDStarter.BridgePoC52.Disarm." + suffix) ||
        !copyText(configuration_.releaseEvent, L"Local\\XDStarter.BridgePoC52.Release." + suffix) ||
        !copyText(configuration_.barrierEvent, L"Local\\XDStarter.BridgePoC52.Barrier." + suffix))
        return false;
    LocalSecurity security;
    if (!security.get()) {
        setError(L"Cannot create current-user-only IPC security descriptor.");
        return false;
    }
    mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(), PAGE_READWRITE, 0,
                                      sizeof(SharedSession), sharedMappingName().c_str()));
    const bool existingMapping = GetLastError() == ERROR_ALREADY_EXISTS;
    if (!mapping_ || existingMapping) {
        setError(L"Session mapping unavailable or already present; refusing replacement.");
        closeLocal();
        return false;
    }
    shared_ = static_cast<SharedSession*>(
        MapViewOfFile(mapping_.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedSession)));
    disarm_.reset(CreateEventW(security.get(), TRUE, FALSE, configuration_.disarmEvent));
    release_.reset(CreateEventW(security.get(), TRUE, FALSE, configuration_.releaseEvent));
    barrier_.reset(CreateEventW(security.get(), TRUE, FALSE, configuration_.barrierEvent));
    fixtureProcess_.reset(
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fixtureProcess));
    pipe_.reset(CreateNamedPipeW(
        configuration_.pipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096,
        4096, 0, security.get()));
    if (!shared_ || !disarm_ || !release_ || !barrier_ || !fixtureProcess_ || !pipe_) {
        setError(L"Cannot create bounded local IPC session.");
        closeLocal();
        return false;
    }
    std::memset(shared_, 0, sizeof(*shared_));
    shared_->configuration = configuration_;
    // Controlled fault cases exercise the DLL's independent validation, not
    // only this controller's pre-install checks. No production target uses it.
    if (fault == ConfigurationFault::MissingReleaseEvent)
        wcscpy_s(shared_->configuration.releaseEvent,
                 L"Local\\XDStarter.BridgePoC52.DeliberatelyMissingRelease");
    else if (fault == ConfigurationFault::InvalidImageVersion)
        ++shared_->configuration.fixture.versionLs;
    else if (fault == ConfigurationFault::ArchitectureMismatch)
        shared_->configuration.fixture.machine ^= 1;
    InterlockedExchange(&shared_->configured, 1);
    localModule_ =
        LoadLibraryExW(modulePath_.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto procedure =
        localModule_
            ? reinterpret_cast<HOOKPROC>(GetProcAddress(localModule_, "XDStarterGetMessageBridge"))
            : nullptr;
    if (!procedure) {
        setError(L"Controlled bridge DLL export unavailable.");
        closeLocal();
        return false;
    }
    closing_ = false;
    enabled_ = true;
    paused_ = false;
    corruptNonce_ = false;
    disconnect_ = false;
    connected_ = false;
    toggles_ = 0;
    pointerToggles_ = 0;
    rejectedFrames_ = 0;
    thread_ = std::thread([this] { serve(); });
    hook_ = SetWindowsHookExW(WH_GETMESSAGE, procedure, localModule_, fixtureThread);
    if (!hook_) {
        setError(L"The documented thread-specific queue hook could not be installed.");
        closeLocal();
        return false;
    }
    PostThreadMessageW(fixtureThread, WM_NULL, 0, 0);
    return true;
}

void SessionServer::serve() {
    auto* connecting = new OVERLAPPED{};
    connecting->hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!connecting->hEvent) {
        delete connecting;
        return;
    }
    const BOOL immediate = ConnectNamedPipe(pipe_.get(), connecting);
    const DWORD connectError = immediate ? ERROR_SUCCESS : GetLastError();
    bool accepted = immediate || connectError == ERROR_PIPE_CONNECTED;
    const auto began = GetTickCount64();
    if (!accepted && connectError == ERROR_IO_PENDING) {
        while (!closing_ && GetTickCount64() - began < 5000) {
            if (WaitForSingleObject(connecting->hEvent, 20) == WAIT_OBJECT_0) {
                DWORD count = 0;
                accepted = GetOverlappedResult(pipe_.get(), connecting, &count, FALSE) != FALSE;
                break;
            }
        }
        if (!accepted) {
            CancelIoEx(pipe_.get(), connecting);
            if (WaitForSingleObject(connecting->hEvent, 200) != WAIT_OBJECT_0)
                return; // Retain kernel-owned OVERLAPPED/event on uncertain cancellation.
        }
    }
    CloseHandle(connecting->hEvent);
    delete connecting;
    DWORD client = 0, clientSession = 0, ownSession = 0;
    if (!accepted || !GetNamedPipeClientProcessId(pipe_.get(), &client) ||
        client != configuration_.fixture.processId ||
        !ProcessIdToSessionId(client, &clientSession) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) || clientSession != ownSession) {
        if (!closing_)
            setError(L"IPC client identity/session did not match the controlled fixture.");
        return;
    }
    bool hello = false;
    std::uint64_t controlSequence = 0;
    std::uint64_t lastEvent = 0;
    ULONGLONG sentAt = 0;
    while (!closing_) {
        if (disconnect_) {
            DisconnectNamedPipe(pipe_.get());
            connected_ = false;
            return;
        }
        WireMessage incoming;
        const auto read = readWire(pipe_.get(), incoming);
        if (read == ReadResult::Message) {
            const auto now = GetTickCount64();
            const bool valid = validWire(incoming, configuration_) &&
                               incoming.processId == configuration_.fixture.processId &&
                               incoming.threadId == configuration_.fixture.threadId &&
                               now >= incoming.tick && now - incoming.tick <= heartbeatLifetimeMs;
            if (!valid) {
                ++rejectedFrames_;
                setError(L"Rejected stale or incorrectly bound IPC frame.");
                break;
            }
            if (incoming.type == WireType::Hello && !hello) {
                hello = true;
                connected_ = true;
            } else if (incoming.type == WireType::Toggle && hello &&
                       incoming.sequence > lastEvent &&
                       inputKindAllowed(incoming.inputKind, configuration_.capabilities) &&
                       validFixtureWindow(reinterpret_cast<HWND>(incoming.window),
                                          configuration_.fixture, incoming.windowGeneration)) {
                lastEvent = incoming.sequence;
                ++toggles_;
                if (incoming.inputKind == InputKind::EmulatedPointer)
                    ++pointerToggles_;
            } else {
                ++rejectedFrames_;
            }
        } else if (read == ReadResult::Closed || read == ReadResult::Invalid) {
            connected_ = false;
            break;
        }
        const auto now = GetTickCount64();
        if (hello && !paused_ && now - sentAt >= heartbeatPeriodMs) {
            WireMessage heartbeat;
            heartbeat.type = WireType::Heartbeat;
            heartbeat.nonce = configuration_.nonce;
            if (corruptNonce_)
                ++heartbeat.nonce[0];
            heartbeat.sequence = ++controlSequence;
            heartbeat.tick = now;
            heartbeat.processId = configuration_.controller.processId;
            heartbeat.threadId = configuration_.controller.threadId;
            heartbeat.flags = enabled_ ? enabledFlag : 0;
            std::uint64_t generation = 0;
            heartbeat.window = reinterpret_cast<std::uint64_t>(
                findFixtureWindow(configuration_.fixture, generation));
            heartbeat.windowGeneration = generation;
            if (!writeWire(pipe_.get(), heartbeat)) {
                connected_ = false;
                break;
            }
            sentAt = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    connected_ = false;
}

void SessionServer::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (disarm_) {
        if (enabled)
            ResetEvent(disarm_.get());
        else
            SetEvent(disarm_.get());
    }
}
SessionObservation SessionServer::observe() const {
    SessionObservation observation;
    observation.connected = connected_;
    observation.toggles = toggles_;
    observation.pointerToggles = pointerToggles_;
    observation.rejectedFrames = rejectedFrames_;
    if (shared_) {
        observation.ready = readShared(&shared_->ready) != 0;
        observation.ownsPress = readShared(&shared_->owned) != 0;
        observation.worker = static_cast<WorkerState>(readShared(&shared_->workerState));
        observation.reason = static_cast<Reason>(readShared(&shared_->reason));
        observation.callbacks = readShared(&shared_->callbacks);
        observation.queued = readShared(&shared_->eventsQueued);
        observation.dropped = readShared(&shared_->eventsDropped);
        observation.promotedDownsOwned = readShared(&shared_->promotedDownsOwned);
        observation.pointerMessagesObserved = readShared(&shared_->pointerMessagesObserved);
        observation.emulatedFramesObserved = readShared(&shared_->emulatedFramesObserved);
    }
    return observation;
}
bool SessionServer::stop(DWORD timeoutMs) {
    if (!shared_ && !hook_) {
        closeLocal();
        return true;
    }
    setEnabled(false);
    const auto began = GetTickCount64();
    const auto dead = [this] {
        return fixtureProcess_ && WaitForSingleObject(fixtureProcess_.get(), 0) == WAIT_OBJECT_0;
    };
    while (shared_ && (readShared(&shared_->ready) || readShared(&shared_->owned)) && !dead()) {
        if (GetTickCount64() - began >= timeoutMs) {
            setError(L"Owned release has not drained; hook and pinned remote module are retained.");
            return false;
        }
        Sleep(5);
    }
    if (hook_) {
        if (!UnhookWindowsHookEx(hook_)) {
            setError(L"Unhook was not confirmed; refusing remote DLL release.");
            return false;
        }
        hook_ = nullptr;
    }
    if (!dead() && shared_) {
        ResetEvent(barrier_.get());
        if (!PostThreadMessageW(configuration_.fixture.threadId, fixtureBarrierMessage,
                                static_cast<WPARAM>(configuration_.nonce[0]),
                                static_cast<LPARAM>(configuration_.nonce[1])) ||
            WaitForSingleObject(barrier_.get(), std::min<DWORD>(timeoutMs, 1000)) !=
                WAIT_OBJECT_0) {
            setError(
                L"Fixture queue barrier unavailable; remote module stays pinned and disarmed.");
            return false;
        }
        SetEvent(release_.get());
        const auto unloadBegan = GetTickCount64();
        while (modulePresent(configuration_.fixture.processId, modulePath_) && !dead()) {
            if (GetTickCount64() - unloadBegan >= timeoutMs) {
                setError(L"Remote module absence was not confirmed.");
                return false;
            }
            Sleep(5);
        }
    }
    closeLocal();
    return true;
}
void SessionServer::closeLocal() {
    closing_ = true;
    connected_ = false;
    if (pipe_)
        CancelIoEx(pipe_.get(), nullptr);
    if (thread_.joinable())
        thread_.join();
    pipe_.reset();
    if (localModule_) {
        FreeLibrary(localModule_);
        localModule_ = nullptr;
    }
    if (shared_) {
        UnmapViewOfFile(shared_);
        shared_ = nullptr;
    }
    mapping_.reset();
    disarm_.reset();
    release_.reset();
    barrier_.reset();
    fixtureProcess_.reset();
}
void SessionServer::retireForDrain() {
    // Destruction cannot turn an owned down into an isolated native up. Move
    // the disarmed hook/session into a self-contained drain custodian instead
    // of unhooking it or retaining references to this destroyed controller.
    setEnabled(false);
    closing_ = true;
    if (pipe_)
        CancelIoEx(pipe_.get(), nullptr);
    if (thread_.joinable())
        thread_.join();
    pipe_.reset();
    struct Retired {
        SessionConfiguration configuration;
        Handle mapping, disarm, release, barrier, process;
        SharedSession* shared = nullptr;
        HMODULE module = nullptr;
        HHOOK hook = nullptr;
        std::wstring path;
    };
    auto* retired = new Retired{configuration_,
                                std::move(mapping_),
                                std::move(disarm_),
                                std::move(release_),
                                std::move(barrier_),
                                std::move(fixtureProcess_),
                                shared_,
                                localModule_,
                                hook_,
                                modulePath_};
    shared_ = nullptr;
    localModule_ = nullptr;
    hook_ = nullptr;
    ++retainedDrains;
    Handle custodian(CreateThread(
        nullptr, 0,
        [](void* parameter) -> DWORD {
            auto* state = static_cast<Retired*>(parameter);
            const auto dead = [&] {
                return state->process &&
                       WaitForSingleObject(state->process.get(), 0) == WAIT_OBJECT_0;
            };
            while (state->shared &&
                   (readShared(&state->shared->ready) || readShared(&state->shared->owned)) &&
                   !dead())
                Sleep(5);
            const bool unhooked = !state->hook || UnhookWindowsHookEx(state->hook) != FALSE;
            bool quiescent = dead();
            if (unhooked && !quiescent && state->shared && state->barrier) {
                ResetEvent(state->barrier.get());
                quiescent =
                    PostThreadMessageW(state->configuration.fixture.threadId, fixtureBarrierMessage,
                                       static_cast<WPARAM>(state->configuration.nonce[0]),
                                       static_cast<LPARAM>(state->configuration.nonce[1])) &&
                    WaitForSingleObject(state->barrier.get(), 1000) == WAIT_OBJECT_0;
            }
            if (unhooked && quiescent && state->release)
                SetEvent(state->release.get());
            if (state->shared)
                UnmapViewOfFile(state->shared);
            if (state->module)
                FreeLibrary(state->module);
            delete state;
            --retainedDrains;
            return 0;
        },
        retired, 0, nullptr));
    if (!custodian)
        OutputDebugStringW(
            L"Bridge PoC drain custodian unavailable; disarmed hook/module retained safely.\n");
    // If no release arrives, the module stays pinned. A controller process
    // crash while hardware is held is an abnormal-exit boundary, not a normal
    // drain/unload guarantee; the integration test verifies the live custodian.
}

} // namespace xdstarter::bridge_poc
