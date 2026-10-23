// src/platform/start_button_capture.cpp

#include "start_button_capture.h"

namespace xdstarter {
QJsonObject startButtonCaptureDiagnosticSnapshot(const WinKeyCapture& capture) {
    const auto* start = dynamic_cast<const StartButtonCapture*>(&capture);
    return start ? start->diagnosticSnapshot()
                 : QJsonObject{{QStringLiteral("supported"), false},
                               {QStringLiteral("hookLiveness"), QStringLiteral("unknown")}};
}
void recordStartButtonHostObservation(WinKeyCapture& capture,
                                      const StartButtonHostObservation& observation) {
    if (auto* start = dynamic_cast<StartButtonCapture*>(&capture))
        start->recordHostObservation(observation);
}
} // namespace xdstarter

#ifndef Q_OS_WIN
#include "stub/stub_win_key_capture.h"
namespace xdstarter {
std::unique_ptr<WinKeyCapture> createStartButtonCapture(StartButtonCaptureOptions) {
    return std::make_unique<StubWinKeyCapture>();
}
} // namespace xdstarter
#endif
