// src/platform/win_key_capture_state.cpp

#include "win_key_capture_state.h"

namespace xdstarter {

bool dispatchStandaloneWinRelease(WinKeyInputDispatcher& dispatcher,
                                 std::uint16_t winKey) noexcept {
    const auto inserted = dispatcher.dispatchMarkAndRelease(winKey);
    if (inserted > 0 && inserted < 3) {
        // The API does not identify exactly which events survived a partial
        // batch. Cleanup is best effort; the physical Win-up still passes.
        dispatcher.releaseDummy();
    }
    return inserted == 3;
}

void WinKeyCaptureState::reset(const HeldKeys& held) {
    held_ = held;
    injectedHeld_.fill(false);
    candidate_ = 0;
}

void WinKeyCaptureState::cancelCandidate() {
    candidate_ = 0;
}

bool WinKeyCaptureState::anyOtherHeld(std::uint16_t key) const {
    for (std::uint16_t index = 0; index < held_.size(); ++index) {
        if ((index != key && held_[index]) || injectedHeld_[index]) {
            return true;
        }
    }
    return false;
}

WinKeyDecision WinKeyCaptureState::process(const WinKeyEvent& event) {
    if (event.ownInjection) {
        return WinKeyDecision::Forward;
    }
    if (event.key >= held_.size()) {
        cancelCandidate();
        return WinKeyDecision::Forward;
    }
    if (event.injected) {
        injectedHeld_[event.key] = event.down;
        cancelCandidate();
        return WinKeyDecision::Forward;
    }

    const bool wasDown = held_[event.key];
    held_[event.key] = event.down;
    const bool win = event.key == leftWin || event.key == rightWin;
    if (!win) {
        cancelCandidate();
        return WinKeyDecision::Forward;
    }
    if (event.down) {
        if (!wasDown) {
            candidate_ = anyOtherHeld(event.key) ? 0 : event.key;
        }
        return WinKeyDecision::Forward;
    }

    const bool replace = wasDown && candidate_ == event.key && !anyOtherHeld(event.key);
    cancelCandidate();
    return replace ? WinKeyDecision::ReplaceRelease : WinKeyDecision::Forward;
}

} // namespace xdstarter
