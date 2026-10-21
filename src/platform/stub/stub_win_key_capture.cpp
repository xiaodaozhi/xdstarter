// src/platform/stub/stub_win_key_capture.cpp

#include "stub_win_key_capture.h"

namespace xdstarter {

void StubWinKeyCapture::setEnabled(bool enabled) {
    const auto generation = endEventSession();
    queueStatusChanged(generation, false,
                       enabled ? QStringLiteral("此平台不支持 Windows 单 Win 键接管。")
                               : QStringLiteral("Win 键接管已暂停，使用系统开始菜单。"),
                       enabled ? QStringLiteral("unsupported-platform") : QString{});
}

void StubWinKeyCapture::stop() {
    (void)endEventSession();
}

} // namespace xdstarter
