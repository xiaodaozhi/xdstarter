// src/experimental/start_bridge/win_support.cpp

#include "experimental/start_bridge/win_support.h"

#include <bcrypt.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace xdstarter::bridge_poc {
namespace {
bool hashFile(const std::wstring& path, std::array<unsigned char, 32>& digest) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    bool success = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 16384> buffer{};
    DWORD count = 0;
    while (success) {
        if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count,
                      nullptr)) {
            success = false;
            break;
        }
        if (!count)
            break;
        success = BCryptHashData(hash, buffer.data(), count, 0) >= 0;
    }
    if (success)
        success = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}
} // namespace

LocalSecurity::LocalSecurity() {
    Handle token;
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return;
    token.reset(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    if (!size || !GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
        return;
    wchar_t* sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
        return;
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor_, nullptr))
        return;
    attributes_ = {sizeof(attributes_), descriptor_, FALSE};
}
LocalSecurity::~LocalSecurity() {
    if (descriptor_)
        LocalFree(descriptor_);
}

std::wstring canonicalPath(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return {};
    wchar_t result[512]{};
    const DWORD length = GetFinalPathNameByHandleW(
        file.get(), result, static_cast<DWORD>(std::size(result)), FILE_NAME_NORMALIZED);
    return length && length < std::size(result) ? std::wstring(result, length) : std::wstring{};
}
std::wstring fileName(const std::wstring& path) {
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}
std::wstring imagePath(DWORD processId) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    wchar_t result[512]{};
    DWORD length = static_cast<DWORD>(std::size(result));
    if (!process || !QueryFullProcessImageNameW(process.get(), 0, result, &length))
        return {};
    return canonicalPath(result);
}
ULONGLONG processCreationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return 0;
    return (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
bool readProcessIdentity(DWORD processId, DWORD threadId, ProcessIdentity& identity) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId));
    if (!process || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT)
        return false;
    const auto path = imagePath(processId);
    if (path.empty() || path.size() >= std::size(identity.path))
        return false;
    USHORT machine = 0, native = 0;
    if (!IsWow64Process2(process.get(), &machine, &native))
        return false;
    if (!machine)
        machine = native;
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<unsigned char> version(size);
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixedSize = 0;
    if (!size || !GetFileVersionInfoW(path.c_str(), 0, size, version.data()) ||
        !VerQueryValueW(version.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) ||
        fixedSize < sizeof(*fixed) || fixed->dwSignature != 0xfeef04bd)
        return false;
    identity = {};
    identity.processId = processId;
    identity.threadId = threadId;
    identity.machine = machine;
    identity.creationTime = processCreationTime(process.get());
    identity.versionMs = fixed->dwFileVersionMS;
    identity.versionLs = fixed->dwFileVersionLS;
    std::copy(path.begin(), path.end(), identity.path);
    return identity.creationTime && hashFile(path, identity.digest);
}
bool identitiesEqual(const ProcessIdentity& expected, const ProcessIdentity& actual) {
    return expected.processId == actual.processId && expected.threadId == actual.threadId &&
           expected.creationTime == actual.creationTime && expected.machine == actual.machine &&
           expected.versionMs == actual.versionMs && expected.versionLs == actual.versionLs &&
           _wcsicmp(expected.path, actual.path) == 0 && expected.digest == actual.digest;
}
bool allowedFixture(const ProcessIdentity& identity) {
    return identity.versionMs == imageVersionMs && identity.versionLs == imageVersionLs &&
           _wcsicmp(fileName(identity.path).c_str(), L"xdstarter_start_bridge_fixture.exe") == 0;
}
bool allowedController(const ProcessIdentity& identity) {
    const auto name = fileName(identity.path);
    return identity.versionMs == imageVersionMs && identity.versionLs == imageVersionLs &&
           (_wcsicmp(name.c_str(), L"xdstarter_start_bridge_controller.exe") == 0 ||
            _wcsicmp(name.c_str(), L"xdstarter_start_bridge_tests.exe") == 0);
}
bool validFixtureWindow(HWND window, const ProcessIdentity& fixture, std::uint64_t generation) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    wchar_t name[96]{};
    return IsWindow(window) && process == fixture.processId && thread == fixture.threadId &&
           GetClassNameW(window, name, static_cast<int>(std::size(name))) &&
           wcscmp(name, fixtureClass) == 0 && generation &&
           reinterpret_cast<UINT_PTR>(GetPropW(window, generationProperty)) == generation;
}
HWND findFixtureWindow(const ProcessIdentity& fixture, std::uint64_t& generation) {
    struct Search {
        const ProcessIdentity* identity;
        HWND window = nullptr;
        unsigned count = 0;
    } search{&fixture};
    EnumThreadWindows(
        fixture.threadId,
        [](HWND window, LPARAM parameter) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(parameter);
            const auto generation =
                reinterpret_cast<UINT_PTR>(GetPropW(window, generationProperty));
            if (validFixtureWindow(window, *search.identity, generation)) {
                search.window = window;
                ++search.count;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    if (search.count != 1)
        return nullptr;
    generation = reinterpret_cast<UINT_PTR>(GetPropW(search.window, generationProperty));
    return search.window;
}
std::wstring mappingName(DWORD processId, ULONGLONG created) {
    return L"Local\\XDStarter.BridgePoC52." + std::to_wstring(processId) + L"." +
           std::to_wstring(created);
}
ModulePresence modulePresence(DWORD processId, const std::wstring& path) {
    Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId));
    if (!modules)
        return ModulePresence::Unknown;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(modules.get(), &entry))
        return ModulePresence::Unknown;
    bool unknownPath = false;
    do {
        const auto canonical = canonicalPath(entry.szExePath);
        if (canonical.empty())
            unknownPath = true;
        else if (_wcsicmp(canonical.c_str(), path.c_str()) == 0)
            return ModulePresence::Present;
    } while (Module32NextW(modules.get(), &entry));
    return unknownPath || GetLastError() != ERROR_NO_MORE_FILES ? ModulePresence::Unknown
                                                                : ModulePresence::Absent;
}
bool modulePresent(DWORD processId, const std::wstring& path) {
    return modulePresence(processId, path) != ModulePresence::Absent;
}
bool validWire(const WireMessage& message, const SessionConfiguration& configuration) {
    return message.magic == protocolMagic && message.version == protocolVersion &&
           message.bytes == sizeof(WireMessage) && message.nonce == configuration.nonce;
}

