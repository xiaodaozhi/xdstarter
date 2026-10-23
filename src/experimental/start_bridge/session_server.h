// src/experimental/start_bridge/session_server.h

#pragma once

#include "experimental/start_bridge/win_support.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace xdstarter::bridge_poc {

struct SessionObservation {
    bool ready = false;
    bool ownsPress = false;
    bool connected = false;
    WorkerState worker = WorkerState::NotStarted;
    Reason reason = Reason::None;
    std::uint64_t callbacks = 0;
    std::uint64_t queued = 0;
    std::uint64_t dropped = 0;
    std::uint64_t toggles = 0;
    std::uint64_t pointerToggles = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t promotedDownsOwned = 0;
    std::uint64_t pointerMessagesObserved = 0;
    std::uint64_t emulatedFramesObserved = 0;
};
unsigned retainedDrainCount();
enum class ConfigurationFault {
    None,
    MissingReleaseEvent,
    InvalidImageVersion,
    ArchitectureMismatch
};

class SessionServer {
  public:
    SessionServer() = default;
    ~SessionServer();
    SessionServer(const SessionServer&) = delete;
    SessionServer& operator=(const SessionServer&) = delete;
    bool start(const std::wstring& dllPath, DWORD fixtureProcess, DWORD fixtureThread,
               ConfigurationFault fault = ConfigurationFault::None,
               bool enableEmulatedFrames = false);
    bool stop(DWORD timeoutMs = 1500);
    void setEnabled(bool enabled);
    void pauseHeartbeats(bool paused) { paused_ = paused; }
    void sendInvalidNonce(bool invalid) { corruptNonce_ = invalid; }
    void disconnectPeer() { disconnect_ = true; }
    SessionObservation observe() const;
    std::wstring error() const;
    const ProcessIdentity& fixtureIdentity() const { return configuration_.fixture; }
    HWND window() const;
    std::wstring sharedMappingName() const;

  private:
    void serve();
    void setError(std::wstring error);
    void closeLocal();
    void retireForDrain();
    SessionConfiguration configuration_{};
    Handle mapping_;
    Handle pipe_;
    Handle disarm_;
    Handle release_;
    Handle barrier_;
    Handle fixtureProcess_;
    SharedSession* shared_ = nullptr;
    HMODULE localModule_ = nullptr;
    HHOOK hook_ = nullptr;
    std::wstring modulePath_;
    std::thread thread_;
    std::atomic<bool> closing_{false};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> paused_{false};
    std::atomic<bool> corruptNonce_{false};
    std::atomic<bool> disconnect_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint64_t> toggles_{0};
    std::atomic<std::uint64_t> pointerToggles_{0};
    std::atomic<std::uint64_t> rejectedFrames_{0};
    mutable std::mutex mutex_;
    std::wstring error_;
};

} // namespace xdstarter::bridge_poc
