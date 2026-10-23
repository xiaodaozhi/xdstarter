// src/platform/start_button_capture.h

#pragma once

#include "win_key_capture.h"

namespace xdstarter {
// Uses the same generation-checked event channel as keyboard capture, with an
// independent enable/pause lifecycle. Construction does not install hooks.
[[nodiscard]] std::unique_ptr<WinKeyCapture> createStartButtonCapture();
} // namespace xdstarter