bool writeWire(HANDLE pipe, const WireMessage& message, DWORD timeoutMs) {
    // A rare kernel cancellation failure retains the heap buffer/event. It is
    // preferable to an outstanding OVERLAPPED referencing a returned stack.
    struct Pending {
        OVERLAPPED operation{};
        WireMessage message;
    };
    auto* pending = new Pending{};
    pending->message = message;
    pending->operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!pending->operation.hEvent) {
        delete pending;
        return false;
    }
    DWORD count = 0;
    bool success =
        WriteFile(pipe, &pending->message, sizeof(message), &count, &pending->operation) != FALSE;
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(pending->operation.hEvent, timeoutMs) == WAIT_OBJECT_0)
            success = GetOverlappedResult(pipe, &pending->operation, &count, FALSE) != FALSE;
        else {
            CancelIoEx(pipe, &pending->operation);
            if (WaitForSingleObject(pending->operation.hEvent, 200) != WAIT_OBJECT_0)
                return false;
            GetOverlappedResult(pipe, &pending->operation, &count, FALSE);
        }
    }
    CloseHandle(pending->operation.hEvent);
    delete pending;
    return success && count == sizeof(message);
}
ReadResult readWire(HANDLE pipe, WireMessage& message) {
    DWORD available = 0, remaining = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, &remaining))
        return ReadResult::Closed;
    if (!available)
        return ReadResult::Empty;
    if (remaining != sizeof(message))
        return ReadResult::Invalid;
    struct Pending {
        OVERLAPPED operation{};
        WireMessage message;
    };
    auto* pending = new Pending{};
    pending->operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!pending->operation.hEvent) {
        delete pending;
        return ReadResult::Closed;
    }
    DWORD count = 0;
    const BOOL read =
        ReadFile(pipe, &pending->message, sizeof(message), &count, &pending->operation);
    bool success = read != FALSE;
    if (!read && GetLastError() == ERROR_IO_PENDING) {
        // Peek established that this one complete fixed-size message exists.
        // There is one reader per pipe endpoint, so this read cannot wait for
        // a peer to produce additional bytes.
        if (WaitForSingleObject(pending->operation.hEvent, 200) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &pending->operation);
            if (WaitForSingleObject(pending->operation.hEvent, 200) != WAIT_OBJECT_0)
                return ReadResult::Closed; // Retain outstanding kernel buffer/event.
        } else {
            success = GetOverlappedResult(pipe, &pending->operation, &count, FALSE) != FALSE;
        }
    }
    if (success)
        message = pending->message;
    CloseHandle(pending->operation.hEvent);
    delete pending;
    return success && count == sizeof(message) ? ReadResult::Message : ReadResult::Closed;
}

} // namespace xdstarter::bridge_poc
