#include "recovery_safety_model.h"

#include "platform/start_button_capture_state.h"
#include "platform/win_key_capture_state.h"

#include <QPoint>
#include <QRect>

#include <array>
#include <iostream>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace recovery_safety {
namespace {
int checks = 0;
int failures = 0;

void expect(bool value, const char* description) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "Recovery model FAIL: " << description << '\n';
    }
}

ClockSample clockAt(std::uint64_t tickMs, std::uint64_t awakeMs) {
    return {tickMs, awakeMs * 10000, tickMs, true};
}

xdstarter::StartButtonCaptureTarget target() {
    xdstarter::StartButtonCaptureTarget result;
    result.taskbar = 1;
    result.hitWindow = 2;
    result.processId = 42;
    result.taskbarRect = QRect(0, 1000, 1000, 50);
    result.buttonRect = QRect(0, 1000, 50, 50);
    result.qualifiedPoint = QPoint(20, 1020);
    result.observedAt = 1000;
    result.epoch = 1;
    result.identified = true;
    result.monitor = 3;
    result.monitorRect = QRect(0, 0, 1000, 1050);
    result.dpi = 96;
    result.sessionGeneration = 1;
    return result;
}

// A test adapter deliberately invokes the same gesture bookkeeping as the
// product, but the gate and all notification sources remain test-only.
struct Fixture {
    Gate gate;
    xdstarter::StartButtonCaptureState mouse;
    xdstarter::WinKeyCaptureState keyboard;
    std::uint64_t token = 0;
    xdstarter::StartButtonCaptureTarget current = target();

    bool arm(bool powerReady = true, bool sessionReady = true, bool desktopReady = true,
             bool keyboardReconciled = true) {
        const bool ok = gate.arm(powerReady, sessionReady, desktopReady,
                                 !mouse.ownsPress() && keyboardReconciled, 42, clockAt(1000, 1000));
        token = gate.token();
        if (ok) {
            keyboard.reset();
            current.sessionGeneration = token;
        }
        return ok;
    }

    void cancelInputs() {
        mouse.cancel(xdstarter::StartButtonCaptureReason::Disabled);
        keyboard.cancelCandidate();
    }

    void power(std::uint64_t sourceToken) {
        if (gate.powerNotification(sourceToken))
            cancelInputs();
    }

    void session(std::uint64_t sourceToken, std::optional<std::uint32_t> sessionId) {
        if (gate.sessionNotification(sourceToken, sessionId))
            cancelInputs();
    }

    ClockResult sample(const ClockSample& value, std::uint64_t toleranceMs) {
        const auto result = gate.observe(token, value, toleranceMs);
        if (result != ClockResult::WithinTolerance && !gate.active(token))
            cancelInputs();
        return result;
    }

    xdstarter::StartMouseDecision mouseEvent(xdstarter::StartMouseEvent event, std::uint64_t now) {
        return mouse.process(event, current.qualifiedPoint, now, current, gate.active(token), false,
                             xdstarter::StartButtonCaptureReason::Disabled);
    }
};

void clockChecks() {
    const auto baseline = clockAt(1000, 1000);
    expect(compare(baseline, clockAt(1100, 1100), 2) == ClockResult::WithinTolerance,
           "equal injected clocks stay within tolerance");
    expect(compare(baseline, clockAt(1300, 1100), 2) == ClockResult::Discontinuity,
           "a measured inclusive-only gap identifies a discontinuity");
    expect(compare(baseline, {1100, 1100 * 10000ULL, 1104, true}, 2) == ClockResult::Ambiguous,
           "non-atomic readings crossing the tolerance remain ambiguous");
    expect(compare(baseline, {1100, 1100 * 10000ULL, 1100, false}, 2) == ClockResult::Invalid,
           "a failed clock read is invalid");
    expect(compare(baseline, clockAt(999, 1100), 2) == ClockResult::Invalid,
           "overlapping or reversed wall-clock samples are invalid");
    expect(compare(baseline, clockAt(1100, 999), 2) == ClockResult::Invalid,
           "a reversed unbiased clock is invalid");
    expect(compare(baseline, clockAt(1100, 1200), 2) == ClockResult::Invalid,
           "awake time greater than the inclusive bound is invalid");
    expect(compare(baseline, clockAt(1101, 1100), 2) == ClockResult::WithinTolerance,
           "a gap below the assumed tolerance is indistinguishable from continuous time");
    const auto top = std::numeric_limits<std::uint64_t>::max();
    expect(compare({top - 20, top - 20000, top - 20, true},
                   {top - 10, top - 10000, top - 10, true}, 2) == ClockResult::Discontinuity,
           "large counters use bounded subtraction without arithmetic overflow");
}

void registrationAndSessions() {
    Fixture fixture;
    for (const auto& flags : std::array<std::array<bool, 4>, 4>{{
             {{false, true, true, true}}, {{true, false, true, true}},
             {{true, true, false, true}}, {{true, true, true, false}}}}) {
        expect(!fixture.arm(flags[0], flags[1], flags[2], flags[3]) &&
                   !fixture.gate.active(fixture.token),
               "missing registration, desktop confirmation or reconciled input refuses arming");
    }
    Gate invalid;
    expect(!invalid.arm(true, true, true, true, 42, {1000, 10000, 999, true}),
           "an invalid baseline cannot arm the model");
    expect(!invalid.arm(true, true, true, true, 0, clockAt(1000, 1000)),
           "unknown session identity cannot arm the model");
    expect(fixture.arm(), "valid prerequisites permit explicit model arming");
    const auto previous = fixture.token;
    fixture.session(previous, 43);
    expect(fixture.gate.active(previous), "another identified session does not invalidate this session");
    fixture.session(previous, std::nullopt);
    expect(!fixture.gate.active(previous), "missing session identity fails closed");
    fixture.session(previous, 42);
    expect(!fixture.gate.active(previous), "a later session event does not auto-resume capture");
    expect(fixture.arm(), "fresh explicit arming creates a replacement session");
    const auto replacement = fixture.token;
    fixture.power(previous);
    expect(fixture.gate.active(replacement), "a stale notification cannot stop the replacement token");
    fixture.power(replacement);
    fixture.power(replacement);
    expect(!fixture.gate.active(replacement), "reentrant notifications remain idempotently paused");
}

