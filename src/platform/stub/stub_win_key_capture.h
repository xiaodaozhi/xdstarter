// src/platform/stub/stub_win_key_capture.h

#pragma once

#include "platform/win_key_capture.h"

namespace xdstarter {

class StubWinKeyCapture final : public WinKeyCapture {
  public:
    using WinKeyCapture::WinKeyCapture;
    void setEnabled(bool enabled) override;
    void stop() override;
    [[nodiscard]] bool isActive() const override { return false; }
    [[nodiscard]] bool isSupported() const override { return false; }
};

} // namespace xdstarter
