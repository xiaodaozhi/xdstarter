#include "platform/graphics_failure_notice.h"
#include "windows_graphics_failure_notice.h"

#include <windows.h>
#include <wtsapi32.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace xdstarter {
namespace {

constexpr wchar_t title[] = L"XDStarter 图形界面不可用";
constexpr wchar_t message[] =
    L"XDStarter 无法初始化图形界面，正在退出。Windows 原生开始菜单仍可使用。\n\n"
    L"请更新显卡驱动或重启电脑后重试。";
constexpr DWORD noticeTimeoutSeconds = 60;
constexpr DWORD helperTimeoutMilliseconds = 30000;

DWORD WINAPI stopStuckHelper(void* timeout) {
    Sleep(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(timeout)));
    TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
    return 0;
}

bool startWatchdog() {
    return startGraphicsFailureNoticeWatchdog(helperTimeoutMilliseconds);
}

bool sendSessionMessage() {
    DWORD response = 0;
    // Lengths are bytes without the terminating null. With bWait=FALSE the
    // system owns the message and dismisses it after the finite timeout.
    if (WTSSendMessageW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
                        const_cast<wchar_t*>(title),
                        static_cast<DWORD>((std::size(title) - 1) * sizeof(wchar_t)),
                        const_cast<wchar_t*>(message),
                        static_cast<DWORD>((std::size(message) - 1) * sizeof(wchar_t)),
                        MB_OK | MB_ICONERROR, noticeTimeoutSeconds, &response, FALSE))
        return true;
    std::fprintf(stderr, "Graphics failure session message failed: %lu\n", GetLastError());
    return false;
}

bool showFallbackMessage() {
    // This can wait for the user only in the disposable helper. Its watchdog
    // bounds the wait, and the resident already released both input captures.
    if (MessageBoxW(nullptr, message, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND) != 0)
        return true;
    std::fprintf(stderr, "Graphics failure fallback message failed: %lu\n", GetLastError());
    return false;
}

} // namespace

bool startGraphicsFailureNoticeWatchdog(std::uint32_t timeoutMilliseconds) {
    HANDLE thread = CreateThread(
        nullptr, 0, &stopStuckHelper,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(timeoutMilliseconds)), 0, nullptr);
    if (!thread)
        return false;
    CloseHandle(thread);
    return true;
}

int runGraphicsFailureNoticeWith(const GraphicsFailureNoticeApi& api) {
    if (!api.startWatchdog()) {
        std::fprintf(stderr, "Graphics failure notice watchdog could not start\n");
        return EXIT_FAILURE;
    }
    if (api.sendSessionMessage())
        return EXIT_SUCCESS;
    return api.showFallbackMessage() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int runGraphicsFailureNotice() {
    return runGraphicsFailureNoticeWith({&startWatchdog, &sendSessionMessage, &showFallbackMessage});
}

} // namespace xdstarter