void gestureOrdering() {
    using namespace xdstarter;
    Fixture fixture;
    expect(fixture.arm(), "gesture fixture can arm");
    const auto old = fixture.token;
    const auto down = fixture.mouseEvent(StartMouseEvent::LeftDown, 1000);
    const auto winDown = fixture.keyboard.process({WinKeyCaptureState::leftWin, true, false, false});
    expect(down.consume && fixture.mouse.ownsPress() && winDown == WinKeyDecision::Forward,
           "an owned Start down and forwarded Win down can precede a power event");
    fixture.power(old);
    expect(!fixture.arm(), "an undrained owned press prevents model rearming");
    const auto up = fixture.mouseEvent(StartMouseEvent::LeftUp, 1050);
    const auto winUp = fixture.keyboard.process({WinKeyCaptureState::leftWin, false, false, false});
    expect(up.consume && !up.toggle && !fixture.mouse.ownsPress(),
           "after suspension the owned release drains without activating the menu");
    expect(winUp == WinKeyDecision::Forward,
           "a Win candidate canceled by suspension forwards the release");
    expect(!fixture.mouseEvent(StartMouseEvent::LeftDown, 1060).consume,
           "new gestures are forwarded while the modeled gate is inactive");
    expect(fixture.arm(), "a drained gesture permits explicit rearming");
    fixture.power(old);
    expect(fixture.gate.active(fixture.token), "a late prior-generation power callback is ignored");

    Fixture missingNotification;
    expect(missingNotification.arm(), "clock fixture can arm");
    expect(missingNotification.sample(clockAt(1300, 1100), 2) == ClockResult::Discontinuity &&
               !missingNotification.gate.active(missingNotification.token),
           "a sampled discontinuity invalidates even when a notification was lost");
    expect(!missingNotification.mouseEvent(StartMouseEvent::LeftDown, 1300).consume,
           "input after a detected discontinuity is forwarded");

    Fixture ambiguous;
    expect(ambiguous.arm(), "ambiguous clock fixture can arm");
    expect(ambiguous.sample({1100, 1100 * 10000ULL, 1104, true}, 2) == ClockResult::Ambiguous &&
               !ambiguous.gate.active(ambiguous.token),
           "ambiguous sample ordering invalidates the model");

    // Counterexample, not a safety guarantee: if sleep occurs but neither its
    // notification nor a new observation is available before input, the gate
    // sees precisely the same state as normal execution and can still consume.
    Fixture unseenWake;
    expect(unseenWake.arm(), "unobserved wake fixture can arm");
    const auto earlyDown = unseenWake.mouseEvent(StartMouseEvent::LeftDown, 1000);
    const auto earlyUp = unseenWake.mouseEvent(StartMouseEvent::LeftUp, 1050);
    expect(earlyDown.consume && earlyUp.toggle,
           "counterexample: input before any wake observation is indistinguishable from normal input");
}

#ifdef Q_OS_WIN
struct NativeFixture {
    Fixture fixture;
    std::uint32_t expectedSession = 42;
};

LRESULT CALLBACK fixtureWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<NativeFixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state && message == WM_POWERBROADCAST &&
        (wParam == PBT_APMSUSPEND || wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)) {
        state->fixture.power(state->fixture.token);
        return TRUE;
    }
    if (state && message == WM_WTSSESSION_CHANGE &&
        (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK || wParam == WTS_REMOTE_DISCONNECT)) {
        state->fixture.session(state->fixture.token, static_cast<std::uint32_t>(lParam));
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void nativeOwnedWindowChecks() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* name = L"XDStarterRecoverySafetyFixture";
    WNDCLASSW type{};
    type.lpfnWndProc = fixtureWindowProc;
    type.hInstance = instance;
    type.lpszClassName = name;
    const auto registered = RegisterClassW(&type);
    expect(registered != 0, "test-only native window class registered");
    if (!registered)
        return;
    NativeFixture state;
    HWND window = CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, &state);
    expect(window != nullptr, "a private message-only window was created");
    if (window) {
        expect(state.fixture.arm(), "native message fixture can arm");
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 43);
        expect(state.fixture.gate.active(state.fixture.token), "a message for another session is ignored");
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
        expect(!state.fixture.gate.active(state.fixture.token), "targeted suspend message pauses the model");
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        expect(!state.fixture.gate.active(state.fixture.token), "targeted resume never auto-arms the model");
        expect(state.fixture.arm(), "native fixture can explicitly rearm");
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, state.expectedSession);
        expect(!state.fixture.gate.active(state.fixture.token), "targeted lock message pauses the model");
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, state.expectedSession);
        expect(!state.fixture.gate.active(state.fixture.token), "targeted unlock never auto-arms the model");
        expect(DestroyWindow(window) != FALSE, "private message-only window destroyed");
    }
    expect(UnregisterClassW(name, instance) != FALSE, "test-only window class unregistered");
}
#endif
} // namespace

int runTests() {
    clockChecks();
    registrationAndSessions();
    gestureOrdering();
#ifdef Q_OS_WIN
    nativeOwnedWindowChecks();
#endif
    std::cout << "Recovery safety model: " << checks << " checks, " << failures << " failures\n";
    return failures;
}
} // namespace recovery_safety
