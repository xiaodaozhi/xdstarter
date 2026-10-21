// tests/platform/win_key_capture_tests.cpp

#include "platform/win_key_capture.h"
#include "platform/win_key_capture_state.h"

#include <QCoreApplication>

#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

namespace {

using xdstarter::WinKeyCaptureState;
using xdstarter::WinKeyDecision;
constexpr auto forward = WinKeyDecision::Forward;
constexpr auto replace = WinKeyDecision::ReplaceRelease;
constexpr auto leftWin = WinKeyCaptureState::leftWin;
constexpr auto rightWin = WinKeyCaptureState::rightWin;

class TestContext {
  public:
    void expect(bool condition, std::string_view description) {
        ++assertions;
        if (!condition) {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }
    int assertions = 0;
    int failures = 0;
};

WinKeyDecision key(WinKeyCaptureState& state, std::uint16_t code, bool down,
                   bool injected = false, bool own = false) {
    return state.process({code, down, injected, own});
}

class FakeInputDispatcher final : public xdstarter::WinKeyInputDispatcher {
  public:
    explicit FakeInputDispatcher(unsigned count) : count_(count) {}
    unsigned dispatchMarkAndRelease(std::uint16_t winKey) noexcept override {
        ++batches;
        releasedKey = winKey;
        return count_;
    }
    void releaseDummy() noexcept override { ++cleanups; }
    unsigned batches = 0;
    unsigned cleanups = 0;
    std::uint16_t releasedKey = 0;

