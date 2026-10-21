#pragma once

#include "platform/desktop_platform.h"

namespace xdstarter {

class StubDesktopPlatform final : public DesktopPlatform {
  public:
    [[nodiscard]] DesktopActionResult openDesktopAction(const QString& actionId) override;
    [[nodiscard]] DesktopAnchorResult queryStartButtonAnchor() override;
};

} // namespace xdstarter
