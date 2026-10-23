// src/platform/start_button_capture_state.cpp

#include "start_button_capture_state.h"

namespace xdstarter {
StartButtonSubscriptionOutcome
startButtonSubscriptionOutcome(qint32 rootResult, qint32 structureResult, qint32 propertiesResult) {
    if (rootResult < 0)
        return {false, StartButtonSubscriptionStage::Root, rootResult};
    if (structureResult < 0)
        return {false, StartButtonSubscriptionStage::Structure, structureResult};
    if (propertiesResult < 0)
        return {false, StartButtonSubscriptionStage::Properties, propertiesResult};
    return {true, StartButtonSubscriptionStage::Ready, propertiesResult};
}

const char* startButtonCaptureReasonName(StartButtonCaptureReason reason) {
    switch (reason) {
#define START_REASON(value, name)                                                                  \
    case StartButtonCaptureReason::value:                                                          \
        return name
        START_REASON(None, "none");
        START_REASON(AcceptedDown, "accepted-down");
        START_REASON(AcceptedRelease, "accepted-release");
        START_REASON(AlreadyOwnedDown, "already-owned-down");
        START_REASON(NoOwnedPress, "no-owned-press");
        START_REASON(InjectedInput, "injected-input");
        START_REASON(OtherInput, "other-input");
        START_REASON(KeyboardInput, "keyboard-input");
        START_REASON(ExplicitCancel, "explicit-cancel");
        START_REASON(Disabled, "disabled");
        START_REASON(SnapshotBusy, "snapshot-busy");
        START_REASON(Modifiers, "modifiers");
        START_REASON(Unidentified, "unidentified");
        START_REASON(InvalidIdentity, "invalid-identity");
        START_REASON(InvalidGeometry, "invalid-geometry");
        START_REASON(OutsideButton, "outside-button");
        START_REASON(FutureSnapshot, "future-snapshot");
        START_REASON(ExpiredSnapshot, "expired-snapshot");
        START_REASON(PointDelta, "point-delta");
        START_REASON(PolicyMismatch, "policy-mismatch");
        START_REASON(AreaNotValidated, "area-not-validated");
        START_REASON(EpochChanged, "epoch-changed");
        START_REASON(SessionSuperseded, "session-superseded");
        START_REASON(NativeTargetChanged, "native-target-changed");
        START_REASON(PressOutside, "press-outside");
        START_REASON(PressMoved, "press-moved");
        START_REASON(PressTooLong, "press-too-long");
        START_REASON(SubscriptionFailed, "subscription-failed");
        START_REASON(QueryFailed, "query-failed");
        START_REASON(QueryTimeout, "query-timeout");
        START_REASON(QueryPointerMoved, "query-pointer-moved");
        START_REASON(InputDesktopUnavailable, "input-desktop-unavailable");
        START_REASON(Stopping, "stopping");
        START_REASON(DispatchFailed, "dispatch-failed");
    case StartButtonCaptureReason::Count:
        break;
#undef START_REASON
    }
    return "unknown";
}

bool StartButtonCaptureState::qualifies(const StartButtonCaptureTarget& target, QPoint point,
                                        quint64 now, StartButtonCaptureMode mode) {
    return qualificationFailure(target, point, now, mode) == StartButtonCaptureReason::None;
}

StartButtonCaptureReason
StartButtonCaptureState::qualificationFailure(const StartButtonCaptureTarget& target, QPoint point,
                                              quint64 now, StartButtonCaptureMode mode) {
    using Reason = StartButtonCaptureReason;
    if (!target.identified)
        return Reason::Unidentified;
    if (target.mode != mode)
        return Reason::PolicyMismatch;
    if (mode == StartButtonCaptureMode::ExperimentalAreaCache && !target.areaValidated)
        return Reason::AreaNotValidated;
    if (!target.taskbar || !target.hitWindow || !target.processId || !target.monitor ||
        !target.monitorRect.isValid() || !target.dpi || target.dpi > 768)
        return Reason::InvalidIdentity;
    if (!target.buttonRect.isValid() || target.buttonRect.width() > 256 ||
        target.buttonRect.height() > 256 || !target.taskbarRect.contains(target.buttonRect))
        return Reason::InvalidGeometry;
    if (!target.buttonRect.contains(point))
        return Reason::OutsideButton;
    if (now < target.observedAt)
        return Reason::FutureSnapshot;
    if (now - target.observedAt > 200)
        return Reason::ExpiredSnapshot;
    if (mode == StartButtonCaptureMode::Point &&
        (qAbs(qint64(point.x()) - target.qualifiedPoint.x()) > 1 ||
         qAbs(qint64(point.y()) - target.qualifiedPoint.y()) > 1))
        return Reason::PointDelta;
    return Reason::None;
}

void StartButtonCaptureState::cancel(StartButtonCaptureReason reason) {
    if (!canceled_)
        cancelReason_ = reason;
    canceled_ = true;
}

StartMouseDecision StartButtonCaptureState::process(StartMouseEvent event, QPoint point,
                                                    quint64 now,
                                                    const StartButtonCaptureTarget& target,
                                                    bool eligible, bool injected,
                                                    StartButtonCaptureReason ineligibleReason,
                                                    StartButtonCaptureMode mode) {
    using Reason = StartButtonCaptureReason;
    if (injected) {
        cancel(Reason::InjectedInput);
        return {false, false, Reason::InjectedInput};
    }
    if (event == StartMouseEvent::Cancel || event == StartMouseEvent::OtherButton) {
        const auto reason =
            event == StartMouseEvent::Cancel ? Reason::ExplicitCancel : Reason::OtherInput;
        cancel(reason);
        return {false, false, reason};
    }
    if (ownsPress_) {
        if (!eligible)
            cancel(ineligibleReason);
        else if (!pressedTarget_.buttonRect.contains(point))
            cancel(Reason::PressOutside);
        else if (qAbs(qint64(point.x()) - origin_.x()) > 4 ||
                 qAbs(qint64(point.y()) - origin_.y()) > 4)
            cancel(Reason::PressMoved);
    }
    if (event == StartMouseEvent::LeftDown) {
        if (ownsPress_)
            return {true, false, Reason::AlreadyOwnedDown};
        if (!eligible)
            return {false, false, ineligibleReason};
        const auto failure = qualificationFailure(target, point, now, mode);
        if (failure != Reason::None)
            return {false, false, failure};
        ownsPress_ = true;
        canceled_ = false;
        cancelReason_ = Reason::None;
        origin_ = point;
        pressedAt_ = now;
        pressedTarget_ = target;
        return {true, false, Reason::AcceptedDown};
    }
    if (event == StartMouseEvent::LeftUp && ownsPress_) {
        ownsPress_ = false;
        const bool sameTarget =
            target.sessionGeneration == pressedTarget_.sessionGeneration &&
            target.epoch == pressedTarget_.epoch && target.monitor == pressedTarget_.monitor &&
            target.monitorRect == pressedTarget_.monitorRect && target.dpi == pressedTarget_.dpi &&
            target.taskbar == pressedTarget_.taskbar &&
            target.processId == pressedTarget_.processId &&
            target.buttonRect == pressedTarget_.buttonRect &&
            target.taskbarRect == pressedTarget_.taskbarRect;
        Reason reason = canceled_ ? cancelReason_ : Reason::None;
        if (reason == Reason::None && !sameTarget)
            reason = target.sessionGeneration != pressedTarget_.sessionGeneration
                         ? Reason::SessionSuperseded
                     : target.epoch != pressedTarget_.epoch ? Reason::EpochChanged
                                                            : Reason::NativeTargetChanged;
        if (reason == Reason::None && (now < pressedAt_ || now - pressedAt_ > 1500))
            reason = Reason::PressTooLong;
        if (reason == Reason::None)
            reason = qualificationFailure(target, point, now, mode);
        return {true, reason == Reason::None,
                reason == Reason::None ? Reason::AcceptedRelease : reason};
    }
    return {false, false, event == StartMouseEvent::LeftUp ? Reason::NoOwnedPress : Reason::None};
}
} // namespace xdstarter
