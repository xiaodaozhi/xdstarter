// tests/platform/start_button_capture_tests.cpp

#include "platform/start_button_capture_state.h"

#include <iostream>

using namespace xdstarter;
namespace {
int checks = 0;
int failures = 0;
void expect(bool value, const char* reason) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << reason << '\n';
    }
}
StartButtonCaptureTarget target() {
    return {1,
            2,
            42,
            QRect(-1920, 1040, 1920, 40),
            QRect(-1920, 1040, 48, 40),
            QPoint(-1900, 1060),
            1000,
            3,
            true,
            7,
            QRect(-1920, 0, 1920, 1080),
            96};
}
} // namespace

int main() {
    const auto ready = target();
    const auto point = ready.qualifiedPoint;
    expect(StartButtonCaptureState::qualifies(ready, point, 1200), "200ms boundary qualifies");
    expect(!StartButtonCaptureState::qualifies(ready, point, 1201),
           "stale snapshot passes through");
    expect(!StartButtonCaptureState::qualifies(ready, point, 999), "future timestamps rejected");
    expect(!StartButtonCaptureState::qualifies(ready, point + QPoint(2, 0), 1000),
           "rectangle alone cannot authorize another point");
    for (int mutation = 0; mutation < 10; ++mutation) {
        auto changed = ready;
        switch (mutation) {
        case 0:
            changed.identified = false;
            break;
        case 1:
            changed.taskbar = 0;
            break;
        case 2:
            changed.hitWindow = 0;
            break;
        case 3:
            changed.processId = 0;
            break;
        case 4:
            changed.buttonRect = QRect(-1920, 1040, 257, 40);
            break;
        case 5:
            changed.taskbarRect = QRect();
            break;
        case 6:
            changed.qualifiedPoint += QPoint(0, 2);
            break;
        case 7:
            changed.monitor = 0;
            break;
        case 8:
            changed.monitorRect = {};
            break;
        case 9:
            changed.dpi = 0;
            break;
        }
        expect(!StartButtonCaptureState::qualifies(changed, point, 1000),
               "invalid identity or geometry");
    }
    StartButtonCaptureState state;
    expect(!state.process(StartMouseEvent::LeftUp, point, 1000, ready, true).consume,
           "release without captured down remains native");
    expect(!state.process(StartMouseEvent::LeftDown, point, 1000, ready, false).consume,
           "disabled press remains native");
    for (int click = 0; click < 2; ++click) {
        expect(state.process(StartMouseEvent::LeftDown, point, 1000, ready, true).consume,
               "each press in a double click is owned");
        const auto up = state.process(StartMouseEvent::LeftUp, point, 1050, ready, true);
        expect(up.consume && up.toggle && !state.ownsPress(), "click toggles once on release");
    }
    // Every invalidation after down still consumes exactly that matching up.
    for (int mutation = 0; mutation < 15; ++mutation) {
        (void)state.process(StartMouseEvent::LeftDown, point, 1000, ready, true);
        auto changed = ready;
        bool eligible = true;
        quint64 time = 1050;
        switch (mutation) {
        case 0:
            eligible = false;
            break;
        case 1:
            ++changed.epoch;
            break;
        case 2:
            ++changed.taskbar;
            break;
        case 3:
            ++changed.processId;
            break;
        case 4:
            changed.buttonRect.translate(1, 0);
            break;
        case 5:
            changed.taskbarRect.translate(1, 0);
            break;
        case 6:
            time = 1201;
            break;
        case 7:
            time = 2600;
            changed.observedAt = time;
            break;
        case 8:
            state.cancel();
            break;
        case 9:
            expect(!state.process(StartMouseEvent::OtherButton, point, 1020, ready, true).consume,
                   "right/middle/wheel remain native");
            break;
        case 10:
            (void)state.process(StartMouseEvent::Move, point + QPoint(10, 0), 1020, ready, true);
            break;
        case 11:
            expect(!state.process(StartMouseEvent::Move, point, 1020, ready, true, true).consume,
                   "external injected input remains native and cancels candidate");
            break;
        case 12:
            ++changed.monitor;
            break;
        case 13:
            ++changed.dpi;
            break;
        case 14:
            changed.monitorRect.translate(1, 0);
            break;
        }
        const auto up = state.process(StartMouseEvent::LeftUp, point, time, changed, eligible);
        expect(up.consume && !up.toggle && !state.ownsPress(), "canceled press owns its release");
        expect(!state.process(StartMouseEvent::LeftUp, point, time, changed, eligible).consume,
               "only one release is consumed");
    }
    expect(!state.process(StartMouseEvent::LeftDown, point, 1000, ready, true, true).consume,
           "synthetic clicks never activate production capture");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
