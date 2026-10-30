// Test-only model for comparing recovery-gate ideas. It is not linked into the
// product and does not establish delivery ordering for real Windows input.
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace recovery_safety {

struct ClockSample {
    std::uint64_t inclusiveBeforeMs = 0;
    std::uint64_t unbiased100ns = 0;
    std::uint64_t inclusiveAfterMs = 0;
    bool readSucceeded = false;
};

enum class ClockResult { WithinTolerance, Discontinuity, Ambiguous, Invalid };

inline bool valid(const ClockSample& sample) {
    return sample.readSucceeded && sample.inclusiveBeforeMs <= sample.inclusiveAfterMs;
}

// The unbiased reading is taken somewhere between the two inclusive readings.
// Bounds represent only the injected samples and caller-supplied tolerance;
// WithinTolerance is not proof that no brief sleep occurred.
inline ClockResult compare(const ClockSample& previous, const ClockSample& current,
                           std::uint64_t toleranceMs) {
    if (!valid(previous) || !valid(current) ||
        current.inclusiveBeforeMs < previous.inclusiveAfterMs ||
        current.unbiased100ns < previous.unbiased100ns)
        return ClockResult::Invalid;
    const auto wallLower = current.inclusiveBeforeMs - previous.inclusiveAfterMs;
    const auto wallUpper = current.inclusiveAfterMs - previous.inclusiveBeforeMs;
    const auto unbiasedDelta = current.unbiased100ns - previous.unbiased100ns;
    const auto unbiasedFloor = unbiasedDelta / 10000;
    const auto unbiasedCeiling = unbiasedFloor + (unbiasedDelta % 10000 != 0);
    if (unbiasedFloor > wallUpper && unbiasedFloor - wallUpper > toleranceMs)
        return ClockResult::Invalid;
    const auto gapLower = wallLower > unbiasedCeiling ? wallLower - unbiasedCeiling : 0;
    const auto gapUpper = wallUpper > unbiasedFloor ? wallUpper - unbiasedFloor : 0;
    if (gapLower > toleranceMs)
        return ClockResult::Discontinuity;
    return gapUpper > toleranceMs ? ClockResult::Ambiguous : ClockResult::WithinTolerance;
}

class Gate {
  public:
    // A new attempt revokes the previous token, even when registration fails.
    // No notification or clock observation may silently restart capture.
    bool arm(bool powerSourceReady, bool sessionSourceReady, bool desktopConfirmed,
             bool ownedInputDrained, std::uint32_t sessionId, const ClockSample& baseline) {
        active_ = false;
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            return false;
        ++generation_;
        if (!powerSourceReady || !sessionSourceReady || !desktopConfirmed ||
            !ownedInputDrained || !sessionId || !valid(baseline))
            return false;
        sessionId_ = sessionId;
        previous_ = baseline;
        active_ = true;
        return true;
    }

    [[nodiscard]] bool active(std::uint64_t token) const {
        return active_ && token == generation_;
    }
    [[nodiscard]] std::uint64_t token() const { return generation_; }

    bool powerNotification(std::uint64_t token) { return invalidate(token); }

    // An event for another known session is irrelevant; missing identity is
    // ambiguous and invalidates rather than guessing which session changed.
    bool sessionNotification(std::uint64_t token, std::optional<std::uint32_t> sessionId) {
        if (!active(token))
            return false;
        if (sessionId && *sessionId != sessionId_)
            return false;
        return invalidate(token);
    }

    ClockResult observe(std::uint64_t token, const ClockSample& sample,
                        std::uint64_t toleranceMs) {
        if (!active(token))
            return ClockResult::Invalid;
        const auto result = compare(previous_, sample, toleranceMs);
        if (result == ClockResult::WithinTolerance)
            previous_ = sample;
        else
            invalidate(token);
        return result;
    }

  private:
    bool invalidate(std::uint64_t token) {
        if (!active(token))
            return false;
        active_ = false;
        return true;
    }

    std::uint64_t generation_ = 0;
    std::uint32_t sessionId_ = 0;
    ClockSample previous_{};
    bool active_ = false;
};

int runTests();

} // namespace recovery_safety
