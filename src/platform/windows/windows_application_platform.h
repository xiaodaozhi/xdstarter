#pragma once

#include "platform/application_platform.h"

namespace xdstarter {

class WindowsApplicationPlatform final : public ApplicationPlatform {
  public:
    WindowsApplicationPlatform() = default;
    // An explicit root list supports isolated fixtures without touching either
    // system Start Menu. Production construction uses the two Known Folders.
    explicit WindowsApplicationPlatform(QStringList shortcutRoots, bool includePackaged = false);

    [[nodiscard]] CatalogSnapshot scanApplications() override;
    [[nodiscard]] QImage loadIcon(const LaunchTarget& target, int size) override;
    [[nodiscard]] LaunchResult launch(const LaunchTarget& target) override;
    [[nodiscard]] LaunchResult checkAvailability(const LaunchTarget& target) override;

  private:
    QStringList shortcutRoots_;
    bool explicitRoots_ = false;
    bool includePackaged_ = true;
};

} // namespace xdstarter
