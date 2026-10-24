// tests/platform/start_button_capture_tests.cpp

#include "platform/start_button_capture.h"
#include "platform/start_button_capture_state.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>

#ifdef Q_OS_WIN
#include "platform/windows/windows_desktop_platform.h"
#include <uiautomation.h>
#include <windows.h>
#endif

using namespace xdstarter;
namespace {
int checks = 0;
int failures = 0;
void expect(bool value, const char* reason) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << reason << '\n';
    }
}
StartButtonCaptureTarget target() {
    return {1,
            2,
            42,
            QRect(-1920, 1040, 1920, 40),
            QRect(-1920, 1040, 48, 40),
            QPoint(-1900, 1060),
            1000,
            3,
            true,
            7,
            QRect(-1920, 0, 1920, 1080),
            96};
}

class DeliveryFixture final : public StartButtonCapture {
  public:
    void setEnabled(bool enabled) override {
        generation = enabled ? beginEventSession() : endEventSession();
        active = enabled;
    }
    void stop() override { setEnabled(false); }
    bool isActive() const override { return active; }
    bool isSupported() const override { return true; }
    QJsonObject diagnosticSnapshot() const override { return {}; }
    void recordHostObservation(const StartButtonHostObservation& observation) override {
        history.append(observation);
    }
    void post(CaptureToggleContext context) { queueToggleRequested(context); }
    quint64 generation = 0;
    bool active = false;
    QList<StartButtonHostObservation> history;
};

void testCorrelation() {
    StartButtonGestureTracker tracker;
    expect(!tracker.observe(StartMouseEvent::LeftUp, 1, 10, false, false).id,
           "an unmatched release has no invented gesture identity");
    const auto down = tracker.observe(StartMouseEvent::LeftDown, 1, 20, false, false);
    expect(down.id && down.pressedAt == 20 && down.sessionGeneration == 1,
           "physical down starts a correlated observation, not inferred Start intent");
    expect(!tracker.observe(StartMouseEvent::LeftUp, 1, 25, true, true).id,
           "injected release does not steal physical gesture ownership");
    const auto repeat = tracker.observe(StartMouseEvent::LeftDown, 1, 30, false, true);
    expect(repeat.id == down.id && repeat.pressedAt == down.pressedAt,
           "repeated owned down preserves the original correlation");
    const auto drained = tracker.observe(StartMouseEvent::LeftUp, 2, 40, false, true);
    expect(drained.id == down.id && drained.sessionGeneration == 1,
           "release drained after pause retains the originating session");
    expect(!tracker.observe(StartMouseEvent::LeftUp, 2, 41, false, false).id,
           "duplicate up cannot reuse a completed identity");
    const auto next = tracker.observe(StartMouseEvent::LeftDown, 2, 50, false, false);
    expect(next.id > down.id && next.sessionGeneration == 2,
           "replacement session never reuses a previous gesture ID");

    DeliveryFixture capture;
    capture.setEnabled(true);
    QList<CaptureToggleContext> delivered;
    QObject::connect(&capture, &WinKeyCapture::toggleRequested, &capture,
                     [&] { delivered.append(capture.deliveringToggleContext()); });
    capture.post({capture.generation, 101, 100, 150});
    capture.post({capture.generation, 102, 160, 200});
    QCoreApplication::processEvents();
    expect(delivered.size() == 2 && delivered[0].gestureId == 101 &&
               delivered[1].gestureId == 102 && delivered[0].releasedAt == 150,
           "queued GUI delivery carries each gesture without latest-event misattribution");
    expect(!capture.deliveringToggleContext().gestureId,
           "context cannot leak into unrelated direct activations after signal delivery");
    DeliveryFixture nested;
    nested.setEnabled(true);
    QList<quint64> nestedOrder;
    QObject::connect(&nested, &WinKeyCapture::toggleRequested, &nested, [&] {
        const auto id = nested.deliveringToggleContext().gestureId;
        nestedOrder.append(id);
        if (id == 201) {
            nested.post({nested.generation, 202, 110, 160});
            QCoreApplication::processEvents();
            nestedOrder.append(nested.deliveringToggleContext().gestureId);
        }
    });
    nested.post({nested.generation, 201, 100, 150});
    QCoreApplication::processEvents();
    expect(nestedOrder == QList<quint64>{201, 202, 201} &&
               !nested.deliveringToggleContext().gestureId,
           "nested GUI event loops restore the outer gesture context and clear it on exit");
    capture.post({capture.generation, 103, 210, 250});
    capture.stop();
    capture.setEnabled(true);
    QCoreApplication::processEvents();
    expect(delivered.size() == 2 && capture.history.last().context.gestureId == 103 &&
               capture.history.last().event == QStringLiteral("qt-toggle-discarded"),
           "stale queued toggle is traced as discarded and never delivered to a new session");
    StartButtonHostObservation host;
    host.context = {capture.generation, 104, 300, 350};
    host.event = QStringLiteral("presentation-result");
    host.requestedAction = QStringLiteral("show");
    auto json = startButtonHostObservationJson(host, 400);
    expect(json[QStringLiteral("foreground")].isNull() &&
               json[QStringLiteral("releasedToObservationMs")].toInteger() == 50 &&
               json[QStringLiteral("gestureId")].toInteger() == 104,
           "latency is correlated while an unmeasured native foreground remains unknown");
    host.nativeMeasured = true;
    host.foreground = true;
    json = startButtonHostObservationJson(host, 200);
    expect(json[QStringLiteral("foreground")].toBool() &&
               json[QStringLiteral("pressedToObservationMs")].isNull(),
           "invalid clock order never produces a negative or fabricated latency");
}

