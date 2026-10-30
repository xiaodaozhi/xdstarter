#include "picker_request_state.h"

#include <algorithm>
#include <limits>

namespace xdstarter {

std::optional<PickerRequestState::Id> PickerRequestState::begin() {
    if (stopping_ || current_ || pending_.size() >= maximumOutstanding ||
        next_ == std::numeric_limits<Id>::max())
        return std::nullopt;
    const Id id = next_++;
    pending_.push_back(id);
    current_ = id;
    return id;
}

std::optional<PickerRequestState::Id> PickerRequestState::cancel() {
    const auto id = current_;
    current_.reset();
    return id;
}

PickerRequestState::Delivery PickerRequestState::finish(Id id, Result result) {
    const auto found = std::find(pending_.begin(), pending_.end(), id);
    if (found == pending_.end())
        return Delivery::Ignore;
    pending_.erase(found);
    if (stopping_ || current_ != id)
        return Delivery::Ignore;
    current_.reset();
    switch (result) {
    case Result::Accepted: return Delivery::Accepted;
    case Result::Rejected: return Delivery::Rejected;
    case Result::Failed: return Delivery::Failed;
    }
    return Delivery::Ignore;
}

std::vector<PickerRequestState::Id> PickerRequestState::shutdown() {
    if (stopping_)
        return {};
    stopping_ = true;
    current_.reset();
    return pending_;
}

} // namespace xdstarter