  private:
    unsigned count_;
};

// A native-free source uses the production QObject session gates. It never
// installs an input hook or sends a key, including on Windows CTest runners.
class SessionFixture final : public xdstarter::WinKeyCapture {
  public:
    void setEnabled(bool enabled) override {
        generation = enabled ? beginEventSession() : endEventSession();
        active = enabled;
    }
    void stop() override {
        generation = endEventSession();
        active = false;
    }
    bool isActive() const override { return active; }
    bool isSupported() const override { return true; }
    void postToggle(quint64 sourceGeneration) { queueToggleRequested(sourceGeneration); }
    void postStatus(quint64 sourceGeneration, bool value) {
        queueStatusChanged(sourceGeneration, value, QStringLiteral("fixture"), {});
    }
    quint64 generation = 0;
    bool active = false;
};

void testStandaloneAndCombinations(TestContext& test) {
    WinKeyCaptureState state;
    for (auto win : {leftWin, rightWin}) {
        state.reset();
        test.expect(key(state, win, false) == forward, "unmatched Win-up is forwarded");
        test.expect(key(state, win, true) == forward, "original Win-down is forwarded");
        for (int repeat = 0; repeat < 5; ++repeat) {
            test.expect(key(state, win, true) == forward, "Win auto-repeat never toggles");
        }
        test.expect(key(state, win, false) == replace, "standalone Win releases once");
        test.expect(key(state, win, false) == forward, "duplicate Win-up cannot toggle twice");

        // Includes common shell shortcuts, letters, digits and all modifier sides.
        for (std::uint16_t other : {0x45, 0x52, 0x4C, 0x44, 0x58, 0x09, 0x31, 0x39,
                                   0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}) {
            state.reset();
            test.expect(key(state, win, true) == forward, "combination Win-down passes");
            test.expect(key(state, other, true) == forward, "combination key-down passes");
            test.expect(key(state, other, true) == forward, "combination repeats pass");
            test.expect(key(state, other, false) == forward, "combination key-up passes");
            test.expect(key(state, win, false) == forward, "combination never replaces Win-up");

            state.reset();
            (void)key(state, other, true);
            (void)key(state, win, true);
            (void)key(state, other, false);
            test.expect(key(state, win, false) == forward,
                        "key held before Win cancels even when released first");
        }
    }

    state.reset();
    (void)key(state, leftWin, true);
    (void)key(state, rightWin, true);
    test.expect(key(state, leftWin, false) == forward, "two overlapping Win keys cancel left");
    test.expect(key(state, rightWin, false) == forward, "two overlapping Win keys cancel right");
    (void)key(state, rightWin, true);
    test.expect(key(state, rightWin, false) == replace,
                "fresh gesture after both Win keys up works");

    state.reset();
    (void)key(state, leftWin, true);
    (void)key(state, 0x41, false);
    test.expect(key(state, leftWin, false) == forward, "unmatched non-Win release cancels safely");
}

void testSeedsAndCancellation(TestContext& test) {
    WinKeyCaptureState state;
    WinKeyCaptureState::HeldKeys held{};
    held[leftWin] = true;
    state.reset(held);
    test.expect(key(state, leftWin, true) == forward,
                "Win already held at install is no candidate");
    test.expect(key(state, leftWin, false) == forward, "held Win release keeps native behavior");
    (void)key(state, leftWin, true);
    test.expect(key(state, leftWin, false) == replace,
                "fresh Win works after initial held release");

    held.fill(false);
    held[0xA2] = true;
    state.reset(held);
    (void)key(state, rightWin, true);
    (void)key(state, 0xA2, false);
    test.expect(key(state, rightWin, false) == forward, "seeded Ctrl prevents standalone capture");

    state.reset();
    (void)key(state, leftWin, true);
    state.cancelCandidate(); // observer saw a mouse button or wheel
    test.expect(key(state, leftWin, false) == forward, "Win+mouse remains native");
    for (std::uint16_t button : {1, 2, 4, 5, 6}) {
        state.reset();
        (void)key(state, button, true);
        (void)key(state, leftWin, true);
        (void)key(state, button, false);
        test.expect(key(state, leftWin, false) == forward,
                    "mouse button held before Win prevents capture even when released first");
        (void)key(state, leftWin, true);
        test.expect(key(state, leftWin, false) == replace,
                    "all mouse buttons released allow a fresh standalone Win");
        held.fill(false);
        held[button] = true;
        state.reset(held);
        (void)key(state, rightWin, true);
        test.expect(key(state, rightWin, false) == forward,
                    "mouse button already held at install prevents capture");
        (void)key(state, button, false);
        (void)key(state, rightWin, true);
        test.expect(key(state, rightWin, false) == replace,
                    "seeded mouse-button release restores standalone gestures");
    }
    (void)key(state, leftWin, true);
    state.reset(); // stop/desktop loss/restart
    test.expect(key(state, leftWin, false) == forward, "reset invalidates unfinished candidate");

    state.reset();
    (void)key(state, rightWin, true);
    (void)key(state, 0xFFFF, true);
    test.expect(key(state, rightWin, false) == forward, "out-of-range input fails open");
}

void testInjectedInput(TestContext& test) {
    WinKeyCaptureState state;
    (void)key(state, leftWin, true, true);
    test.expect(key(state, leftWin, false, true) == forward, "foreign injected Win never captures");
    (void)key(state, leftWin, true);
    (void)key(state, 0xFF, true, true, true);
    (void)key(state, 0xFF, false, true, true);
    (void)key(state, leftWin, false, true, true);
    test.expect(key(state, leftWin, false) == replace,
                "own marked dispatch cannot recursively mutate physical state");

    state.reset();
    (void)key(state, rightWin, true);
    (void)key(state, 0x45, true, true);
    (void)key(state, 0x45, false, true);
    test.expect(key(state, rightWin, false) == forward,
                "foreign injected shortcut cancels current candidate");

    (void)key(state, 0xA2, true, true);
    (void)key(state, leftWin, true);
    test.expect(key(state, leftWin, false) == forward,
                "foreign injected held modifier blocks a later candidate");
    (void)key(state, 0xA2, false, true);
    (void)key(state, leftWin, true);
    test.expect(key(state, leftWin, false) == replace, "foreign release allows a fresh gesture");

    state.reset();
    (void)key(state, leftWin, true);
    (void)key(state, leftWin, false, true);
    (void)key(state, leftWin, true);
    test.expect(key(state, leftWin, false) == forward,
                "foreign Win-up cannot turn physical repeat into fresh down");
}

void testInputTransaction(TestContext& test) {
    for (unsigned inserted : {0U, 1U, 2U, 3U, 4U}) {
        FakeInputDispatcher input(inserted);
        const bool suppress = xdstarter::dispatchStandaloneWinRelease(input, rightWin);
        test.expect(suppress == (inserted == 3),
                    "only complete native input transaction may suppress physical up/toggle");
        test.expect(input.batches == 1 && input.releasedKey == rightWin,
                    "exactly one bounded batch releases the matching Win side");
        test.expect(input.cleanups == ((inserted == 1 || inserted == 2) ? 1U : 0U),
                    "partial injection gets bounded best-effort cancellation-key cleanup");
    }
}

void testSessionLifetime(TestContext& test) {
    int toggles = 0;
    int statuses = 0;
    bool statusActive = true;
    auto fixture = std::make_unique<SessionFixture>();
    QObject::connect(fixture.get(), &xdstarter::WinKeyCapture::toggleRequested,
                     [&] { ++toggles; });
    QObject::connect(fixture.get(), &xdstarter::WinKeyCapture::statusChanged,
                     [&](bool active, const QString&, const QString&) {
                         ++statuses;
                         statusActive = active;
                     });
    fixture->setEnabled(true);
    const auto first = fixture->generation;
    fixture->postToggle(first);
    fixture->postStatus(first, true);
    fixture->stop();
    QCoreApplication::processEvents();
    test.expect(toggles == 0 && statuses == 0, "stop drops queued toggle and active status");

    fixture->setEnabled(true);
    fixture->postToggle(fixture->generation);
    fixture->postStatus(fixture->generation, true);
    fixture->active = false; // native failure before its inactive status is queued
    QCoreApplication::processEvents();
    test.expect(toggles == 0 && statuses == 0,
                "native failure immediately invalidates queued toggle and active status");

    fixture->setEnabled(true);
    const auto second = fixture->generation;
    fixture->postToggle(first);
    fixture->postStatus(first, true);
    std::thread nativeCallback([&] { fixture->postToggle(second); });
    nativeCallback.join();
    fixture->postStatus(second, true);
    QCoreApplication::processEvents();
    test.expect(toggles == 1 && statuses == 1 && statusActive,
                "only current generation delivers from native thread to GUI");

    fixture->postToggle(second);
    fixture->setEnabled(false);
    fixture->postStatus(fixture->generation, false);
    QCoreApplication::processEvents();
    test.expect(toggles == 1 && statuses == 2 && !statusActive,
                "pause invalidates old input but delivers current inactive status");

    fixture->setEnabled(true);
    fixture->postToggle(fixture->generation);
    fixture->postStatus(fixture->generation, true);
    fixture.reset();
    QCoreApplication::processEvents();
    test.expect(toggles == 1 && statuses == 2,
                "QObject destruction discards queued native callbacks");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    TestContext test;
    testStandaloneAndCombinations(test);
    testSeedsAndCancellation(test);
    testInjectedInput(test);
    testInputTransaction(test);
    testSessionLifetime(test);

    auto capture = xdstarter::createWinKeyCapture();
    test.expect(!capture->isActive(), "factory construction installs no global Hook");
    capture->stop();
    test.expect(!capture->isActive(), "stopping never-started capture is harmless");
#ifndef Q_OS_WIN
    test.expect(!capture->isSupported(), "stub accurately reports unsupported platform");
    int unsupportedStatuses = 0;
    QObject::connect(capture.get(), &xdstarter::WinKeyCapture::statusChanged,
                     [&](bool active, const QString&, const QString& error) {
                         test.expect(!active && error == QStringLiteral("unsupported-platform"),
                                     "stub returns structured failure without installing input");
                         ++unsupportedStatuses;
                     });
    capture->setEnabled(true);
    QCoreApplication::processEvents();
    test.expect(unsupportedStatuses == 1, "stub unsupported request completes asynchronously");
#endif
    if (test.failures == 0) {
        std::cout << "PASS: " << test.assertions
                  << " native-free Win-key state, dispatch and lifecycle assertions.\n";
    }
    return test.failures ? 1 : 0;
}