#ifdef Q_OS_WIN
int inspectNativeTarget(const QString& modeName) {
    if (modeName != QLatin1String("point") && modeName != QLatin1String("area-cache"))
        return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return 1;
    IUIAutomation* automation = nullptr;
    auto status = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&automation));
    IUIAutomation2* bounded = nullptr;
    if (SUCCEEDED(status))
        status = automation->QueryInterface(IID_PPV_ARGS(&bounded));
    if (SUCCEEDED(status))
        status = bounded->put_ConnectionTimeout(100);
    if (SUCCEEDED(status))
        status = bounded->put_TransactionTimeout(100);
    QJsonArray samples;
    int identified = 0;
    if (SUCCEEDED(status)) {
        // Release cached provider elements before destroying their MTA/client.
        WindowsDesktopPlatform platform;
        const auto mode = modeName == QLatin1String("point")
                              ? StartButtonCaptureMode::Point
                              : StartButtonCaptureMode::ExperimentalAreaCache;
        for (int index = 0; index < 8; ++index) {
            StartButtonQueryObservation observation;
            const auto current =
                platform.queryStartButtonCaptureTarget(automation, mode, 1, &observation);
            if (current.identified)
                ++identified;
            samples.append(
                QJsonObject{{QStringLiteral("identified"), current.identified},
                            {QStringLiteral("areaValidated"), current.areaValidated},
                            {QStringLiteral("reason"),
                             QString::fromLatin1(startButtonCaptureReasonName(observation.reason))},
                            {QStringLiteral("stage"), observation.stage},
                            {QStringLiteral("hresult"), QString::number(observation.result)},
                            {QStringLiteral("durationMs"), qint64(observation.durationMs)},
                            {QStringLiteral("reusedElement"), observation.reusedElement},
                            {QStringLiteral("details"), observation.details},
                            {QStringLiteral("button"),
                             QJsonArray{current.buttonRect.x(), current.buttonRect.y(),
                                        current.buttonRect.width(), current.buttonRect.height()}}});
            Sleep(20);
        }
    }
    if (bounded)
        bounded->Release();
    if (automation)
        automation->Release();
    CoUninitialize();
    const QJsonObject evidence{
        {QStringLiteral("scope"), QStringLiteral("Read-only UIA query; no Hook or input")},
        {QStringLiteral("mode"), modeName},
        {QStringLiteral("setupHresult"), QString::number(status)},
        {QStringLiteral("identifiedSamples"), identified},
        {QStringLiteral("samples"), samples}};
    std::cout << QJsonDocument(evidence).toJson(QJsonDocument::Indented).constData();
    return FAILED(status) ? 1 : 0;
}
#endif
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
#ifdef Q_OS_WIN
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == QLatin1String("--inspect-native-target"))
        return inspectNativeTarget(arguments[2]);
