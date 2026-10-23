#pragma once

#include "platform/desktop_platform.h"
#include "platform/start_button_capture_state.h"

#include <QJsonObject>

struct IUIAutomation;
struct IUIAutomationElement;

namespace xdstarter {
class WindowsMenuResizeFilter;
class WindowsStartButtonQueryCache;

struct StartButtonQueryObservation {
    StartButtonCaptureReason reason = StartButtonCaptureReason::QueryFailed;
    QString stage;
    qint64 result = 0;
    quint64 durationMs = 0;
    bool reusedElement = false;
    QJsonObject details;
};

class WindowsDesktopPlatform final : public DesktopPlatform {
  public:
    WindowsDesktopPlatform();
    ~WindowsDesktopPlatform() override;
    [[nodiscard]] DesktopActionResult openDesktopAction(const QString& actionId) override;
    [[nodiscard]] DesktopAnchorResult queryStartButtonAnchor() override;
    [[nodiscard]] DesktopStartButtonHitResult queryStartButtonHitTarget() override;
    [[nodiscard]] DesktopAnchorResult queryMenuAnchor() override;
    // Worker-only point hit test. NativeWindowHandle may legitimately be zero
    // for a XAML button; UIA element identity, not its host rectangle, is tested.
    [[nodiscard]] StartButtonCaptureTarget queryStartButtonCaptureTarget();
    [[nodiscard]] StartButtonCaptureTarget
    queryStartButtonCaptureTarget(IUIAutomation* automation, StartButtonCaptureMode mode,
                                  quint64 epoch, StartButtonQueryObservation* observation,
                                  IUIAutomationElement** validatedButton = nullptr);
    [[nodiscard]] DesktopAppearanceResult configureMenuWindow(quintptr nativeHandle) override;
    [[nodiscard]] QScreen *screenForAnchor(const DesktopAnchorResult &anchor) override;
    [[nodiscard]] DesktopWindowMetricsResult queryWindowMetrics(quintptr nativeHandle) override;
    [[nodiscard]] bool hasOwnedForegroundWindow(quintptr menuHandle) override;
    [[nodiscard]] bool hasOwnedForegroundDialog(quintptr menuHandle) override;
    [[nodiscard]] DesktopPresentationResult presentMenuWindow(quintptr nativeHandle) override;
    [[nodiscard]] DesktopPlacementResult placeWindow(quintptr nativeHandle,
                                                     const DesktopAnchorResult& anchor,
                                                     int gapPhysicalPixels = 8,
                                                     bool activate = true,
                                                     int shadowInsetPhysicalPixels = 0) override;

  private:
    std::unique_ptr<WindowsMenuResizeFilter> resizeFilter_;
    std::unique_ptr<WindowsStartButtonQueryCache> captureQueryCache_;
};

} // namespace xdstarter
