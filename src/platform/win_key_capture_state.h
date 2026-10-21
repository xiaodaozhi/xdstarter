// src/platform/win_key_capture_state.h

#pragma once

#include <array>
#include <cstdint>

namespace xdstarter {

enum class WinKeyDecision { Forward, ReplaceRelease };

struct WinKeyEvent {
    std::uint16_t key = 0;
    bool down = false;
    bool injected = false;
    bool ownInjection = false;
};

// The native implementation sends one bounded batch: cancellation-key down/up
// and matching Win-up. Only a complete count allows eating the physical up.
class WinKeyInputDispatcher {
  public:
    virtual ~WinKeyInputDispatcher() = default;
    [[nodiscard]] virtual unsigned dispatchMarkAndRelease(std::uint16_t winKey) noexcept = 0;
    virtual void releaseDummy() noexcept = 0;
};

[[nodiscard]] bool dispatchStandaloneWinRelease(WinKeyInputDispatcher& dispatcher,
                                               std::uint16_t winKey) noexcept;

// Pure keyboard bookkeeping, independent of Win32/Qt. The caller must complete
// the replacement input transaction before suppressing a ReplaceRelease event.
class WinKeyCaptureState {
  public:
    static constexpr std::uint16_t leftWin = 0x5B;
    static constexpr std::uint16_t rightWin = 0x5C;
    using HeldKeys = std::array<bool, 256>;

    void reset(const HeldKeys& held = {});
    void cancelCandidate();
    [[nodiscard]] WinKeyDecision process(const WinKeyEvent& event);

  private:
    [[nodiscard]] bool anyOtherHeld(std::uint16_t key) const;
    HeldKeys held_{};
    HeldKeys injectedHeld_{};
    std::uint16_t candidate_ = 0;
};

} // namespace xdstarter
