// src/platform/start_button_capture_state.h

#pragma once

#include <QPoint>
#include <QRect>
#include <QtGlobal>

namespace xdstarter {

enum class StartButtonCaptureMode { Point, ExperimentalAreaCache };
enum class StartButtonCaptureReason {
    None,
    AcceptedDown,
    AcceptedRelease,
    AlreadyOwnedDown,
    NoOwnedPress,
    InjectedInput,
    OtherInput,
    KeyboardInput,
    ExplicitCancel,
    Disabled,
    SnapshotBusy,
    Modifiers,
    Unidentified,
    InvalidIdentity,
    InvalidGeometry,
    OutsideButton,
    FutureSnapshot,
    ExpiredSnapshot,
    PointDelta,
    PolicyMismatch,
    AreaNotValidated,
    EpochChanged,
    SessionSuperseded,
    NativeTargetChanged,
    PressOutside,
    PressMoved,
    PressTooLong,
    SubscriptionFailed,
    QueryFailed,
    QueryTimeout,
    QueryPointerMoved,
    InputDesktopUnavailable,
    Stopping,
    DispatchFailed,
    Count
};
[[nodiscard]] const char* startButtonCaptureReasonName(StartButtonCaptureReason reason);

enum class StartButtonSubscriptionStage { Ready, Root, Structure, Properties };
struct StartButtonSubscriptionOutcome {
    bool ready = false;
    StartButtonSubscriptionStage stage = StartButtonSubscriptionStage::Root;
    qint32 result = 0;
};
// HRESULT sign semantics, independent of COM/native calls for fault injection.
[[nodiscard]] StartButtonSubscriptionOutcome
startButtonSubscriptionOutcome(qint32 rootResult, qint32 structureResult, qint32 propertiesResult);

// Point observations remain the default. An area observation grants no input
// permission unless both the producer and the explicit consumer policy opt in.
struct StartButtonCaptureTarget {
    quintptr taskbar = 0;
    quintptr hitWindow = 0;
    quint32 processId = 0;
    QRect taskbarRect;
    QRect buttonRect;
    QPoint qualifiedPoint;
    quint64 observedAt = 0;
    quint64 epoch = 0;
    bool identified = false;
    quintptr monitor = 0;
    QRect monitorRect;
    unsigned int dpi = 0;
    StartButtonCaptureMode mode = StartButtonCaptureMode::Point;
    bool areaValidated = false;
    quint64 sessionGeneration = 0;
};

enum class StartMouseEvent { Move, LeftDown, LeftUp, OtherButton, Cancel };

struct StartButtonGestureIdentity {
    quint64 sessionGeneration = 0;
    quint64 id = 0;
    quint64 pressedAt = 0;
};

// Correlates observed physical halves, including releases drained after pause.
// IDs describe observations, never infer an intended click on Start.
class StartButtonGestureTracker final {
  public:
    [[nodiscard]] StartButtonGestureIdentity observe(StartMouseEvent event, quint64 session,
                                                     quint64 now, bool injected, bool ownsPress);

  private:
    quint64 next_ = 0;
    StartButtonGestureIdentity current_;
};

struct StartMouseDecision {
    bool consume = false;
    bool toggle = false;
    StartButtonCaptureReason reason = StartButtonCaptureReason::None;
};

// Own both halves once a press is accepted. Losing eligibility cancels its
// action but never leaks an orphan release into the Shell. No input is replayed.
class StartButtonCaptureState final {
  public:
    [[nodiscard]] StartMouseDecision
    process(StartMouseEvent event, QPoint point, quint64 now,
            const StartButtonCaptureTarget& target, bool eligible, bool injected = false,
            StartButtonCaptureReason ineligibleReason = StartButtonCaptureReason::Disabled,
            StartButtonCaptureMode mode = StartButtonCaptureMode::Point);
    void cancel(StartButtonCaptureReason reason = StartButtonCaptureReason::ExplicitCancel);
    [[nodiscard]] bool ownsPress() const { return ownsPress_; }
    [[nodiscard]] static bool
    qualifies(const StartButtonCaptureTarget& target, QPoint point, quint64 now,
              StartButtonCaptureMode mode = StartButtonCaptureMode::Point);
    [[nodiscard]] static StartButtonCaptureReason
    qualificationFailure(const StartButtonCaptureTarget& target, QPoint point, quint64 now,
                         StartButtonCaptureMode mode = StartButtonCaptureMode::Point);

  private:
    bool ownsPress_ = false;
    bool canceled_ = false;
    StartButtonCaptureReason cancelReason_ = StartButtonCaptureReason::None;
    QPoint origin_;
    quint64 pressedAt_ = 0;
    StartButtonCaptureTarget pressedTarget_;
};
} // namespace xdstarter
