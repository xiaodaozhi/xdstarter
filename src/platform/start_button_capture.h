// src/platform/start_button_capture.h

#pragma once

#include "start_button_capture_state.h"
#include "win_key_capture.h"

#include <QJsonObject>

namespace xdstarter {
struct StartButtonCaptureOptions {
    StartButtonCaptureMode mode = StartButtonCaptureMode::Point;
};

struct StartButtonHostObservation {
    CaptureToggleContext context;
    QString event;
    QString requestedAction;
    bool visible = false;
    bool minimized = false;
    bool foreground = false;
    bool nativeMeasured = false;
    QString errorCode;
};

class StartButtonCapture : public WinKeyCapture {
  public:
    using WinKeyCapture::WinKeyCapture;
    [[nodiscard]] virtual QJsonObject diagnosticSnapshot() const = 0;
    virtual void recordHostObservation(const StartButtonHostObservation& observation) = 0;

  protected:
    void recordToggleDelivery(const CaptureToggleContext& context, bool delivered) override;
};

[[nodiscard]] QJsonObject
startButtonHostObservationJson(const StartButtonHostObservation& observation, quint64 observedAt);

// Uses the same generation-checked event channel as keyboard capture, with an
// independent enable/pause lifecycle. Construction does not install hooks.
[[nodiscard]] std::unique_ptr<WinKeyCapture>
createStartButtonCapture(StartButtonCaptureOptions options = {});
[[nodiscard]] QJsonObject startButtonCaptureDiagnosticSnapshot(const WinKeyCapture& capture);
void recordStartButtonHostObservation(WinKeyCapture& capture,
                                      const StartButtonHostObservation& observation);
} // namespace xdstarter
