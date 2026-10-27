// src/platform/windows/windows_start_button_events.h

#pragma once

#include "platform/start_button_capture_state.h"

#include <QJsonObject>

#include <windows.h>

#include <array>
#include <atomic>

namespace xdstarter {
struct StartButtonWindowEvent {
    DWORD event = 0;
    HWND window = nullptr;
    LONG objectId = 0;
    LONG childId = 0;
    DWORD threadId = 0;
    DWORD eventTime = 0;
};

enum class StartButtonWindowEventAction { Unrelated, AccessibilityObject, Invalidate };
struct StartButtonWindowEventDecision {
    StartButtonWindowEventAction action = StartButtonWindowEventAction::Unrelated;
    HWND root = nullptr;
};

// Native window invalidation and Start UIA element invalidation have separate
// scopes. This does not qualify a button or grant permission to consume input.
[[nodiscard]] StartButtonWindowEventDecision
classifyStartButtonWindowEvent(const StartButtonWindowEvent& event, HWND taskbar,
                               StartButtonCaptureMode mode);

class StartButtonWindowEventJournal final {
  public:
    static constexpr size_t capacity = 128;
    // One writer: the native Hook/WinEvent thread. Export may run on the GUI.
    void record(const StartButtonWindowEvent& event, StartButtonWindowEventDecision decision,
                HWND taskbar, quint64 session, quint64 epochBefore, quint64 epochAfter);
    [[nodiscard]] quint64 sequence() const { return sequence_.load(); }
    [[nodiscard]] QJsonObject snapshot() const;

  private:
    struct Sample {
        std::atomic<quint64> sequence{0}, at{0}, session{0}, before{0}, after{0};
        std::atomic<quintptr> window{0}, root{0}, taskbar{0};
        std::atomic<DWORD> event{0}, thread{0}, time{0};
        std::atomic<LONG> object{0}, child{0};
        std::atomic<int> action{0};
    };
    std::array<Sample, capacity> samples_{};
    std::atomic<quint64> sequence_{0}, invalidations_{0}, accessibilityObjects_{0};
};
} // namespace xdstarter
