#include "platform/windows/windows_motion_preference.h"

#include <QGuiApplication>
#include <QEventLoop>
#include <QWindow>

#include <windows.h>

#include <cstdio>
#include <optional>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    int failures = 0;
    const auto expect = [&failures](bool valid, const char* message) {
        if (!valid) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    };
    std::optional<bool> state = false;
    int reads = 0;
    int changes = 0;
    xdstarter::WindowsMotionPreference preference(nullptr, [&] {
        ++reads;
        return state;
    });
    QObject::connect(&preference, &xdstarter::MotionPreference::animationsEnabledChanged,
                     &application, [&changes] { ++changes; });
    expect(!preference.animationsEnabled() && reads == 1, "initial preference is read once");
    MSG message{};
    message.message = WM_SETTINGCHANGE;
    message.lParam = 1; // Never dereferenced by the event filter.
    qintptr result = 0;
    state = true;
    expect(!preference.nativeEventFilter("other", &message, &result), "unrelated event is passed on");
    expect(reads == 1, "unrelated event does not read settings");
    expect(!preference.nativeEventFilter("windows_generic_MSG", &message, &result),
           "setting notification is not consumed");
    preference.nativeEventFilter("windows_generic_MSG", &message, &result);
    expect(reads == 1 && !preference.animationsEnabled(), "native callback only queues refresh");
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    expect(reads == 2 && changes == 1 && preference.animationsEnabled(),
           "duplicate notifications coalesce and publish on the event loop");
    state.reset();
    preference.nativeEventFilter("windows_dispatcher_MSG", &message, &result);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    expect(reads == 3 && changes == 2 && !preference.animationsEnabled(),
           "read failure falls back to reduced motion");
    preference.nativeEventFilter("windows_generic_MSG", &message, &result);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    expect(reads == 4 && changes == 2, "unchanged value does not emit another signal");
    QWindow window;
    window.create(); // Hidden, process-owned window; no system setting is changed.
    expect(window.winId() != 0, "test window exists");
    state = true;
    const int readsBeforeNativeMessage = reads;
    SendMessageW(reinterpret_cast<HWND>(window.winId()), WM_SETTINGCHANGE, 0, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    expect(reads > readsBeforeNativeMessage && preference.animationsEnabled(),
           "Qt receives a targeted native setting notification on its own window");
    state = false;
    preference.refresh();
    expect(!preference.animationsEnabled(), "explicit refresh detects changes missed while hidden");
    {
        xdstarter::WindowsMotionPreference transient(nullptr, [] { return std::optional<bool>(true); });
        transient.nativeEventFilter("windows_generic_MSG", &message, &result);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents); // No stale callback after destruction.
    if (!failures)
        std::puts("Motion preference tests passed.");
    return failures ? 1 : 0;
}
