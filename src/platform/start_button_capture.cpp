// src/platform/start_button_capture.cpp

#include "start_button_capture.h"

namespace xdstarter {
QJsonObject startButtonHostObservationJson(const StartButtonHostObservation& observation,
                                           quint64 observedAt) {
    const auto latency = [observedAt](quint64 began) {
        return began && observedAt >= began ? QJsonValue(qint64(observedAt - began))
                                            : QJsonValue(QJsonValue::Null);
    };
    return {{QStringLiteral("event"), observation.event},
            {QStringLiteral("at"), qint64(observedAt)},
            {QStringLiteral("sessionGeneration"), qint64(observation.context.sessionGeneration)},
            {QStringLiteral("gestureId"), qint64(observation.context.gestureId)},
            {QStringLiteral("pressedAt"), qint64(observation.context.pressedAt)},
            {QStringLiteral("releasedAt"), qint64(observation.context.releasedAt)},
            {QStringLiteral("pressedToObservationMs"), latency(observation.context.pressedAt)},
            {QStringLiteral("releasedToObservationMs"), latency(observation.context.releasedAt)},
            {QStringLiteral("requestedAction"), observation.requestedAction},
            {QStringLiteral("visible"), observation.visible},
            {QStringLiteral("minimized"), observation.minimized},
            {QStringLiteral("nativeMeasured"), observation.nativeMeasured},
            {QStringLiteral("foreground"), observation.nativeMeasured
                                               ? QJsonValue(observation.foreground)
                                               : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("errorCode"), observation.errorCode}};
}

void StartButtonCapture::recordToggleDelivery(const CaptureToggleContext& context, bool delivered) {
    StartButtonHostObservation observation;
    observation.context = context;
    observation.event =
        delivered ? QStringLiteral("qt-toggle-delivered") : QStringLiteral("qt-toggle-discarded");
    recordHostObservation(observation);
}

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
