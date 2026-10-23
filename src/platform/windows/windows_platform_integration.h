#pragma once

#include "platform/platform_integration.h"

namespace xdstarter {

class WindowsPlatformIntegration final : public PlatformIntegration {
  public:
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString statusDescription() const override;
    [[nodiscard]] bool isSimulated() const override;
    [[nodiscard]] QString sessionKey() const override;
    void allowForegroundActivation(qint64 processId) const override;
};

} // namespace xdstarter
