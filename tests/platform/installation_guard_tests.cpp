#include "platform/installation_guard.h"

#include <QCoreApplication>
#include <QProcess>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using xdstarter::InstallationMode;
    if (argc == 2) {
        auto guard = xdstarter::createInstallationGuard(QString::fromLocal8Bit(argv[1]) ==
                                                                QLatin1String("install")
                                                            ? InstallationMode::Install
                                                            : InstallationMode::Runtime);
        if (!guard->acquire())
            return 2;
        std::cout << "ready\n" << std::flush;
        char command;
        std::cin.get(command);
        return 0;
    }
    int failures = 0;
    const auto expect = [&](bool result, const char* description) {
        if (!result) {
            std::cerr << description << '\n';
            ++failures;
        }
    };
    const auto child = [&](QProcess& process, const QString& mode) {
        process.start(app.applicationFilePath(), {mode});
        return process.waitForStarted(3000) && process.waitForReadyRead(3000) &&
               process.readLine().trimmed() == "ready";
    };
    const auto stop = [&](QProcess& process) {
        process.write("\n");
        process.closeWriteChannel();
        expect(process.waitForFinished(3000) && process.exitCode() == 0,
               "guard owner exits normally");
    };
    QProcess installer;
    expect(child(installer, QStringLiteral("install")), "isolated installer acquires gate");
    expect(!xdstarter::createInstallationGuard()->acquire(), "installation blocks new startup");
    stop(installer);
    QProcess runtime;
    expect(child(runtime, QStringLiteral("runtime")), "runtime registers lifetime");
    expect(!xdstarter::createInstallationGuard(InstallationMode::Install)->acquire(),
           "resident blocks install and uninstall");
    QProcess other;
    expect(child(other, QStringLiteral("runtime")), "other sessions can share runtime presence");
    stop(runtime);
    expect(!xdstarter::createInstallationGuard(InstallationMode::Install)->acquire(),
           "last runtime owner must exit");
    stop(other);
    expect(xdstarter::createInstallationGuard(InstallationMode::Install)->acquire(),
           "normal exits release gate and runtime presence");
    return failures ? 1 : 0;
}
