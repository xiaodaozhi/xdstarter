#include "app/desktop_window_host.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/win_key_capture.h"
#include "services/desktop_shell_service.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRasterWindow>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QWindow>
#include <QtTest>

#include <atomic>
#include <memory>

namespace {

struct AnchorState {
    std::atomic_bool delay{false};
    std::atomic_bool workerOnly{true};
    std::atomic_int queries{0};
    std::atomic_int completed{0};
    bool alternateTrust = false;
    QSemaphore release;
};

class AnchorPlatform final : public xdstarter::DesktopPlatform {
  public:
    explicit AnchorPlatform(std::shared_ptr<AnchorState> state) : state_(std::move(state)) {}

    xdstarter::DesktopActionResult openDesktopAction(const QString&) override {
        return {false, QStringLiteral("No real desktop action in host tests"), {}};
    }

    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            state_->workerOnly = false;
        }
        ++state_->queries;
        if (state_->delay) {
            // Bounded even if a test assertion aborts before releasing the worker.
            state_->release.tryAcquire(1, 2000);
        }
        xdstarter::DesktopAnchorResult anchor;
        anchor.trusted = !state_->alternateTrust || state_->queries.load() % 2 == 1;
        anchor.startButtonPhysicalRect = QRect(24, 1008, 72, 72);
        anchor.monitorPhysicalRect = QRect(0, 0, 1920, 1080);
        anchor.workAreaPhysicalRect = QRect(0, 0, 1920, 1008);
        anchor.dpi = 144;
        ++state_->completed;
        return anchor;
    }

  private:
    std::shared_ptr<AnchorState> state_;
};

class PlacementPlatform final : public xdstarter::DesktopPlatform {
  public:
    xdstarter::DesktopActionResult openDesktopAction(const QString&) override { return {}; }
    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override { return {}; }

    xdstarter::DesktopPlacementResult placeWindow(
        quintptr nativeHandle, const xdstarter::DesktopAnchorResult& anchor,
        int gapPhysicalPixels, bool activate, int shadowInsetPhysicalPixels) override {
        ++placements;
        handle = nativeHandle;
        lastAnchor = anchor;
        gap = gapPhysicalPixels;
        activationRequested = activate;
        inset = shadowInsetPhysicalPixels;
        if (observedWindow) {
            placedWhileVisible |= observedWindow->isVisible();
            const qreal scale = observedWindow->devicePixelRatio();
            const auto panel = xdstarter::desktopPopupPhysicalRect(
                QSize(qRound(observedWindow->width() * scale) - 2 * inset,
                      qRound(observedWindow->height() * scale) - 2 * inset),
                anchor, gapPhysicalPixels);
            panels.append(panel);
        }
        guiThreadOnly = guiThreadOnly &&
                        QThread::currentThread() == QCoreApplication::instance()->thread();
        return {true, QRect(24, 200, 640, 480), {}, {}};
    }

    int placements = 0;
    quintptr handle = 0;
    int gap = 0;
    int inset = 0;
    QWindow *observedWindow = nullptr;
    bool placedWhileVisible = false;
    QList<QRect> panels;
    bool activationRequested = false;
    bool guiThreadOnly = true;
    xdstarter::DesktopAnchorResult lastAnchor;
};

class FakeTray final : public xdstarter::DesktopTray {
  public:
    xdstarter::DesktopActionResult start(const QImage& icon) override {
        ++starts;
        receivedIcon = !icon.isNull();
        available = registrationSucceeds;
        return {available, QStringLiteral("Fake tray registration"), {}};
    }
    void stop() override {
        ++stops;
        available = false;
    }
    bool isAvailable() const override { return available; }
    void showContextMenu() override { ++menus; }
    void setWinKeyCaptureState(bool active, bool canEnable, const QString& status) override {
        captureActive = active;
        captureCanEnable = canEnable;
        captureStatus = status;
        if (failNextStateUpdate && available) {
            failNextStateUpdate = false;
            setRegistration(false);
        }
    }
    void toggle() { emit toggleRequested(); }
    void requestMenu() { emit menuRequested(QPoint(10, 20)); }
    void requestQuit() { emit quitRequested(); }
    void requestWinKeyToggle() { emit winKeyCaptureToggleRequested(); }
    void setRegistration(bool registered) {
        available = registered;
        emit registrationChanged(registered, QStringLiteral("Fake registration change"));
    }

    bool registrationSucceeds = true;
    bool available = false;
    bool receivedIcon = false;
    int starts = 0;
    int stops = 0;
    int menus = 0;
    bool captureActive = false;
    bool captureCanEnable = false;
    bool failNextStateUpdate = false;
    QString captureStatus;
};

