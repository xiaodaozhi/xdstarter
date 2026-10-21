#pragma once

#include <QString>

#include <memory>

namespace xdstarter {

class PlatformIntegration {
  public:
    virtual ~PlatformIntegration() = default;

    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual QString statusDescription() const = 0;
    [[nodiscard]] virtual bool isSimulated() const = 0;
};

[[nodiscard]] std::unique_ptr<PlatformIntegration> createPlatformIntegration();

} // namespace xdstarter
