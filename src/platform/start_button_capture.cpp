// src/platform/start_button_capture.cpp

#include "start_button_capture.h"

#ifndef Q_OS_WIN
#include "stub/stub_win_key_capture.h"
namespace xdstarter {
std::unique_ptr<WinKeyCapture> createStartButtonCapture() {
    return std::make_unique<StubWinKeyCapture>();
}
} // namespace xdstarter
#endif