class FakeWinKeyCapture final : public xdstarter::WinKeyCapture {
  public:
    void setEnabled(bool enabled) override {
        ++requests;
        if (!enabled) {
            stop();
            return;
        }
        active = !fails;
        emit statusChanged(active, active ? QStringLiteral("Win 键唤起已启用。")
                                          : QStringLiteral("测试安装失败，保留原生输入。"),
                           fails ? QStringLiteral("fixture-failed") : QString{});
    }
    void stop() override {
        ++stops;
        active = false;
        emit statusChanged(false, QStringLiteral("Win 键唤起已停止。"), {});
    }
    bool isActive() const override { return active; }
    bool isSupported() const override { return supported; }
    void toggle() { emit toggleRequested(); }

    bool active = false;
    bool fails = false;
    bool supported = true;
    int requests = 0;
    int stops = 0;
};

QImage trayIcon() {
    QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::blue);
    return image;
}

} // namespace

class MenuWindow final : public QWindow {
    Q_OBJECT
    Q_PROPERTY(bool dismissalBlocked READ blocked NOTIFY protectionChanged)
    Q_PROPERTY(bool dragActive READ dragging NOTIFY protectionChanged)
    Q_PROPERTY(bool dismissalManaged MEMBER managed)
    Q_PROPERTY(qreal shadowInset MEMBER shadowInsetDip)
  public:
    bool blocked() const { return drag || popup; }
    bool dragging() const { return drag; }
    void setDrag(bool value) { drag = value; emit protectionChanged(); }
    void setPopup(bool value) { popup = value; emit protectionChanged(); }
    Q_INVOKABLE void prepareForDismissal() {
        ++dismissals;
        drag = false;
        popup = false;
        emit protectionChanged();
    }
    Q_INVOKABLE void prepareForPresentation() { ++presentations; }
    void requestDismiss() { emit dismissRequested(); }
    bool managed = false;
    bool editMode = true;
    int dismissals = 0;
    int presentations = 0;
    bool drag = false;
    bool popup = false;
    qreal shadowInsetDip = 24;
  signals:
    void protectionChanged();
    void dismissRequested();
};

class DesktopWindowHostTest : public QObject {
    Q_OBJECT

  private slots:
    void init();
    void cleanup();
    void trayRestoresClosedWindow();
    void hiddenWindowRejectsDelayedAnchor();
    void failedTrayKeepsTaskbarRecovery();
    void lostTrayRecoversHiddenWindow();
    void shutdownIgnoresPendingWork();
    void restoresApplicationPolicy_data();
    void restoresApplicationPolicy();
    void externalFocusDismissesAndRestores();
    void transientFocusAndQuickReturnStayOpen();
    void externalFocusWaitsForDragRelease();
    void externalDismissalWithoutTrayMinimizes();
    void delayedLaunchRecoveryDoesNotMinimizeReopenedWindow();
    void repeatedRevealPlacesHiddenAtStableMargins();
    void pendingRevealCanBeCanceled();
    void visibleReactivationDoesNotMoveAgain();
    void winKeyCaptureRequiresOptInAndStops();
    void winKeyCaptureFailureAndTrayLoss();
    void unsupportedWinKeyCaptureStaysDisabled();
    void trayFailureDuringEnableDoesNotInstallCapture();

  private:
    bool originalQuitOnClose_ = false;
};

void DesktopWindowHostTest::init() {
    originalQuitOnClose_ = QGuiApplication::quitOnLastWindowClosed();
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

void DesktopWindowHostTest::cleanup() {
    QGuiApplication::setQuitOnLastWindowClosed(originalQuitOnClose_);
}

void DesktopWindowHostTest::winKeyCaptureRequiresOptInAndStops() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    FakeWinKeyCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachWinKeyCapture(capture);
    host.start(trayIcon());
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(capture.requests, 0);
    QVERIFY(!capture.isActive());
    QVERIFY(tray.captureCanEnable);
    capture.toggle();
    QVERIFY(window.isVisible());

    tray.requestWinKeyToggle();
    QCOMPARE(capture.requests, 1);
    QVERIFY(capture.isActive());
    QVERIFY(window.property("winKeyCaptureActive").toBool());
    QVERIFY(tray.captureActive);
    capture.toggle();
    QVERIFY(!window.isVisible());
    capture.toggle();
    QTRY_VERIFY(window.isVisible());
    host.toggleWinKeyCapture();
    QVERIFY(!capture.isActive());
    QVERIFY(!tray.captureActive);
    capture.toggle();
    QVERIFY(window.isVisible());
    host.shutdown();
    QVERIFY(!tray.captureCanEnable);
    const auto requests = capture.requests;
    tray.requestWinKeyToggle();
    QCOMPARE(capture.requests, requests);
}

