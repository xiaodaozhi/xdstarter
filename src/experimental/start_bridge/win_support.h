// src/experimental/start_bridge/win_support.h

#pragma once

#include "experimental/start_bridge/protocol.h"

#include <string>

namespace xdstarter::bridge_poc {

class Handle {
  public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE release() {
        const auto result = value_;
        value_ = nullptr;
        return result;
    }
    void reset(HANDLE value = nullptr) {
        if (*this)
            CloseHandle(value_);
        value_ = value;
    }

  private:
    HANDLE value_ = nullptr;
};

class LocalSecurity {
  public:
    LocalSecurity();
    ~LocalSecurity();
    SECURITY_ATTRIBUTES* get() { return descriptor_ ? &attributes_ : nullptr; }

  private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

std::wstring imagePath(DWORD processId);
std::wstring canonicalPath(const std::wstring& path);
std::wstring fileName(const std::wstring& path);
ULONGLONG processCreationTime(HANDLE process);
bool readProcessIdentity(DWORD processId, DWORD threadId, ProcessIdentity& identity);
bool identitiesEqual(const ProcessIdentity& expected, const ProcessIdentity& actual);
bool allowedFixture(const ProcessIdentity& identity);
bool allowedController(const ProcessIdentity& identity);
bool validFixtureWindow(HWND window, const ProcessIdentity& fixture, std::uint64_t generation);
HWND findFixtureWindow(const ProcessIdentity& fixture, std::uint64_t& generation);
std::wstring mappingName(DWORD processId, ULONGLONG creationTime);
bool modulePresent(DWORD processId, const std::wstring& path);
enum class ModulePresence { Present, Absent, Unknown };
ModulePresence modulePresence(DWORD processId, const std::wstring& path);
bool validWire(const WireMessage& message, const SessionConfiguration& configuration);
bool writeWire(HANDLE pipe, const WireMessage& message, DWORD timeoutMs = 100);
enum class ReadResult { Empty, Message, Closed, Invalid };
ReadResult readWire(HANDLE pipe, WireMessage& message);

} // namespace xdstarter::bridge_poc
