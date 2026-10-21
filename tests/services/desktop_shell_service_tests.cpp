#include "services/desktop_shell_service.h"

#include <QCoreApplication>
#include <QMutexLocker>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace {

using namespace xdstarter;

struct FakeDesktopState {
    QMutex mutex;
    DesktopActionResult actionResult{true, {}, {}};
    QStringList actionIds;
    std::atomic_int actionCalls{0};
    std::atomic_int anchorCalls{0};
    std::atomic_int guiThreadCalls{0};
    std::atomic_bool destroyed{false};
    std::atomic_bool destroyedOnWorker{false};
    bool blockActions = false;
    bool blockAnchors = false;
    QSemaphore actionEntered;
    QSemaphore actionReleased;
    QSemaphore anchorEntered;
    QSemaphore anchorReleased;
};

class FakeDesktopPlatform final : public DesktopPlatform {
  public:
    explicit FakeDesktopPlatform(std::shared_ptr<FakeDesktopState> state)
        : state_(std::move(state)) {}

    ~FakeDesktopPlatform() override {
        state_->destroyedOnWorker =
            QThread::currentThread() != QCoreApplication::instance()->thread();
        state_->destroyed = true;
    }

    DesktopActionResult openDesktopAction(const QString& actionId) override {
        recordThread();
        ++state_->actionCalls;
        {
            QMutexLocker lock(&state_->mutex);
            state_->actionIds.append(actionId);
        }
        state_->actionEntered.release();
        if (state_->blockActions && !state_->actionReleased.tryAcquire(1, 3000))
            return {false, QStringLiteral("fixture gate timed out"), QStringLiteral("timeout")};
        QMutexLocker lock(&state_->mutex);
        return state_->actionResult;
    }

    DesktopAnchorResult queryStartButtonAnchor() override {
        recordThread();
        const auto call = ++state_->anchorCalls;
        state_->anchorEntered.release();
        if (state_->blockAnchors && !state_->anchorReleased.tryAcquire(1, 3000))
            return {};
        return {true,
                QRect(call * 100, 1000, 48, 48),
                QRect(0, 0, 1920, 1080),
                QRect(0, 0, 1920, 1000),
                144,
                {},
                {}};
    }

  private:
    void recordThread() {
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            ++state_->guiThreadCalls;
    }

    std::shared_ptr<FakeDesktopState> state_;
};

std::unique_ptr<DesktopPlatform> fakePlatform(const std::shared_ptr<FakeDesktopState>& state) {
    return std::make_unique<FakeDesktopPlatform>(state);
}

class DesktopShellServiceTests final : public QObject {
    Q_OBJECT

  private slots:
    void rejectsUnknownActionsWithoutCallingPlatform();
    void actionsReportCompletionAndDoNotQueueDuplicateClicks();
    void anchorQueriesCoalesceAndDiscardPreviousReveal();
    void invalidatedQueriesCannotRepositionAClosedMenu();
    void destructorJoinsAnInFlightOperation();
};

