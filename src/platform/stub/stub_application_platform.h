#pragma once

#include "platform/application_platform.h"

namespace xdstarter {

class StubApplicationPlatform final : public ApplicationPlatform {
  public:
    [[nodiscard]] CatalogSnapshot scanApplications() override;
    [[nodiscard]] QImage loadIcon(const LaunchTarget& target, int size) override;
    [[nodiscard]] LaunchResult launch(const LaunchTarget& target) override;
    [[nodiscard]] LaunchResult checkAvailability(const LaunchTarget& target) override;
};

} // namespace xdstarter
