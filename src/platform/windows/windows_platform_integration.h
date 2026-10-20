#pragma once

#include "platform/platform_integration.h"

namespace xdstarter {

class WindowsPlatformIntegration final : public PlatformIntegration {
  public:
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString statusDescription() const override;
    [[nodiscard]] bool isSimulated() const override;
};

} // namespace xdstarter
