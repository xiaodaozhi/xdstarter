#pragma once

#include "platform/desktop_platform.h"

namespace xdstarter {

class WindowsDesktopPlatform final : public DesktopPlatform {
  public:
    [[nodiscard]] DesktopActionResult openDesktopAction(const QString& actionId) override;
    [[nodiscard]] DesktopAnchorResult queryStartButtonAnchor() override;
    [[nodiscard]] DesktopStartButtonHitResult queryStartButtonHitTarget() override;
    [[nodiscard]] DesktopAnchorResult queryMenuAnchor() override;
    [[nodiscard]] DesktopAppearanceResult configureMenuWindow(quintptr nativeHandle) override;
    [[nodiscard]] QScreen *screenForAnchor(const DesktopAnchorResult &anchor) override;
    [[nodiscard]] DesktopWindowMetricsResult queryWindowMetrics(quintptr nativeHandle) override;
    [[nodiscard]] DesktopPlacementResult placeWindow(quintptr nativeHandle,
                                                     const DesktopAnchorResult& anchor,
                                                     int gapPhysicalPixels = 8,
                                                     bool activate = true,
                                                     int shadowInsetPhysicalPixels = 0) override;
};

} // namespace xdstarter
