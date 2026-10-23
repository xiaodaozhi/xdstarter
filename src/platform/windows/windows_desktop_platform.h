#pragma once

#include "platform/desktop_platform.h"
#include "platform/start_button_capture_state.h"

namespace xdstarter {

class WindowsDesktopPlatform final : public DesktopPlatform {
  public:
    [[nodiscard]] DesktopActionResult openDesktopAction(const QString& actionId) override;
    [[nodiscard]] DesktopAnchorResult queryStartButtonAnchor() override;
    [[nodiscard]] DesktopStartButtonHitResult queryStartButtonHitTarget() override;
    [[nodiscard]] DesktopAnchorResult queryMenuAnchor() override;
    // Worker-only point hit test. NativeWindowHandle may legitimately be zero
    // for a XAML button; UIA element identity, not its host rectangle, is tested.
    [[nodiscard]] StartButtonCaptureTarget queryStartButtonCaptureTarget();
    [[nodiscard]] DesktopAppearanceResult configureMenuWindow(quintptr nativeHandle) override;
    [[nodiscard]] QScreen *screenForAnchor(const DesktopAnchorResult &anchor) override;
    [[nodiscard]] DesktopWindowMetricsResult queryWindowMetrics(quintptr nativeHandle) override;
    [[nodiscard]] bool hasOwnedForegroundWindow(quintptr menuHandle) override;
    [[nodiscard]] DesktopPresentationResult presentMenuWindow(quintptr nativeHandle) override;
    [[nodiscard]] DesktopPlacementResult placeWindow(quintptr nativeHandle,
                                                     const DesktopAnchorResult& anchor,
                                                     int gapPhysicalPixels = 8,
                                                     bool activate = true,
                                                     int shadowInsetPhysicalPixels = 0) override;
};

} // namespace xdstarter
