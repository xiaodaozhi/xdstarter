#include "platform/windows/windows_graphics_failure_notice.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

#include <windows.h>

#include <cstdlib>
#include <iostream>

namespace {

QStringList calls;
bool watchdogResult = true;
bool sessionResult = true;
bool fallbackResult = true;
bool stuckSession = false;
bool stuckFallback = false;

bool watchdog() {
    calls.append(QStringLiteral("watchdog"));
    return watchdogResult;
}
bool testWatchdog() { return xdstarter::startGraphicsFailureNoticeWatchdog(300); }
bool session() {
    calls.append(QStringLiteral("session"));
    if (stuckSession)
        Sleep(5000);
    return sessionResult;
}
bool fallback() {
    calls.append(QStringLiteral("fallback"));
    if (stuckFallback)
        Sleep(5000);
    return fallbackResult;
}

bool check(bool expected, const char* detail) {
    if (!expected)
        std::cerr << detail << '\n';
    return expected;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.contains(QStringLiteral("--stuck-session")) ||
        args.contains(QStringLiteral("--stuck-fallback"))) {
        stuckSession = args.contains(QStringLiteral("--stuck-session"));
        stuckFallback = !stuckSession;
        sessionResult = false;
        return xdstarter::runGraphicsFailureNoticeWith({&testWatchdog, &session, &fallback});
    }

    const xdstarter::GraphicsFailureNoticeApi api{&watchdog, &session, &fallback};
    watchdogResult = false;
    if (!check(xdstarter::runGraphicsFailureNoticeWith(api) == EXIT_FAILURE &&
                   calls == QStringList{QStringLiteral("watchdog")},
               "watchdog failure must stop before displaying a notice"))
        return EXIT_FAILURE;
    calls.clear();
    watchdogResult = true;
    if (!check(xdstarter::runGraphicsFailureNoticeWith(api) == EXIT_SUCCESS &&
                   calls == QStringList{QStringLiteral("watchdog"), QStringLiteral("session")},
               "session success must not show a fallback"))
        return EXIT_FAILURE;
    calls.clear();
    sessionResult = false;
    if (!check(xdstarter::runGraphicsFailureNoticeWith(api) == EXIT_SUCCESS &&
                   calls == QStringList{QStringLiteral("watchdog"), QStringLiteral("session"),
                                        QStringLiteral("fallback")},
               "session failure must show the fallback"))
        return EXIT_FAILURE;
    calls.clear();
    fallbackResult = false;
    if (!check(xdstarter::runGraphicsFailureNoticeWith(api) == EXIT_FAILURE &&
                   calls == QStringList{QStringLiteral("watchdog"), QStringLiteral("session"),
                                        QStringLiteral("fallback")},
               "failure of both notice paths must return failure"))
        return EXIT_FAILURE;

    for (const auto& mode : {QStringLiteral("--stuck-session"), QStringLiteral("--stuck-fallback")}) {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {mode});
        if (!check(child.waitForStarted(3000) && child.waitForFinished(3000) && child.exitCode() != 0,
                   "a stuck helper must be stopped by its watchdog")) {
            child.kill();
            child.waitForFinished(3000);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
