// tests/services/session_instance_tests.cpp

#include "services/session_instance.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

class SessionInstanceTest final : public QObject {
    Q_OBJECT
  private slots:
    void secondaryActivatesWithoutBecomingOwner() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        xdstarter::SessionInstance first(QStringLiteral("session-one"), directory.path());
        QCOMPARE(first.acquire(), xdstarter::SessionInstance::Result::Primary);
        QSignalSpy activated(&first, &xdstarter::SessionInstance::activationRequested);
        for (int attempt = 0; attempt < 3; ++attempt) {
            QProcess second;
            second.start(
                QCoreApplication::applicationFilePath(),
                {QStringLiteral("--activate"), QStringLiteral("session-one"), directory.path()});
            QVERIFY(second.waitForStarted());
            QTRY_COMPARE_WITH_TIMEOUT(second.state(), QProcess::NotRunning, 5000);
            QCOMPARE(second.exitStatus(), QProcess::NormalExit);
            QCOMPARE(second.exitCode(), 0);
            QCOMPARE(activated.count(), attempt + 1);
        }
    }
    void sessionsAreIsolatedAndOwnerReleaseIsRecoverable() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        {
            xdstarter::SessionInstance one(QStringLiteral("one"), directory.path());
            xdstarter::SessionInstance two(QStringLiteral("two"), directory.path());
            QCOMPARE(one.acquire(), xdstarter::SessionInstance::Result::Primary);
            QCOMPARE(two.acquire(), xdstarter::SessionInstance::Result::Primary);
        }
        xdstarter::SessionInstance replacement(QStringLiteral("one"), directory.path());
        QCOMPARE(replacement.acquire(), xdstarter::SessionInstance::Result::Primary);
    }
    void concurrentSecondariesAndCrashedOwner() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        {
            xdstarter::SessionInstance first(QStringLiteral("parallel"), directory.path());
            QCOMPARE(first.acquire(), xdstarter::SessionInstance::Result::Primary);
            QCOMPARE(first.acquire(), xdstarter::SessionInstance::Result::Primary);
            QSignalSpy activated(&first, &xdstarter::SessionInstance::activationRequested);
            std::vector<std::unique_ptr<QProcess>> processes;
            for (int index = 0; index < 4; ++index) {
                auto process = std::make_unique<QProcess>();
                process->start(
                    QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--activate"), QStringLiteral("parallel"), directory.path()});
                QVERIFY(process->waitForStarted());
                processes.push_back(std::move(process));
            }
            QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 4, 5000);
            for (const auto& process : processes) {
                QTRY_COMPARE(process->state(), QProcess::NotRunning);
                QCOMPARE(process->exitCode(), 0);
            }
        }
        QProcess owner;
        owner.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--owner"), QStringLiteral("crash"), directory.path()});
        QVERIFY(owner.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(directory.filePath(QStringLiteral("ready"))),
                                 5000);
        owner.kill(); // Only the explicitly created test child is terminated.
        QVERIFY(owner.waitForFinished());
        xdstarter::SessionInstance recovered(QStringLiteral("crash"), directory.path());
        QCOMPARE(recovered.acquire(), xdstarter::SessionInstance::Result::Primary);
    }
    void invalidDirectoryFailsWithoutStarting() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile blocker(directory.filePath(QStringLiteral("file")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        xdstarter::SessionInstance instance(QStringLiteral("blocked"),
                                            blocker.fileName() + QStringLiteral("/child"));
        QCOMPARE(instance.acquire(), xdstarter::SessionInstance::Result::Failed);
        QVERIFY(!instance.errorString().isEmpty());
    }
};

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() == 4 && args[1] == QLatin1String("--owner")) {
        xdstarter::SessionInstance instance(args[2], args[3]);
        if (instance.acquire() != xdstarter::SessionInstance::Result::Primary)
            return 1;
        QFile ready(QDir(args[3]).filePath(QStringLiteral("ready")));
        if (!ready.open(QIODevice::WriteOnly) || ready.write("ready") != 5)
            return 2;
        ready.close();
        return application.exec();
    }
    if (args.size() == 4 && args[1] == QLatin1String("--activate")) {
        xdstarter::SessionInstance instance(args[2], args[3]);
        return instance.acquire() == xdstarter::SessionInstance::Result::ActivatedExisting ? 0 : 1;
    }
    SessionInstanceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "session_instance_tests.moc"
