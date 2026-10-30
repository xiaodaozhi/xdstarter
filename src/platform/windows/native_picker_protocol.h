#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace xdstarter::native_picker {

// Private, inherited mapping shared only with the per-request helper. Do not
// put pointers in this structure or use it as a public IPC contract.
inline constexpr std::uint32_t magic = 0x58535031; // XSP1
inline constexpr std::size_t textCapacity = 256;
inline constexpr std::size_t pathCapacity = 32768;
enum class Result : LONG { Pending = 0, Accepted = 1, Rejected = 2, Failed = 3 };

struct Shared {
    std::uint32_t signature = magic;
    std::uint32_t size = sizeof(Shared);
    std::uintptr_t owner = 0;
    std::uintptr_t parentProcess = 0;
    wchar_t title[textCapacity]{};
    wchar_t jsonFilter[textCapacity]{};
    wchar_t allFilter[textCapacity]{};
    wchar_t path[pathCapacity]{};
    volatile LONG result = static_cast<LONG>(Result::Pending);
    LONG error = 0;
};

inline constexpr char argument[] = "--internal-layout-picker";

// Executes before the GUI application is initialized, in the isolated child.
[[nodiscard]] int runHelper(const char* mappingHandle);

} // namespace xdstarter::native_picker
