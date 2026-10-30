#include "native_picker_focus.h"

#include <algorithm>
#include <vector>

namespace xdstarter::native_picker {
namespace {
struct Owner { HWND menu; HANDLE process; };
std::vector<Owner> owners;
} // namespace

void registerForegroundOwner(HWND menu, HANDLE process) {
    owners.push_back({menu, process});
}

void unregisterForegroundOwner(HWND menu, HANDLE process) {
    owners.erase(std::remove_if(owners.begin(), owners.end(), [=](const Owner& owner) {
        return owner.menu == menu && owner.process == process;
    }), owners.end());
}

bool isTrustedForeground(HWND menu, HWND foreground) {
    if (!menu || !foreground || !IsWindow(foreground) || GetForegroundWindow() != foreground)
        return false;
    DWORD foregroundPid = 0;
    if (!GetWindowThreadProcessId(foreground, &foregroundPid) || foregroundPid == GetCurrentProcessId())
        return false;
    for (const auto& owner : owners) {
        if (owner.menu != menu || !owner.process || WaitForSingleObject(owner.process, 0) != WAIT_TIMEOUT ||
            GetProcessId(owner.process) != foregroundPid)
            continue;
        HWND candidate = GetAncestor(foreground, GA_ROOT);
        bool sawPicker = false;
        for (int depth = 0; candidate && depth < 16; ++depth) {
            DWORD pid = 0;
            if (!IsWindow(candidate) || !GetWindowThreadProcessId(candidate, &pid) ||
                (pid != foregroundPid && pid != GetCurrentProcessId()))
                break;
            sawPicker |= pid == foregroundPid;
            if (candidate == menu)
                return sawPicker && GetForegroundWindow() == foreground &&
                       WaitForSingleObject(owner.process, 0) == WAIT_TIMEOUT &&
                       GetProcessId(owner.process) == foregroundPid;
            const HWND next = GetWindow(candidate, GW_OWNER);
            if (next == candidate)
                break;
            candidate = next;
        }
    }
    return false;
}

} // namespace xdstarter::native_picker
