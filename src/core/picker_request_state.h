#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace xdstarter {

// GUI-thread bookkeeping only: native process/owner identity is verified by
// the platform adapter. A stale completion must never affect a newer request.
class PickerRequestState {
  public:
    using Id = std::uint64_t;
    enum class Result { Accepted, Rejected, Failed };
    enum class Delivery { Ignore, Accepted, Rejected, Failed };
    static constexpr std::size_t maximumOutstanding = 8;

    [[nodiscard]] std::optional<Id> begin();
    [[nodiscard]] std::optional<Id> cancel();
    [[nodiscard]] Delivery finish(Id id, Result result);
    [[nodiscard]] std::vector<Id> shutdown();
    [[nodiscard]] std::optional<Id> current() const { return current_; }
    [[nodiscard]] std::size_t outstanding() const { return pending_.size(); }
    [[nodiscard]] bool stopping() const { return stopping_; }

  private:
    Id next_ = 1;
    std::optional<Id> current_;
    std::vector<Id> pending_;
    bool stopping_ = false;
};

} // namespace xdstarter