void DesktopWindowHostTest::winKeyCaptureFailureAndTrayLoss() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    FakeWinKeyCapture capture;
    capture.fails = true;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachWinKeyCapture(capture);
    host.start(trayIcon());
    QTRY_VERIFY(window.isVisible());
    host.toggleWinKeyCapture();
    QVERIFY(!capture.isActive());
    QVERIFY(window.property("integrationStatus").toString().contains(QStringLiteral("安装失败")));
    QVERIFY(!window.property("winKeyCaptureBusy").toBool());
    capture.fails = false;
    host.toggleWinKeyCapture();
    QVERIFY(capture.isActive());
    window.hide();
    tray.setRegistration(false);
    QVERIFY(!capture.isActive());
    QVERIFY(!tray.captureCanEnable);
    QTRY_COMPARE(window.visibility(), QWindow::Minimized);
    const auto requests = capture.requests;
    tray.setRegistration(true);
    QCOMPARE(capture.requests, requests);
    QVERIFY(!capture.isActive());
    QVERIFY(tray.captureCanEnable);
    host.toggleWinKeyCapture();
    QVERIFY(capture.isActive());
    host.shutdown();
    QVERIFY(!capture.isActive());
    capture.toggle();
    QVERIFY(!tray.captureActive);
}

void DesktopWindowHostTest::unsupportedWinKeyCaptureStaysDisabled() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    FakeWinKeyCapture capture;
    capture.supported = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachWinKeyCapture(capture);
    host.start(trayIcon());
    QTRY_VERIFY(window.isVisible());
    QVERIFY(!tray.captureCanEnable);
    tray.requestWinKeyToggle();
    QCOMPARE(capture.requests, 0);
    QVERIFY(!capture.isActive());
}

void DesktopWindowHostTest::trayFailureDuringEnableDoesNotInstallCapture() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    FakeWinKeyCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachWinKeyCapture(capture);
    host.start(trayIcon());
    QTRY_VERIFY(window.isVisible());
    tray.failNextStateUpdate = true;
    host.toggleWinKeyCapture();
    QCOMPARE(capture.requests, 0);
    QVERIFY(!capture.isActive());
    QVERIFY(!window.property("winKeyCaptureAvailable").toBool());
    QVERIFY(!window.property("winKeyCaptureBusy").toBool());
    QVERIFY(!tray.captureActive);
    QVERIFY(!tray.captureCanEnable);
}

void DesktopWindowHostTest::trayRestoresClosedWindow() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);

    host.start(trayIcon());
    QCOMPARE(tray.starts, 1);
    QVERIFY(tray.receivedIcon);
    QVERIFY(!QGuiApplication::quitOnLastWindowClosed());
    QTRY_VERIFY(window.isVisible());
    QTRY_COMPARE(placement.placements, 1);
    QVERIFY(placement.handle != 0);
    QCOMPARE(placement.gap, qRound(8 * window.devicePixelRatio()));
    QVERIFY(!placement.activationRequested);
    QVERIFY(placement.guiThreadOnly);
    QVERIFY(state->workerOnly);
    QVERIFY(window.property("integrationStatus").toString().contains(QStringLiteral("面板")));

    QVERIFY(window.close());
    QTRY_VERIFY(!window.isVisible());
    tray.toggle();
    QTRY_VERIFY(window.isVisible());
    QTRY_COMPARE(placement.placements, 2);
    tray.requestMenu();
    QCOMPARE(tray.menus, 1);
    tray.toggle();
    QVERIFY(!window.isVisible());
    tray.toggle();
    QTRY_VERIFY(window.isVisible());
}

void DesktopWindowHostTest::hiddenWindowRejectsDelayedAnchor() {
    auto state = std::make_shared<AnchorState>();
    state->delay = true;
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    QSignalSpy ready(&shell, &xdstarter::DesktopShellService::startButtonAnchorReady);

    host.start(trayIcon());
    QTRY_COMPARE(state->queries.load(), 1);
    host.dismissWindow();
    QVERIFY(!window.isVisible());
    state->delay = false;
    state->release.release();
    QTRY_COMPARE(state->completed.load(), 1);
    QTest::qWait(30);
    QCOMPARE(placement.placements, 0);
    QCOMPARE(ready.count(), 0);

    // A fresh reveal must still work after the obsolete worker result is discarded.
    host.showWindow();
    QTRY_COMPARE(placement.placements, 1);
    QCOMPARE(state->queries.load(), 2);
    QCOMPARE(ready.count(), 1);
}

