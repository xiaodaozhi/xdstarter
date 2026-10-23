// src/platform/start_button_capture_state.h

#pragma once

#include <QPoint>
#include <QRect>
#include <QtGlobal>

namespace xdstarter {

// A point-specific UIA observation, not a rectangular permission to intercept.
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
};

enum class StartMouseEvent { Move, LeftDown, LeftUp, OtherButton, Cancel };
struct StartMouseDecision {
    bool consume = false;
    bool toggle = false;
};

// Own both halves once a press is accepted. Losing eligibility cancels its
// action but never leaks an orphan release into the Shell. No input is replayed.
class StartButtonCaptureState final {
  public:
    [[nodiscard]] StartMouseDecision process(StartMouseEvent event, QPoint point, quint64 now,
                                             const StartButtonCaptureTarget& target, bool eligible,
                                             bool injected = false);
    void cancel();
    [[nodiscard]] bool ownsPress() const { return ownsPress_; }
    [[nodiscard]] static bool qualifies(const StartButtonCaptureTarget& target, QPoint point,
                                        quint64 now);

  private:
    bool ownsPress_ = false;
    bool canceled_ = false;
    QPoint origin_;
    quint64 pressedAt_ = 0;
    StartButtonCaptureTarget pressedTarget_;
};
} // namespace xdstarter
