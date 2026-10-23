// src/platform/start_button_capture_state.cpp

#include "start_button_capture_state.h"

namespace xdstarter {
bool StartButtonCaptureState::qualifies(const StartButtonCaptureTarget& target, QPoint point,
                                        quint64 now) {
    return target.identified && target.taskbar && target.hitWindow && target.processId &&
           target.monitor && target.monitorRect.isValid() && target.dpi > 0 && target.dpi <= 768 &&
           target.buttonRect.isValid() && target.buttonRect.width() <= 256 &&
           target.buttonRect.height() <= 256 && target.taskbarRect.contains(target.buttonRect) &&
           target.buttonRect.contains(point) && now >= target.observedAt &&
           now - target.observedAt <= 200 &&
           qAbs(qint64(point.x()) - target.qualifiedPoint.x()) <= 1 &&
           qAbs(qint64(point.y()) - target.qualifiedPoint.y()) <= 1;
}

void StartButtonCaptureState::cancel() { canceled_ = true; }

StartMouseDecision StartButtonCaptureState::process(StartMouseEvent event, QPoint point,
                                                    quint64 now,
                                                    const StartButtonCaptureTarget& target,
                                                    bool eligible, bool injected) {
    if (injected) {
        cancel();
        return {};
    }
    if (event == StartMouseEvent::Cancel || event == StartMouseEvent::OtherButton) {
        cancel();
        return {};
    }
    if (ownsPress_ &&
        (!eligible || !pressedTarget_.buttonRect.contains(point) ||
         qAbs(qint64(point.x()) - origin_.x()) > 4 || qAbs(qint64(point.y()) - origin_.y()) > 4))
        cancel();
    if (event == StartMouseEvent::LeftDown) {
        if (ownsPress_)
            return {true, false};
        if (!eligible || !qualifies(target, point, now))
            return {};
        ownsPress_ = true;
        canceled_ = false;
        origin_ = point;
        pressedAt_ = now;
        pressedTarget_ = target;
        return {true, false};
    }
    if (event == StartMouseEvent::LeftUp && ownsPress_) {
        ownsPress_ = false;
        const bool sameTarget =
            target.epoch == pressedTarget_.epoch && target.monitor == pressedTarget_.monitor &&
            target.monitorRect == pressedTarget_.monitorRect && target.dpi == pressedTarget_.dpi &&
            target.taskbar == pressedTarget_.taskbar &&
            target.processId == pressedTarget_.processId &&
            target.buttonRect == pressedTarget_.buttonRect &&
            target.taskbarRect == pressedTarget_.taskbarRect;
        const bool toggle = !canceled_ && eligible && sameTarget && now >= pressedAt_ &&
                            now - pressedAt_ <= 1500 && qualifies(target, point, now);
        return {true, toggle};
    }
    return {};
}
} // namespace xdstarter