void DesktopWindowHostTest::failedTrayKeepsTaskbarRecovery() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);

    host.start(trayIcon());
    QVERIFY(QGuiApplication::quitOnLastWindowClosed());
    QTRY_VERIFY(window.isVisible());
    window.hide();
    host.keepReachableAfterLaunch();
    QTRY_COMPARE(window.visibility(), QWindow::Minimized);
    QVERIFY(window.isVisible());
    QVERIFY(window.property("integrationStatus").toString().contains(QStringLiteral("托盘不可用")));
    host.toggleWindow();
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

void DesktopWindowHostTest::lostTrayRecoversHiddenWindow() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);

    host.start(trayIcon());
    QTRY_VERIFY(window.isVisible());
    window.hide();
    tray.setRegistration(false);
    QTRY_COMPARE(window.visibility(), QWindow::Minimized);
    QVERIFY(QGuiApplication::quitOnLastWindowClosed());
    tray.setRegistration(true);
    QVERIFY(!QGuiApplication::quitOnLastWindowClosed());
    host.toggleWindow();
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
}

void DesktopWindowHostTest::shutdownIgnoresPendingWork() {
    auto state = std::make_shared<AnchorState>();
    state->delay = true;
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    QSignalSpy exit(&host, &xdstarter::DesktopWindowHost::exitRequested);

    host.start(trayIcon());
    QTRY_COMPARE(state->queries.load(), 1);
    tray.requestQuit();
    QCOMPARE(exit.count(), 1);
    host.shutdown();
    host.shutdown();
    QCOMPARE(tray.stops, 1);
    const auto before = window.visibility();
    tray.toggle();
    tray.requestMenu();
    tray.requestQuit();
    tray.setRegistration(false);
    QCOMPARE(tray.menus, 0);
    QCOMPARE(exit.count(), 1);
    QCOMPARE(window.visibility(), before);
    state->release.release();
    QTRY_COMPARE(state->completed.load(), 1);
    QTest::qWait(30);
    QCOMPARE(placement.placements, 0);
}

void DesktopWindowHostTest::restoresApplicationPolicy_data() {
    QTest::addColumn<bool>("quitOnClose");
    QTest::newRow("restore-resident") << false;
    QTest::newRow("restore-default") << true;
}

void DesktopWindowHostTest::restoresApplicationPolicy() {
    QFETCH(bool, quitOnClose);
    QGuiApplication::setQuitOnLastWindowClosed(quitOnClose);
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    {
        xdstarter::DesktopWindowHost host(window, shell, placement, tray);
        host.start(trayIcon());
        QVERIFY(!QGuiApplication::quitOnLastWindowClosed());
    }
    QCOMPARE(tray.stops, 1);
    QCOMPARE(QGuiApplication::quitOnLastWindowClosed(), quitOnClose);
    // Avoid posting a runner quit when the local window is destroyed.
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

void DesktopWindowHostTest::externalFocusDismissesAndRestores() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_VERIFY(window.isActive());
    QVERIFY(window.managed);
    window.setPopup(true);
    QTest::qWait(150);
    QVERIFY(window.isVisible());

    QWindow other;
    other.show();
    other.requestActivate();
    QTRY_VERIFY(other.isActive());
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(!window.popup);
    QVERIFY(window.editMode);
    QVERIFY(window.dismissals > 0);
    const int presentations = window.presentations;
    tray.toggle();
    QTRY_VERIFY(window.isVisible());
    QTRY_VERIFY(window.isActive());
    QCOMPARE(window.presentations, presentations + 1);
    QTest::qWait(150);
    QVERIFY(window.isVisible());
    window.requestDismiss();
    QVERIFY(!window.isVisible());
}

void DesktopWindowHostTest::transientFocusAndQuickReturnStayOpen() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_VERIFY(window.isActive());
    QWindow child;
    child.setTransientParent(&window);
    child.show();
    child.requestActivate();
    QTRY_COMPARE(QGuiApplication::focusWindow(), &child);
    QTest::qWait(150);
    QVERIFY(window.isVisible());
    child.hide();
    window.requestActivate();
    QTRY_VERIFY(window.isActive());
    QWindow other;
    other.show();
    other.requestActivate();
    QTRY_VERIFY(other.isActive());
    // A new reveal invalidates the outstanding delayed external-focus check.
    host.showWindow();
    QTRY_VERIFY(window.isActive());
    QTest::qWait(150);
    QVERIFY(window.isVisible());
}

