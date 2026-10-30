#include "core/picker_request_state.h"

#include <iostream>

using xdstarter::PickerRequestState;

int main() {
    int failures = 0;
    const auto expect = [&](bool value, const char* name) {
        if (!value) {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    };
    PickerRequestState state;
    const auto first = state.begin();
    expect(first.has_value() && !state.begin(), "one current request");
    expect(state.cancel() == first && !state.current(), "cancel ends the current request immediately");
    const auto second = state.begin();
    expect(second && first && *second != *first, "a new request has a distinct identity");
    if (!first || !second)
        return 1;
    expect(state.finish(*first, PickerRequestState::Result::Accepted) == PickerRequestState::Delivery::Ignore &&
               state.current() == second,
           "late acceptance from a canceled request cannot affect the newer request");
    expect(state.finish(*first, PickerRequestState::Result::Failed) == PickerRequestState::Delivery::Ignore,
           "duplicate completion is ignored");
    expect(state.finish(*second, PickerRequestState::Result::Accepted) == PickerRequestState::Delivery::Accepted &&
               !state.current() && state.outstanding() == 0,
           "only the current request delivers its selection");
    const auto rejected = state.begin();
    expect(rejected && state.finish(*rejected, PickerRequestState::Result::Rejected) ==
                           PickerRequestState::Delivery::Rejected,
           "normal cancel is delivered once");
    const auto failed = state.begin();
    expect(failed && state.finish(*failed, PickerRequestState::Result::Failed) ==
                         PickerRequestState::Delivery::Failed,
           "current native failure remains visible to the caller");
    for (std::size_t index = 0; index < PickerRequestState::maximumOutstanding; ++index) {
        const auto id = state.begin();
        expect(id.has_value() && state.cancel() == id, "cancelled requests retain bounded cleanup state");
    }
    expect(!state.begin(), "too many uncompleted native requests fail closed");
    const auto pending = state.shutdown();
    expect(pending.size() == PickerRequestState::maximumOutstanding && state.stopping() && !state.begin(),
           "shutdown returns all cleanup identities without starting another request");
    expect(state.shutdown().empty(), "repeated shutdown does not issue duplicate cancellation");
    for (const auto id : pending)
        expect(state.finish(id, PickerRequestState::Result::Accepted) == PickerRequestState::Delivery::Ignore,
               "shutdown never publishes a late selection");
    expect(state.outstanding() == 0, "completed requests release cleanup bookkeeping");
    return failures ? 1 : 0;
}