#endif
    testCorrelation();
    const auto ready = target();
    const auto point = ready.qualifiedPoint;
    expect(StartButtonCaptureState::qualifies(ready, point, 1200), "200ms boundary qualifies");
    expect(!StartButtonCaptureState::qualifies(ready, point, 1201),
           "stale snapshot passes through");
    expect(!StartButtonCaptureState::qualifies(ready, point, 999), "future timestamps rejected");
    expect(!StartButtonCaptureState::qualifies(ready, point + QPoint(2, 0), 1000),
           "rectangle alone cannot authorize another point");
    for (int mutation = 0; mutation < 10; ++mutation) {
        auto changed = ready;
        switch (mutation) {
        case 0:
            changed.identified = false;
            break;
        case 1:
            changed.taskbar = 0;
            break;
        case 2:
            changed.hitWindow = 0;
            break;
        case 3:
            changed.processId = 0;
            break;
        case 4:
            changed.buttonRect = QRect(-1920, 1040, 257, 40);
            break;
        case 5:
            changed.taskbarRect = QRect();
            break;
        case 6:
            changed.qualifiedPoint += QPoint(0, 2);
            break;
        case 7:
            changed.monitor = 0;
            break;
        case 8:
            changed.monitorRect = {};
            break;
        case 9:
            changed.dpi = 0;
            break;
        }
        expect(!StartButtonCaptureState::qualifies(changed, point, 1000),
               "invalid identity or geometry");
    }
    StartButtonCaptureState state;
    expect(!state.process(StartMouseEvent::LeftUp, point, 1000, ready, true).consume,
           "release without captured down remains native");
    expect(!state.process(StartMouseEvent::LeftDown, point, 1000, ready, false).consume,
           "disabled press remains native");
    for (int click = 0; click < 2; ++click) {
        expect(state.process(StartMouseEvent::LeftDown, point, 1000, ready, true).consume,
               "each press in a double click is owned");
        const auto up = state.process(StartMouseEvent::LeftUp, point, 1050, ready, true);
        expect(up.consume && up.toggle && !state.ownsPress(), "click toggles once on release");
    }
    // Every invalidation after down still consumes exactly that matching up.
    for (int mutation = 0; mutation < 15; ++mutation) {
        (void)state.process(StartMouseEvent::LeftDown, point, 1000, ready, true);
        auto changed = ready;
        bool eligible = true;
        quint64 time = 1050;
        switch (mutation) {
        case 0:
            eligible = false;
            break;
        case 1:
            ++changed.epoch;
            break;
        case 2:
            ++changed.taskbar;
            break;
        case 3:
            ++changed.processId;
            break;
        case 4:
            changed.buttonRect.translate(1, 0);
            break;
        case 5:
            changed.taskbarRect.translate(1, 0);
            break;
        case 6:
            time = 1201;
            break;
        case 7:
            time = 2600;
            changed.observedAt = time;
            break;
        case 8:
            state.cancel();
            break;
        case 9:
            expect(!state.process(StartMouseEvent::OtherButton, point, 1020, ready, true).consume,
                   "right/middle/wheel remain native");
            break;
        case 10:
            (void)state.process(StartMouseEvent::Move, point + QPoint(10, 0), 1020, ready, true);
            break;
        case 11:
            expect(!state.process(StartMouseEvent::Move, point, 1020, ready, true, true).consume,
                   "external injected input remains native and cancels candidate");
            break;
        case 12:
            ++changed.monitor;
            break;
        case 13:
            ++changed.dpi;
            break;
        case 14:
            changed.monitorRect.translate(1, 0);
            break;
        }
        const auto up = state.process(StartMouseEvent::LeftUp, point, time, changed, eligible);
        expect(up.consume && !up.toggle && !state.ownsPress(), "canceled press owns its release");
        expect(!state.process(StartMouseEvent::LeftUp, point, time, changed, eligible).consume,
               "only one release is consumed");
    }
    expect(!state.process(StartMouseEvent::LeftDown, point, 1000, ready, true, true).consume,
           "synthetic clicks never activate production capture");
    // The control policy is unchanged; a different region tag alone never
    // broadens a default point consumer's authority.
    auto area = ready;
    area.mode = StartButtonCaptureMode::ExperimentalAreaCache;
    area.areaValidated = true;
    const auto distantPoint = ready.buttonRect.topLeft() + QPoint(3, 3);
    expect(StartButtonCaptureState::qualificationFailure(ready, distantPoint, 1000) ==
               StartButtonCaptureReason::PointDelta,
           "default point policy reports its strict point mismatch");
    expect(StartButtonCaptureState::qualificationFailure(area, distantPoint, 1000) ==
               StartButtonCaptureReason::PolicyMismatch,
           "default consumer cannot accept experimental area snapshots");
    expect(!StartButtonCaptureState::qualifies(ready, distantPoint, 1000,
                                               StartButtonCaptureMode::ExperimentalAreaCache),
           "area consumer cannot reinterpret an ordinary point snapshot");
    for (const auto candidate :
         {area.buttonRect.topLeft(), area.buttonRect.topRight(), area.buttonRect.bottomLeft(),
          area.buttonRect.bottomRight(), area.buttonRect.center()}) {
        StartButtonCaptureState experiment;
        const auto down = experiment.process(StartMouseEvent::LeftDown, candidate, 1000, area, true,
                                             false, StartButtonCaptureReason::None,
                                             StartButtonCaptureMode::ExperimentalAreaCache);
        const auto up = experiment.process(StartMouseEvent::LeftUp, candidate, 1050, area, true,
                                           false, StartButtonCaptureReason::None,
                                           StartButtonCaptureMode::ExperimentalAreaCache);
        expect(down.consume && down.reason == StartButtonCaptureReason::AcceptedDown &&
                   up.consume && up.toggle &&
                   up.reason == StartButtonCaptureReason::AcceptedRelease,
               "explicit validated area owns one complete click at each in-area point");
    }
    area.areaValidated = false;
    expect(StartButtonCaptureState::qualificationFailure(
               area, point, 1000, StartButtonCaptureMode::ExperimentalAreaCache) ==
               StartButtonCaptureReason::AreaNotValidated,
           "area tag requires actual producer validation");
    area.areaValidated = true;
    expect(StartButtonCaptureState::qualifies(area, point, 1200,
                                              StartButtonCaptureMode::ExperimentalAreaCache) &&
               !StartButtonCaptureState::qualifies(area, point, 1201,
                                                   StartButtonCaptureMode::ExperimentalAreaCache),
           "area experiment retains the exact 200ms lease boundary");
    expect(StartButtonCaptureState::qualificationFailure(ready, point, 999) ==
                   StartButtonCaptureReason::FutureSnapshot &&
               StartButtonCaptureState::qualificationFailure(ready, point, 1201) ==
                   StartButtonCaptureReason::ExpiredSnapshot,
           "timestamp rejection exports distinct stable reasons");
    for (const auto reason :
         {StartButtonCaptureReason::SnapshotBusy, StartButtonCaptureReason::Modifiers,
          StartButtonCaptureReason::EpochChanged, StartButtonCaptureReason::SubscriptionFailed,
          StartButtonCaptureReason::InputDesktopUnavailable}) {
        StartButtonCaptureState fault;
        const auto passthrough =
            fault.process(StartMouseEvent::LeftDown, point, 1000, ready, false, false, reason);
        expect(!passthrough.consume && passthrough.reason == reason,
               "a failed prerequisite leaves native down and preserves its exact reason");
        static_cast<void>(fault.process(StartMouseEvent::LeftDown, point, 1000, ready, true));
        const auto canceled =
            fault.process(StartMouseEvent::LeftUp, point, 1050, ready, false, false, reason);
        expect(canceled.consume && !canceled.toggle && canceled.reason == reason &&
                   !fault.ownsPress(),
               "the same prerequisite failure after down cancels action but still owns up");
    }
    {
        StartButtonCaptureState canceled;
        static_cast<void>(canceled.process(StartMouseEvent::LeftDown, point, 1000, ready, true));
        canceled.cancel(StartButtonCaptureReason::KeyboardInput);
        canceled.cancel(StartButtonCaptureReason::Disabled);
        const auto up = canceled.process(StartMouseEvent::LeftUp, point, 1050, ready, true);
        expect(up.consume && !up.toggle && up.reason == StartButtonCaptureReason::KeyboardInput,
               "the first cancellation reason survives later cleanup");
    }
    for (int stage = 0; stage < 3; ++stage) {
        const qint32 failure = -2147467259;
        const auto subscription = startButtonSubscriptionOutcome(
            stage == 0 ? failure : 0, stage == 1 ? failure : 0, stage == 2 ? failure : 0);
        expect(!subscription.ready && subscription.result == failure &&
                   static_cast<int>(subscription.stage) == stage + 1,
               "injected subscription failure identifies root/structure/property stage");
        StartButtonCaptureState fault;
        const auto down =
            fault.process(StartMouseEvent::LeftDown, point, 1000, ready, subscription.ready, false,
                          StartButtonCaptureReason::SubscriptionFailed);
        expect(!down.consume && !down.toggle && !fault.ownsPress(),
               "a subscription failure cannot produce a usable interception session");
    }
    expect(startButtonSubscriptionOutcome(0, 0, 0).ready,
           "successful subscription results permit qualification work");
    {
        StartButtonCaptureState crossing;
        auto previousSession = ready;
        previousSession.sessionGeneration = 7;
        static_cast<void>(
            crossing.process(StartMouseEvent::LeftDown, point, 1000, previousSession, true));
        auto replacement = previousSession;
        ++replacement.sessionGeneration;
        const auto up = crossing.process(StartMouseEvent::LeftUp, point, 1050, replacement, true);
        expect(up.consume && !up.toggle && up.reason == StartButtonCaptureReason::SessionSuperseded,
               "an owned release cannot activate a replacement session with otherwise identical "
               "geometry");
    }
    for (int value = 0; value < static_cast<int>(StartButtonCaptureReason::Count); ++value)
        expect(QString::fromLatin1(startButtonCaptureReasonName(
                   static_cast<StartButtonCaptureReason>(value))) != QStringLiteral("unknown"),
               "all diagnostic reasons have stable names");
    {
        auto unstarted = createStartButtonCapture();
        const auto diagnostic = startButtonCaptureDiagnosticSnapshot(*unstarted);
        expect(!unstarted->isActive() &&
                   diagnostic.value(QStringLiteral("hookLiveness")) == QStringLiteral("unknown"),
               "construction and diagnostics never install hooks or claim liveness");
        if (diagnostic.value(QStringLiteral("supported")).toBool())
            expect(
                !diagnostic.value(QStringLiteral("hookInstalled")).toBool() &&
                    !diagnostic.value(QStringLiteral("qualificationLeaseAvailable")).toBool() &&
                    !diagnostic.value(QStringLiteral("inputObserved")).toBool() &&
                    diagnostic.value(QStringLiteral("consumedDown")).toInteger() == 0,
                "unstarted native diagnostics separate installation/eligibility/input/consumption");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