void DesktopWindowHostTest::externalFocusWaitsForDragRelease() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_VERIFY(window.isActive());
    window.setDrag(true);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QVERIFY(QGuiApplication::mouseButtons().testFlag(Qt::LeftButton));
    QWindow other;
    other.show();
    other.requestActivate();
    QTRY_VERIFY(other.isActive());
    QTest::qWait(200);
    QVERIFY(window.isVisible());
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(-20, -20));
    window.setDrag(false);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(!window.dragging());
    QVERIFY(window.editMode);
}

void DesktopWindowHostTest::externalDismissalWithoutTrayMinimizes() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_VERIFY(window.isActive());
    QWindow other;
    other.show();
    other.requestActivate();
    QTRY_VERIFY(other.isActive());
    QTRY_COMPARE(window.visibility(), QWindow::Minimized);
    QVERIFY(window.isVisible());
    host.showWindow();
    QTRY_VERIFY(window.isActive());
    QCOMPARE(window.visibility(), QWindow::Windowed);
    window.requestDismiss();
    QCOMPARE(window.visibility(), QWindow::Minimized);
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

void DesktopWindowHostTest::delayedLaunchRecoveryDoesNotMinimizeReopenedWindow() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_VERIFY(window.isActive());
    window.hide();
    host.keepReachableAfterLaunch();
    host.showWindow();
    QTest::qWait(150);
    QCOMPARE(window.visibility(), QWindow::Windowed);
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

void DesktopWindowHostTest::repeatedRevealPlacesHiddenAtStableMargins() {
    auto state = std::make_shared<AnchorState>();
    state->alternateTrust = true;
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(688, 528);
    PlacementPlatform placement;
    placement.observedWindow = &window;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    for (int round = 0; round < 12; ++round) {
        QTRY_VERIFY(window.isVisible());
        QTRY_COMPARE(placement.placements, round + 1);
        QCOMPARE(placement.inset, qRound(24 * window.devicePixelRatio()));
        const auto panel = placement.panels.last();
        const auto area = placement.lastAnchor.workAreaPhysicalRect;
        QCOMPARE(panel.left() - area.left(), placement.gap);
        QCOMPARE(area.bottom() - panel.bottom(), placement.gap);
        QCOMPARE(panel, placement.panels.first());
        QVERIFY(!placement.placedWhileVisible);
        tray.toggle();
        QVERIFY(!window.isVisible());
        if (round < 11)
            tray.toggle();
    }
}

void DesktopWindowHostTest::pendingRevealCanBeCanceled() {
    auto state = std::make_shared<AnchorState>();
    state->delay = true;
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_COMPARE(state->queries.load(), 1);
    QVERIFY(!window.isVisible());
    tray.toggle();
    state->delay = false;
    state->release.release();
    QTRY_COMPARE(state->completed.load(), 1);
    QTest::qWait(50);
    QCOMPARE(placement.placements, 0);
    QVERIFY(!window.isVisible());
    tray.toggle();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, 1);
}

void DesktopWindowHostTest::visibleReactivationDoesNotMoveAgain() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QTRY_VERIFY(window.isActive());
    QCOMPARE(placement.placements, 1);
    host.showWindow();
    QTest::qWait(150);
    QCOMPARE(state->queries.load(), 1);
    QCOMPARE(placement.placements, 1);
    QVERIFY(window.isVisible());
}

// Opt-in, inert second application window for reproducible native focus tests.
// Normal CTest never displays it or interacts with another desktop application.
class ManualFocusWindow final : public QRasterWindow {
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(QRect(QPoint(), size()), QColor("#E6EBF4"));
        painter.setPen(QColor("#172338"));
        painter.drawText(QRect(24, 24, width() - 48, height() - 48),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("XDStarter 外部焦点验证窗口\n\n点击空白区域验证菜单收起；\n此窗口不读写任何文件。"));
    }
};

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--manual-focus-window"))) {
        QGuiApplication::setQuitOnLastWindowClosed(true);
        ManualFocusWindow window;
        window.setTitle(QStringLiteral("XDStarter 外部焦点验证窗口"));
        window.setGeometry(1020, 80, 460, 260);
        window.show();
        return application.exec();
    }
    QGuiApplication::setQuitOnLastWindowClosed(false);
    DesktopWindowHostTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_desktop_window_host.moc"
