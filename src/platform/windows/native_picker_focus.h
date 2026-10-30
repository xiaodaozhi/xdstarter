#pragma once

#include <windows.h>

namespace xdstarter::native_picker {

// GUI-thread only. The owner retains the live process handle until unregistered.
void registerForegroundOwner(HWND menu, HANDLE process);
void unregisterForegroundOwner(HWND menu, HANDLE process);
[[nodiscard]] bool isTrustedForeground(HWND menu, HWND foreground);

} // namespace xdstarter::native_picker
