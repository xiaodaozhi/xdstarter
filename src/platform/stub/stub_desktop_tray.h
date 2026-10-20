#pragma once

#include "platform/desktop_tray.h"

namespace xdstarter {

class StubDesktopTray final : public DesktopTray {
  public:
    using DesktopTray::DesktopTray;

    [[nodiscard]] DesktopActionResult start(const QImage&) override;
    void stop() override {}
    [[nodiscard]] bool isAvailable() const override { return false; }
    void showContextMenu() override {}
};

} // namespace xdstarter
