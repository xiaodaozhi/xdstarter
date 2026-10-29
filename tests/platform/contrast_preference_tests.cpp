#include "platform/windows/windows_contrast_preference.h"

#include <QColor>
#include <QEventLoop>
#include <QGuiApplication>
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
    const QVariantMap first{{QStringLiteral("window"), QColor(Qt::black)},
                            {QStringLiteral("windowText"), QColor(Qt::yellow)},
                            {QStringLiteral("highlight"), QColor(Qt::blue)},
                            {QStringLiteral("highlightText"), QColor(Qt::white)},
                            {QStringLiteral("grayText"), QColor(Qt::gray)}};
    auto second = first;
    second[QStringLiteral("window")] = QColor(Qt::white);
    second[QStringLiteral("windowText")] = QColor(Qt::black);
    std::optional<xdstarter::ContrastState> state = xdstarter::ContrastState{};
    int reads = 0;
    int changes = 0;
    xdstarter::WindowsContrastPreference preference(nullptr, [&] {
        ++reads;
        return state;
    });
    QObject::connect(&preference, &xdstarter::ContrastPreference::changed, &application,
                     [&changes] { ++changes; });
    expect(reads == 1 && !preference.enabled() && preference.colors().isEmpty(),
           "initial inactive preference is read once");
    MSG message{};
    qintptr result = 0;
    message.message = WM_SYSCOLORCHANGE;
    state = xdstarter::ContrastState{true, first};
    expect(!preference.nativeEventFilter("other", &message, &result), "unrelated type is passed on");
    expect(reads == 1, "unrelated type does not read settings");
    for (const auto code : {WM_SYSCOLORCHANGE, WM_THEMECHANGED, WM_SETTINGCHANGE}) {
        message.message = code;
        expect(!preference.nativeEventFilter("windows_generic_MSG", &message, &result),
               "native notification is not consumed");
    }
    expect(reads == 1 && !preference.enabled(), "native callbacks only queue one refresh");
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    expect(reads == 2 && changes == 1 && preference.enabled() && preference.colors() == first,
           "notifications coalesce and publish the supplied system colors");
    state.reset();
    preference.refresh();
    expect(preference.enabled() && preference.colors() == first && changes == 1,
           "a read failure retains the last known high contrast state");
    auto invalid = first;
    invalid.remove(QStringLiteral("windowText"));
    state = xdstarter::ContrastState{true, invalid};
    preference.refresh();
    expect(preference.enabled() && preference.colors() == first && changes == 1,
           "an incomplete palette cannot replace the last known state");
    QWindow window;
    window.create(); // Target only a hidden window owned by this process.
    expect(window.winId() != 0, "test window exists");
    state = xdstarter::ContrastState{true, second};
    const int readsBefore = reads;
    SendMessageW(reinterpret_cast<HWND>(window.winId()), WM_SYSCOLORCHANGE, 0, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    expect(reads > readsBefore && preference.colors() == second && changes == 2,
           "targeted native color notification refreshes changed colors");
    state = xdstarter::ContrastState{};
    preference.refresh();
    expect(!preference.enabled() && preference.colors() == second && changes == 3,
           "explicit refresh restores normal mode");
    HIGHCONTRASTW actual{};
    actual.cbSize = sizeof(actual);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(actual), &actual, 0)) {
        xdstarter::WindowsContrastPreference systemPreference;
        HIGHCONTRASTW after{};
        after.cbSize = sizeof(after);
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(after), &after, 0) &&
            (actual.dwFlags & HCF_HIGHCONTRASTON) == (after.dwFlags & HCF_HIGHCONTRASTON)) {
            expect(systemPreference.enabled() == bool(actual.dwFlags & HCF_HIGHCONTRASTON),
                   "default reader agrees with the unchanged native preference");
            if (systemPreference.enabled()) {
                const auto color = GetSysColor(COLOR_WINDOWTEXT);
                expect(systemPreference.colors().value(QStringLiteral("windowText")).value<QColor>() ==
                           QColor(GetRValue(color), GetGValue(color), GetBValue(color)),
                       "default reader obtains the native window text color");
            }
        } else {
            std::puts("System preference changed during the read-only comparison; skipped.");
        }
    } else {
        std::puts("System high contrast query unavailable; read-only comparison skipped.");
    }
    {
        xdstarter::WindowsContrastPreference transient(nullptr, [] {
            return std::optional<xdstarter::ContrastState>(xdstarter::ContrastState{});
        });
        message.message = WM_THEMECHANGED;
        transient.nativeEventFilter("windows_generic_MSG", &message, &result);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    if (!failures)
        std::puts("High contrast preference tests passed.");
    return failures ? 1 : 0;
}