void DesktopShellServiceTests::rejectsUnknownActionsWithoutCallingPlatform() {
    auto state = std::make_shared<FakeDesktopState>();
    DesktopShellService service(fakePlatform(state));
    QSignalSpy failed(&service, &DesktopShellService::desktopActionFailed);
    for (const auto& id : {QString{}, QStringLiteral("Documents"), QStringLiteral("cmd.exe"),
                           QStringLiteral("file:///C:/"), QStringLiteral("settings ")}) {
        QVERIFY(!service.requestDesktopAction(id));
        QVERIFY(!service.busy());
    }
    QCOMPARE(failed.count(), 5);
    QCOMPARE(state->actionCalls.load(), 0);
    QVERIFY(!service.status().isEmpty());

    DesktopShellService unavailable(nullptr);
    QSignalSpy unavailableFailed(&unavailable, &DesktopShellService::desktopActionFailed);
    QVERIFY(!unavailable.requestDesktopAction(QStringLiteral("documents")));
    QCOMPARE(unavailableFailed.count(), 1);
    QVERIFY(!unavailable.busy());
    QSignalSpy anchor(&unavailable, &DesktopShellService::startButtonAnchorReady);
    const auto request = unavailable.requestStartButtonAnchor();
    QCOMPARE(anchor.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(anchor.count(), 1, 3000);
    QCOMPARE(anchor.first().at(0).toULongLong(), request);
    const auto result = qvariant_cast<DesktopAnchorResult>(anchor.first().at(1));
    QVERIFY(!result.trusted);
    QVERIFY(!result.errorCode.isEmpty());
}

void DesktopShellServiceTests::actionsReportCompletionAndDoNotQueueDuplicateClicks() {
    auto state = std::make_shared<FakeDesktopState>();
    state->blockActions = true;
    state->actionResult = {false, QStringLiteral("fixture denied"), QStringLiteral("denied")};
    DesktopShellService service(fakePlatform(state));
    QSignalSpy succeeded(&service, &DesktopShellService::desktopActionSucceeded);
    QSignalSpy failed(&service, &DesktopShellService::desktopActionFailed);
    QVERIFY(service.requestDesktopAction(QStringLiteral("documents")));
    QVERIFY(service.busy());
    QVERIFY(state->actionEntered.tryAcquire(1, 3000));
    QVERIFY(!service.requestDesktopAction(QStringLiteral("downloads")));
    QVERIFY(service.busy());
    QCOMPARE(failed.count(), 1);
    state->actionReleased.release();
    QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
    QCOMPARE(failed.count(), 2);
    QCOMPARE(failed.last().at(0).toString(), QStringLiteral("documents"));
    QCOMPARE(failed.last().at(1).toString(), QStringLiteral("fixture denied"));
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(state->actionCalls.load(), 1);
    {
        QMutexLocker lock(&state->mutex);
        state->actionResult = {true, {}, {}};
    }
    for (const auto& id :
         {QStringLiteral("downloads"), QStringLiteral("settings"), QStringLiteral("account")}) {
        QVERIFY(service.requestDesktopAction(id));
        state->actionReleased.release();
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
    }
    QCOMPARE(succeeded.count(), 3);
    QCOMPARE(succeeded.last().at(0).toString(), QStringLiteral("account"));
    QCOMPARE(state->actionCalls.load(), 4);
    QCOMPARE(state->guiThreadCalls.load(), 0);
    QVERIFY(!service.status().isEmpty());
}

void DesktopShellServiceTests::anchorQueriesCoalesceAndDiscardPreviousReveal() {
    auto state = std::make_shared<FakeDesktopState>();
    state->blockAnchors = true;
    DesktopShellService service(fakePlatform(state));
    QSignalSpy anchor(&service, &DesktopShellService::startButtonAnchorReady);
    const auto first = service.requestStartButtonAnchor();
    QVERIFY(state->anchorEntered.tryAcquire(1, 3000));
    const auto second = service.requestStartButtonAnchor();
    const auto latest = service.requestStartButtonAnchor();
    QVERIFY(latest > second && second > first);
    state->anchorReleased.release();
    QTRY_COMPARE_WITH_TIMEOUT(state->anchorCalls.load(), 2, 3000);
    QCOMPARE(anchor.count(), 0);
    state->anchorReleased.release();
    QTRY_COMPARE_WITH_TIMEOUT(anchor.count(), 1, 3000);
    QCOMPARE(anchor.first().at(0).toULongLong(), latest);
    const auto result = qvariant_cast<DesktopAnchorResult>(anchor.first().at(1));
    QVERIFY(result.trusted);
    QCOMPARE(result.startButtonPhysicalRect.left(), 200);
    QCOMPARE(result.dpi, 144U);
    QCOMPARE(state->anchorCalls.load(), 2);
    QCOMPARE(state->guiThreadCalls.load(), 0);
}

void DesktopShellServiceTests::invalidatedQueriesCannotRepositionAClosedMenu() {
    auto state = std::make_shared<FakeDesktopState>();
    state->blockAnchors = true;
    DesktopShellService service(fakePlatform(state));
    QSignalSpy anchor(&service, &DesktopShellService::startButtonAnchorReady);
    const auto old = service.requestStartButtonAnchor();
    QVERIFY(state->anchorEntered.tryAcquire(1, 3000));
    service.invalidateStartButtonAnchor();
    const auto current = service.requestStartButtonAnchor();
    QVERIFY(current > old);
    state->anchorReleased.release();
    QTRY_COMPARE_WITH_TIMEOUT(state->anchorCalls.load(), 2, 3000);
    QCOMPARE(anchor.count(), 0);
    // Closing the second reveal also invalidates its already-running query.
    service.invalidateStartButtonAnchor();
    state->anchorReleased.release();
    // A later action completes on the same serial worker, establishing that
    // the canceled query has finished without relying on a timing delay.
    QSignalSpy action(&service, &DesktopShellService::desktopActionSucceeded);
    QVERIFY(service.requestDesktopAction(QStringLiteral("documents")));
    QTRY_COMPARE_WITH_TIMEOUT(action.count(), 1, 3000);
    QCOMPARE(anchor.count(), 0);
    QCOMPARE(state->guiThreadCalls.load(), 0);
}

void DesktopShellServiceTests::destructorJoinsAnInFlightOperation() {
    auto state = std::make_shared<FakeDesktopState>();
    state->blockActions = true;
    auto service = std::make_unique<DesktopShellService>(fakePlatform(state));
    QVERIFY(service->requestDesktopAction(QStringLiteral("documents")));
    QVERIFY(state->actionEntered.tryAcquire(1, 3000));
    std::thread release([state] {
        QThread::msleep(30);
        state->actionReleased.release();
    });
    service.reset();
    release.join();
    QVERIFY(state->destroyed.load());
    QVERIFY(state->destroyedOnWorker.load());
    QCOMPARE(state->guiThreadCalls.load(), 0);
    QCoreApplication::processEvents();
}

} // namespace

QTEST_GUILESS_MAIN(DesktopShellServiceTests)

#include "desktop_shell_service_tests.moc"
