// src/platform/windows/windows_win_key_capture.h

#pragma once

#include "platform/win_key_capture.h"

namespace xdstarter {

class WindowsWinKeyCapture final : public WinKeyCapture {
  public:
    explicit WindowsWinKeyCapture(QObject* parent = nullptr);
    ~WindowsWinKeyCapture() override;
    void setEnabled(bool enabled) override;
    void stop() override;
    [[nodiscard]] bool isActive() const override;
    [[nodiscard]] bool isSupported() const override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xdstarter
