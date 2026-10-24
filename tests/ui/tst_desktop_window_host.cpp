#include "app/desktop_window_host.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/start_button_capture.h"
#include "platform/win_key_capture.h"
#include "services/desktop_shell_service.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRasterWindow>
#include <QSemaphore>
#include <QScreen>
#include <QSignalSpy>
#include <QThread>
#include <QWindow>
#include <QtTest>

#include <atomic>
#include <memory>
#include <limits>
#include <functional>

namespace {

struct AnchorState {
    std::atomic_bool delay{false};
    std::atomic_bool workerOnly{true};
    std::atomic_int queries{0};
    std::atomic_int completed{0};
    bool alternateTrust = false;
    QString monitorName;
    QRect workArea;
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
        if (state_->workArea.isValid())
            anchor.workAreaPhysicalRect = state_->workArea;
        anchor.dpi = 144;
        anchor.monitorName = state_->monitorName;
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

    QScreen *screenForAnchor(const xdstarter::DesktopAnchorResult &anchor) override {
        ++screenMappings;
        return !nativeScreenIdentity.isEmpty() && anchor.monitorName == nativeScreenIdentity
                   ? QGuiApplication::primaryScreen()
                   : DesktopPlatform::screenForAnchor(anchor);
    }

    xdstarter::DesktopAppearanceResult configureMenuWindow(quintptr) override {
        if (afterConfigure)
            afterConfigure();
        return {};
    }

    xdstarter::DesktopWindowMetricsResult queryWindowMetrics(quintptr) override {
        ++metricQueries;
        if (!reportNativeSize)
            return {};
        const QSize size = physicalSizeOverride.isValid()
                               ? physicalSizeOverride
                               : QSize(qRound(observedWindow->width() * observedWindow->devicePixelRatio()),
                                       qRound(observedWindow->height() * observedWindow->devicePixelRatio()));
        return {true, nativeQuerySucceeds, size};
    }

    bool hasOwnedForegroundWindow(quintptr nativeHandle) override {
        ++foregroundQueries;
        foregroundHandle = nativeHandle;
        guiThreadOnly = guiThreadOnly &&
                        QThread::currentThread() == QCoreApplication::instance()->thread();
        return ownedForeground;
    }
    bool hasOwnedForegroundDialog(quintptr) override { return ownedDialog; }

    xdstarter::DesktopPresentationResult presentMenuWindow(quintptr nativeHandle) override {
        ++presentations;
        presentedHandle = nativeHandle;
        presentedWhileVisible = !observedWindow || observedWindow->isVisible();
        return presentationResult;
    }

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
        if (afterPlace)
            afterPlace();
        return {placementError.isEmpty(), QRect(24, 200, 640, 480), {}, placementError};
    }

    int placements = 0;
    int presentations = 0;
    quintptr presentedHandle = 0;
    bool presentedWhileVisible = false;
    int screenMappings = 0;
    QString nativeScreenIdentity;
    int metricQueries = 0;
    bool reportNativeSize = false;
    bool nativeQuerySucceeds = true;
    QSize physicalSizeOverride;
    std::function<void()> afterConfigure;
    std::function<void()> afterPlace;
    quintptr handle = 0;
    int gap = 0;
    int inset = 0;
    QWindow *observedWindow = nullptr;
    bool placedWhileVisible = false;
    QList<QRect> panels;
    bool activationRequested = false;
    bool guiThreadOnly = true;
    bool ownedForeground = false;
    bool ownedDialog = false;
    int foregroundQueries = 0;
    quintptr foregroundHandle = 0;
    xdstarter::DesktopAnchorResult lastAnchor;
    QString placementError;
    xdstarter::DesktopPresentationResult presentationResult;
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
        if (nextStateUpdate) {
            auto callback = std::move(nextStateUpdate);
            callback();
        }
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
    std::function<void()> nextStateUpdate;
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

class RecordingStartButtonCapture final : public xdstarter::StartButtonCapture {
  public:
    void setEnabled(bool enabled) override {
        generation = enabled ? beginEventSession() : endEventSession();
        active = enabled;
        emit statusChanged(active, QStringLiteral("Fixture hook state"), {});
    }
    void stop() override { setEnabled(false); }
    bool isActive() const override { return active; }
    bool isSupported() const override { return true; }
    QJsonObject diagnosticSnapshot() const override { return {}; }
    void recordHostObservation(const xdstarter::StartButtonHostObservation& observation) override {
        observations.append(observation);
    }
    void toggle() { emit toggleRequested(); }
    void queueGesture(quint64 id) {
        queueToggleRequested(xdstarter::CaptureToggleContext{generation, id, 100, 150});
    }
    void queueFailure(quint64 observedGeneration) {
        queueStatusChanged(observedGeneration, false, QStringLiteral("Delayed query failure"),
                           QStringLiteral("fixture-delayed-query-failure"));
    }
    quint64 generation = 0;
    bool active = false;
    QList<xdstarter::StartButtonHostObservation> observations;
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
    Q_PROPERTY(int dismissAnimationDuration MEMBER animationDuration)
  public:
    MenuWindow() { setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint); }
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
    Q_INVOKABLE void stagePresentation() {
        ++stages;
        stagedWhileVisible |= isVisible();
    }
    Q_INVOKABLE void restorePresentationFocus() { ++focusRestorations; }
    Q_INVOKABLE void animateDismissal() { ++animations; }
    void requestDismiss() { emit dismissRequested(); }
    bool managed = false;
    bool editMode = true;
    int dismissals = 0;
    int presentations = 0;
    int stages = 0;
    int focusRestorations = 0;
    bool stagedWhileVisible = false;
    int animationDuration = 0;
    int animations = 0;
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
    void startsInTrayWithoutShowingMenu();
    void presentationIsStagedOnceBeforeShowing();
    void repeatedDismissalFinishesAnimation();
    void trayFailureCanRetryOrExit();
    void recoverySurfaceAvoidsTaskbarAndRestoresResizeEdges();
    void placementFallbackAvoidsTaskbarUntilNextNativePlacement();
    void oversizedRevealReservesAllMargins();
    void inputRegionFailureDoesNotRevealUnclippedWindow_data();
    void inputRegionFailureDoesNotRevealUnclippedWindow();
    void reopeningCancelsPendingDismissAnimation();
    void cleanup();
    void trayRestoresClosedWindow();
    void hiddenWindowRejectsDelayedAnchor();
    void failedTrayKeepsVisibleRecovery();
    void lostTrayRecoversHiddenWindow();
    void lostTrayRestoresMinimizedToolWindow();
    void shutdownIgnoresPendingWork();
    void restoresApplicationPolicy_data();
    void restoresApplicationPolicy();
    void externalFocusDismissesAndRestores();
    void transientFocusAndQuickReturnStayOpen();
    void nativeOwnedFocusPollsAndDetectsExternalSwitch();
    void nativeOwnedPollingStopsOnQtFocusAndShutdown();
    void externalFocusWaitsForDragRelease();
    void externalDismissalWithoutTrayKeepsRecoveryVisible();
    void delayedLaunchRecoveryDoesNotMinimizeReopenedWindow();
    void repeatedRevealPlacesHiddenAtStableMargins();
    void pendingRevealCanBeCanceled();
    void visibleReactivationDoesNotMoveAgain();
    void hiddenReadinessTracksDpiAndResize();
    void nativePreparationCanBeCanceledAndRestarted();
    void newRevealWaitsForItsOwnAnchor();
    void nativePreparationRejectsLateCloseAndShutdown();
    void nativePreparationTimeoutKeepsRecovery_data();
    void nativePreparationTimeoutKeepsRecovery();
    void nativePreparationRejectsReentrantCancellation_data();
    void nativePreparationRejectsReentrantCancellation();
    void nativeScreenIdentityIsResolvedByPlatform();
    void missingNativeScreenDoesNotUsePreviousScreen();
    void hiddenMigrationPreservesLogicalWindowSize();
    void canceledMigrationRetainsIntendedWindowSize();
    void winKeyCaptureRequiresOptInAndStops();
    void winKeyCaptureFailureAndTrayLoss();
    void unsupportedWinKeyCaptureStaysDisabled();
    void trayFailureDuringEnableDoesNotInstallCapture();
    void startButtonCaptureStartsInTrayAndCanPause();
    void startButtonCaptureStopsOnTrayLossAndShutdown();
    void startButtonCaptureRejectsUnavailableAndLateSignals();
    void startButtonDiagnosticsDistinguishNativeForegroundAndHidden();
    void startButtonOldQueuedFailureCannotDisableNewSession();

  private:
    bool originalQuitOnClose_ = false;
};

void DesktopWindowHostTest::startButtonCaptureStartsInTrayAndCanPause() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.observedWindow = &window;
    FakeTray tray;
    FakeWinKeyCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachStartButtonCapture(capture);
    QCOMPARE(capture.requests, 0);
    host.start(trayIcon());
    QCOMPARE(capture.requests, 1);
    QVERIFY(capture.active);
    QVERIFY(!window.isVisible());
    capture.toggle();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, 1);
    QCOMPARE(placement.presentations, 1);
    QCOMPARE(placement.presentedHandle, static_cast<quintptr>(window.winId()));
    QVERIFY(placement.presentedWhileVisible);
    host.toggleStartButtonCapture();
    QVERIFY(!capture.active);
    capture.toggle();
    QVERIFY(window.isVisible());
    host.toggleStartButtonCapture();
    QVERIFY(capture.active);
    capture.toggle();
    QTRY_VERIFY(!window.isVisible());
    host.shutdown();
    QVERIFY(!capture.active);
}

void DesktopWindowHostTest::startButtonCaptureStopsOnTrayLossAndShutdown() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    FakeWinKeyCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachStartButtonCapture(capture);
    host.start(trayIcon());
    QVERIFY(capture.active);
    tray.available = false;
    emit tray.registrationChanged(false, QStringLiteral("fixture failure"));
    QVERIFY(!capture.active);
    tray.available = true;
    emit tray.registrationChanged(true, {});
    QVERIFY(!capture.active); // recovery never silently reinstalls capture
    host.toggleStartButtonCapture();
    QVERIFY(capture.active);
    host.shutdown();
    QVERIFY(!capture.active);
    const int requests = capture.requests;
    host.toggleStartButtonCapture();
    QCOMPARE(capture.requests, requests);
}

void DesktopWindowHostTest::startButtonCaptureRejectsUnavailableAndLateSignals() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    FakeWinKeyCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachStartButtonCapture(capture);
    host.start(trayIcon());
    QCOMPARE(capture.requests, 0);
    emit capture.statusChanged(true, {}, {});
    QVERIFY(!capture.active);
    QTRY_VERIFY(window.isVisible());
    const int queries = state->queries;
    capture.toggle();
    QTest::qWait(50);
    QCOMPARE(state->queries.load(), queries);
}

void DesktopWindowHostTest::startsInTrayWithoutShowingMenu() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon());
    QVERIFY(!window.isVisible());
    QTest::qWait(100);
    QVERIFY(!window.isVisible());
    host.showWindow();
    QTRY_VERIFY(window.isVisible());
}

void DesktopWindowHostTest::reopeningCancelsPendingDismissAnimation() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(640, 480);
    window.animationDuration = 80;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    window.requestDismiss();
    QCOMPARE(window.animations, 1);
    QVERIFY(window.isVisible());
    placement.ownedForeground = true; // Exact native foreground can be the root itself.
    host.showWindow();
    QCOMPARE(window.focusRestorations, 1);
    QTest::qWait(140);
    QVERIFY(window.isVisible());
    window.requestDismiss();
    QCOMPARE(window.animations, 2);
    QTRY_VERIFY(!window.isVisible());
    host.showWindow();
    QTRY_VERIFY(window.isVisible());
    window.requestDismiss();
    host.shutdown();
    QTest::qWait(140);
    QVERIFY(window.isVisible()); // Shutdown canceled the outstanding timer.
}

void DesktopWindowHostTest::presentationIsStagedOnceBeforeShowing() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(640, 480);
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    int stagesAtFirstVisibility = 0;
    connect(&window, &QWindow::visibleChanged, this, [&](bool visible) {
        if (visible && !stagesAtFirstVisibility)
            stagesAtFirstVisibility = window.stages;
    });
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(stagesAtFirstVisibility, 1);
    QCOMPARE(window.presentations, 1);
    QVERIFY(!window.stagedWhileVisible);
    host.showWindow();
    QCOMPARE(window.stages, 1);
    QCOMPARE(window.presentations, 1);
    QCOMPARE(window.focusRestorations, 1);
    placement.ownedForeground = true;
    placement.ownedDialog = true;
    host.showWindow();
    QCOMPARE(window.focusRestorations, 1);
    QCOMPARE(window.presentations, 1);
}

void DesktopWindowHostTest::repeatedDismissalFinishesAnimation() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.animationDuration = 80;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    host.dismissWindow();
    QTest::qWait(50);
    host.dismissWindow();
    QCOMPARE(window.animations, 1);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(window.visibility() != QWindow::Minimized);
    QCOMPARE(window.animations, 1);
}

void DesktopWindowHostTest::trayFailureCanRetryOrExit() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    QSignalSpy exit(&host, &xdstarter::DesktopWindowHost::exitRequested);
    host.start(trayIcon());
    QTRY_VERIFY(window.isVisible());
    QVERIFY(window.property("trayRecoveryUnavailable").toBool());
    QVERIFY(window.visibility() != QWindow::Minimized);
    QVERIFY(window.flags().testFlag(Qt::Tool));
    host.dismissWindow();
    QCOMPARE(exit.count(), 1);
    QVERIFY(window.isVisible());
    tray.registrationSucceeds = true;
    QVERIFY(QMetaObject::invokeMethod(&host, "retryTrayRegistration"));
    QCOMPARE(tray.starts, 2);
    QVERIFY(!window.property("trayRecoveryUnavailable").toBool());
    host.dismissWindow();
    QVERIFY(!window.isVisible());
}

void DesktopWindowHostTest::recoverySurfaceAvoidsTaskbarAndRestoresResizeEdges() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.setMinimumSize(QSize(528, 528));
    window.resize(5000, 4000);
    PlacementPlatform placement;
    placement.observedWindow = &window;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    window.hide();
    tray.setRegistration(false);
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(window.property("allowedResizeEdges").toInt(), 0);
    QVERIFY(window.screen());
    const QRect safe = window.screen()->availableGeometry().adjusted(8, 8, -8, -8);
    QVERIFY(safe.contains(window.geometry()));
    QVERIFY(window.maximumWidth() <= safe.width());
    QVERIFY(window.maximumHeight() <= safe.height());
    QVERIFY(window.minimumWidth() <= window.maximumWidth());
    QVERIFY(window.minimumHeight() <= window.maximumHeight());
    const int placementsBeforeRetry = placement.placements;
    tray.registrationSucceeds = true;
    QVERIFY(QMetaObject::invokeMethod(&host, "retryTrayRegistration"));
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, placementsBeforeRetry + 1);
    QCOMPARE(window.property("allowedResizeEdges").toInt(),
             static_cast<int>(Qt::TopEdge | Qt::RightEdge));
    QVERIFY(!placement.placedWhileVisible);
}

void DesktopWindowHostTest::placementFallbackAvoidsTaskbarUntilNextNativePlacement() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(5000, 4000);
    PlacementPlatform placement;
    placement.placementError = QStringLiteral("window-position-fixture-failed");
    placement.observedWindow = &window;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    QVERIFY(window.screen());
    const QRect safe = window.screen()->availableGeometry().adjusted(8, 8, -8, -8);
    QVERIFY(safe.contains(window.geometry()));
    QCOMPARE(window.property("allowedResizeEdges").toInt(), 0);
    QVERIFY(window.property("menuConservativeWorkAreaFallback").toBool());
    host.dismissWindow();
    QVERIFY(!window.isVisible());
    placement.placementError.clear();
    host.showWindow();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, 2);
    QCOMPARE(window.property("allowedResizeEdges").toInt(),
             static_cast<int>(Qt::TopEdge | Qt::RightEdge));
    QVERIFY(!window.property("menuConservativeWorkAreaFallback").toBool());
}

void DesktopWindowHostTest::oversizedRevealReservesAllMargins() {
    auto state = std::make_shared<AnchorState>();
    state->workArea = QRect(0, 0, 420, 360);
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.setMinimumSize(QSize(528, 528));
    window.resize(5000, 4000);
    PlacementPlatform placement;
    placement.observedWindow = &window;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, 1);
    const int gap = qRound(8 * window.devicePixelRatio());
    const auto allowed = state->workArea.adjusted(gap, gap, -gap, -gap);
    QVERIFY(allowed.contains(placement.panels.last()));
    QVERIFY(window.minimumWidth() <= window.maximumWidth());
    QVERIFY(window.minimumHeight() <= window.maximumHeight());
    QVERIFY(window.width() <= window.maximumWidth());
    QVERIFY(window.height() <= window.maximumHeight());
    QVERIFY(!placement.placedWhileVisible);
}

void DesktopWindowHostTest::inputRegionFailureDoesNotRevealUnclippedWindow_data() {
    QTest::addColumn<bool>("trayAvailable");
    QTest::newRow("tray-remains-reachable") << true;
    QTest::newRow("no-tray-exits") << false;
}

void DesktopWindowHostTest::inputRegionFailureDoesNotRevealUnclippedWindow() {
    QFETCH(bool, trayAvailable);
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.placementError = QStringLiteral("menu-input-region-failed");
    FakeTray tray;
    tray.registrationSucceeds = trayAvailable;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    QSignalSpy exit(&host, &xdstarter::DesktopWindowHost::exitRequested);
    host.start(trayIcon(), true);
    QTRY_COMPARE(placement.placements, 1);
    QTest::qWait(100);
    QVERIFY(!window.isVisible());
    QCOMPARE(window.stages, 0);
    QCOMPARE(placement.presentations, 0);
    QCOMPARE(exit.count(), trayAvailable ? 0 : 1);
}

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
    host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
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
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
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
    host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
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

    host.start(trayIcon(), true);
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

    host.start(trayIcon(), true);
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

void DesktopWindowHostTest::failedTrayKeepsVisibleRecovery() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    QWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);

    host.start(trayIcon(), true);
    QVERIFY(QGuiApplication::quitOnLastWindowClosed());
    QTRY_VERIFY(window.isVisible());
    window.hide();
    host.keepReachableAfterLaunch();
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
    QVERIFY(window.isVisible());
    QVERIFY(window.property("integrationStatus").toString().contains(QStringLiteral("托盘不可用")));
    host.showWindow();
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

    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    window.hide();
    tray.setRegistration(false);
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
    QVERIFY(QGuiApplication::quitOnLastWindowClosed());
    tray.setRegistration(true);
    QVERIFY(!QGuiApplication::quitOnLastWindowClosed());
    host.showWindow();
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
}

void DesktopWindowHostTest::lostTrayRestoresMinimizedToolWindow() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    window.showMinimized();
    QTRY_COMPARE(window.visibility(), QWindow::Minimized);
    QVERIFY(window.isVisible());
    tray.setRegistration(false);
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
    QVERIFY(window.isVisible());
    QVERIFY(window.property("trayRecoveryUnavailable").toBool());
    QVERIFY(window.stages >= 2);
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

    host.start(trayIcon(), true);
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
        host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
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

void DesktopWindowHostTest::nativeOwnedFocusPollsAndDetectsExternalSwitch() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isActive());
    window.setPopup(true);
    placement.ownedForeground = true;
    QWindow untracked;
    untracked.show();
    untracked.requestActivate();
    QTRY_VERIFY(untracked.isActive());
    // The platform reports an OS-owned foreground with no Qt transient link.
    // Repeated checks prove protection lasts beyond one focus grace period.
    QTRY_VERIFY(placement.foregroundQueries >= 3);
    QVERIFY(window.isVisible());
    QCOMPARE(placement.foregroundHandle, quintptr(window.winId()));
    QVERIFY(placement.guiThreadOnly);
    const int dismissals = window.dismissals;
    // No new Qt event: the native dialog loses foreground to an external app.
    // An unconditional popup guard or stopping the native poll would miss it.
    placement.ownedForeground = false;
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(window.dismissals > dismissals);
    QVERIFY(!window.popup);
    QVERIFY(window.editMode);
}

void DesktopWindowHostTest::nativeOwnedPollingStopsOnQtFocusAndShutdown() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isActive());
    placement.ownedForeground = true;
    QWindow untracked;
    untracked.show();
    untracked.requestActivate();
    QTRY_VERIFY(untracked.isActive());
    QTRY_VERIFY(placement.foregroundQueries >= 2);
    window.requestActivate();
    QTRY_VERIFY(window.isActive());
    const int returnedQueries = placement.foregroundQueries;
    QTest::qWait(250);
    QCOMPARE(placement.foregroundQueries, returnedQueries);
    QVERIFY(window.isVisible());

    untracked.requestActivate();
    QTRY_VERIFY(untracked.isActive());
    QTRY_VERIFY(placement.foregroundQueries > returnedQueries);
    host.shutdown();
    const int stoppedQueries = placement.foregroundQueries;
    placement.ownedForeground = false;
    QTest::qWait(250);
    QCOMPARE(placement.foregroundQueries, stoppedQueries);
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
    host.start(trayIcon(), true);
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

void DesktopWindowHostTest::externalDismissalWithoutTrayKeepsRecoveryVisible() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    tray.registrationSucceeds = false;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isActive());
    QWindow other;
    other.show();
    other.requestActivate();
    QTRY_VERIFY(other.isActive());
    QTest::qWait(200);
    QCOMPARE(window.visibility(), QWindow::Windowed);
    QVERIFY(window.isVisible());
    host.showWindow();
    QTRY_VERIFY(window.isActive());
    QCOMPARE(window.visibility(), QWindow::Windowed);
    QSignalSpy exit(&host, &xdstarter::DesktopWindowHost::exitRequested);
    window.requestDismiss();
    QCOMPARE(exit.count(), 1);
    QCOMPARE(window.visibility(), QWindow::Windowed);
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
    host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
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
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isActive());
    QCOMPARE(placement.placements, 1);
    host.showWindow();
    QTest::qWait(150);
    QCOMPARE(state->queries.load(), 1);
    QCOMPARE(placement.placements, 1);
    QVERIFY(window.isVisible());
}

void DesktopWindowHostTest::hiddenReadinessTracksDpiAndResize() {
    xdstarter::HiddenWindowReadiness gate;
    using Metrics = xdstarter::DesktopWindowMetricsResult;
    const QSize logical(1028, 728);
    const Metrics low{true, true, logical};
    const Metrics high{true, true, QSize(1542, 1092)};

    // A changed screen identity alone must not authorize the old 96-DPI size.
    QVERIFY(!gate.observe(false, 1.5, 1, logical, low));
    QVERIFY(!gate.observe(true, 1.5, 1, logical, low));
    QVERIFY(!gate.observe(true, 1.5, 1.5, logical, low));
    QVERIFY(!gate.observe(true, 1.5, 1.5, logical, high));
    QVERIFY(gate.observe(true, 1.5, 1.5, logical, high));

    // The opposite direction also waits for both Qt and the native surface.
    QVERIFY(!gate.observe(true, 1, 1.5, logical, high));
    QVERIFY(!gate.observe(true, 1, 1, logical, high));
    QVERIFY(!gate.observe(true, 1, 1, logical, low));
    QVERIFY(gate.observe(true, 1, 1, logical, low));

    // A resize between samples cannot reuse readiness from the old geometry.
    const QSize resized(1040, 750);
    const Metrics newSize{true, true, resized};
    QVERIFY(!gate.observe(true, 1, 1, resized, low));
    QVERIFY(!gate.observe(true, 1, 1, resized, newSize));
    QVERIFY(gate.observe(true, 1, 1, resized, newSize));
    gate.reset();
    QVERIFY(!gate.observe(true, 1, 1, resized, newSize));
    QVERIFY(!gate.observe(true, 1, 1, resized, Metrics{true, false, resized}));
    QVERIFY(!gate.observe(true, 1, 1, resized, newSize));
    QVERIFY(!gate.observe(true, 1, std::numeric_limits<qreal>::quiet_NaN(), resized, newSize));
    QVERIFY(!gate.observe(true, 1, 1, resized, newSize));
    QVERIFY(gate.observe(true, 1, 1, resized, newSize));
}

void DesktopWindowHostTest::nativePreparationCanBeCanceledAndRestarted() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    window.resize(688, 528);
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.reportNativeSize = true;
    placement.physicalSizeOverride = QSize(1, 1);
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(placement.metricQueries >= 2);
    QCOMPARE(placement.placements, 0);
    QVERIFY(!window.isVisible());
    tray.toggle();
    const int queries = placement.metricQueries;
    placement.physicalSizeOverride = {};
    QTest::qWait(80);
    QCOMPARE(placement.metricQueries, queries);
    QCOMPARE(placement.placements, 0);
    QVERIFY(!window.isVisible());
    tray.toggle();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, 1);
    QVERIFY(!placement.placedWhileVisible);
    QCOMPARE(state->queries.load(), 2);
}

void DesktopWindowHostTest::newRevealWaitsForItsOwnAnchor() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.reportNativeSize = true;
    placement.nativeQuerySucceeds = false;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(placement.metricQueries > 0);
    state->delay = true;
    host.showWindow();
    QTRY_COMPARE(state->queries.load(), 2);
    placement.nativeQuerySucceeds = true;
    const int queries = placement.metricQueries;
    QTest::qWait(80);
    QCOMPARE(placement.metricQueries, queries);
    QCOMPARE(placement.placements, 0);
    QVERIFY(!window.isVisible());
    state->delay = false;
    state->release.release();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.placements, 1);
}

void DesktopWindowHostTest::nativePreparationRejectsLateCloseAndShutdown() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.reportNativeSize = true;
    placement.nativeQuerySucceeds = false;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(placement.metricQueries > 0);
    window.close();
    placement.nativeQuerySucceeds = true;
    QTest::qWait(80);
    QVERIFY(!window.isVisible());
    QCOMPARE(placement.placements, 0);

    placement.nativeQuerySucceeds = false;
    const int queries = placement.metricQueries;
    host.showWindow();
    QTRY_VERIFY(placement.metricQueries > queries);
    host.shutdown();
    placement.nativeQuerySucceeds = true;
    QTest::qWait(80);
    QVERIFY(!window.isVisible());
    QCOMPARE(placement.placements, 0);
}

void DesktopWindowHostTest::nativePreparationTimeoutKeepsRecovery_data() {
    QTest::addColumn<bool>("trayAvailable");
    QTest::newRow("tray-retry") << true;
    QTest::newRow("visible-retry") << false;
}

void DesktopWindowHostTest::nativePreparationTimeoutKeepsRecovery() {
    QFETCH(bool, trayAvailable);
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.reportNativeSize = true;
    placement.nativeQuerySucceeds = false;
    FakeTray tray;
    tray.registrationSucceeds = trayAvailable;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.property("integrationStatus").toString().contains(QStringLiteral("本次显示已取消")));
    QCOMPARE(placement.placements, 0);
    if (trayAvailable)
        QVERIFY(!window.isVisible());
    else
        QCOMPARE(window.visibility(), QWindow::Windowed);
    const int queries = placement.metricQueries;
    QTest::qWait(80);
    QCOMPARE(placement.metricQueries, queries);
    placement.nativeQuerySucceeds = true;
    window.hide();
    host.showWindow();
    QTRY_COMPARE(window.visibility(), QWindow::Windowed);
    QCOMPARE(placement.placements, 1);
    QVERIFY(!placement.placedWhileVisible);
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

void DesktopWindowHostTest::nativePreparationRejectsReentrantCancellation_data() {
    QTest::addColumn<QString>("boundary");
    QTest::newRow("appearance") << QStringLiteral("appearance");
    QTest::newRow("native-placement") << QStringLiteral("placement");
    QTest::newRow("published-status") << QStringLiteral("status");
}

void DesktopWindowHostTest::nativePreparationRejectsReentrantCancellation() {
    QFETCH(QString, boundary);
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    const auto cancel = [&] { host.dismissWindow(); };
    if (boundary == QStringLiteral("appearance"))
        placement.afterConfigure = cancel;
    else if (boundary == QStringLiteral("placement"))
        placement.afterPlace = cancel;
    else
        placement.afterPlace = [&] { tray.nextStateUpdate = cancel; };
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.dismissals > 0);
    QTest::qWait(80);
    QVERIFY(!window.isVisible());
    QCOMPARE(window.presentations, 0);
    QCOMPARE(placement.placements, boundary == QStringLiteral("appearance") ? 0 : 1);
}

void DesktopWindowHostTest::nativeScreenIdentityIsResolvedByPlatform() {
    auto state = std::make_shared<AnchorState>();
    state->monitorName = QStringLiteral("native-display-identity");
    QVERIFY(QGuiApplication::primaryScreen()->name() != state->monitorName);
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.nativeScreenIdentity = state->monitorName;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(placement.screenMappings, 1);
    QCOMPARE(placement.placements, 1);
    QCOMPARE(window.screen(), QGuiApplication::primaryScreen());
}

void DesktopWindowHostTest::missingNativeScreenDoesNotUsePreviousScreen() {
    auto state = std::make_shared<AnchorState>();
    state->monitorName = QStringLiteral("disconnected-native-display");
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.property("integrationStatus").toString().contains(QStringLiteral("本次显示已取消")));
    QCOMPARE(placement.screenMappings, 1);
    QCOMPARE(placement.placements, 0);
    QVERIFY(!window.isVisible());
}

void DesktopWindowHostTest::hiddenMigrationPreservesLogicalWindowSize() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    const QSize initialSize(688, 528);
    window.resize(initialSize);
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.reportNativeSize = true;
    // Reproduce the logical-size side effect independently of a physical
    // monitor: the real mixed-DPI probe covers Qt's native DPI transition.
    placement.afterConfigure = [&] { window.resize(500, 400); };
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(window.size(), initialSize);
    QCOMPARE(placement.placements, 1);
    QVERIFY(!placement.placedWhileVisible);

    // A genuine visible resize becomes the next reveal's intended DIP size.
    const QSize userSize(812, 612);
    window.resize(userSize);
    QCOMPARE(window.size(), userSize);
    host.dismissWindow();
    host.showWindow();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(window.size(), userSize);
    QCOMPARE(placement.placements, 2);
    QVERIFY(!placement.placedWhileVisible);
}

void DesktopWindowHostTest::canceledMigrationRetainsIntendedWindowSize() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    const QSize intendedSize(744, 568);
    window.resize(intendedSize);
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.reportNativeSize = true;
    FakeTray tray;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    placement.afterConfigure = [&] {
        window.resize(500, 400);
        host.dismissWindow();
    };
    host.start(trayIcon(), true);
    QTRY_VERIFY(window.dismissals > 0);
    QVERIFY(!window.isVisible());
    QCOMPARE(window.size(), QSize(500, 400));
    QCOMPARE(placement.placements, 0);

    placement.afterConfigure = {};
    host.showWindow();
    QTRY_VERIFY(window.isVisible());
    QCOMPARE(window.size(), intendedSize);
    QCOMPARE(placement.placements, 1);
    QVERIFY(!placement.placedWhileVisible);
}

// Opt-in, inert second application window for reproducible native focus tests.
// Normal CTest never displays it or interacts with another desktop application.
void DesktopWindowHostTest::startButtonDiagnosticsDistinguishNativeForegroundAndHidden() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    placement.observedWindow = &window;
    placement.presentationResult = {true, true, false, false,
                                    QStringLiteral("menu-foreground-denied")};
    FakeTray tray;
    RecordingStartButtonCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachStartButtonCapture(capture);
    host.start(trayIcon());
    capture.queueGesture(701);
    QTRY_VERIFY(window.isVisible());
    QTRY_COMPARE(capture.observations.last().event, QStringLiteral("presentation-result"));
    QVERIFY(capture.observations.last().nativeMeasured);
    QVERIFY(!capture.observations.last().foreground);
    QCOMPARE(capture.observations.last().errorCode, QStringLiteral("menu-foreground-denied"));
    QCOMPARE(capture.observations.last().context.gestureId, quint64(701));
    QCOMPARE(capture.observations.last().context.releasedAt, quint64(150));
    QCOMPARE(capture.observations.last().requestedAction, QStringLiteral("show"));
    capture.queueGesture(702);
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(capture.observations.last().event, QStringLiteral("hidden"));
    QVERIFY(!capture.observations.last().visible);
    QVERIFY(!capture.observations.last().nativeMeasured);
    QCOMPARE(capture.observations.last().context.gestureId, quint64(702));
    QCOMPARE(capture.observations.last().requestedAction, QStringLiteral("hide"));
    placement.presentationResult = {true, true, false, true, {}};
    capture.toggle();
    QTRY_VERIFY(window.isVisible());
    QTRY_COMPARE(capture.observations.last().event, QStringLiteral("presentation-result"));
    QVERIFY(capture.observations.last().nativeMeasured);
    QVERIFY(capture.observations.last().foreground);
    capture.toggle();
    QVERIFY(!window.isVisible());
    // A Qt show or an unsupported platform result cannot confirm native focus,
    // even if its unmeasured foreground field happens to be true.
    placement.presentationResult = {false, true, false, true, {}};
    capture.toggle();
    QTRY_VERIFY(window.isVisible());
    QTRY_COMPARE(capture.observations.last().event, QStringLiteral("presentation-result"));
    QVERIFY(!capture.observations.last().nativeMeasured);
    QVERIFY(!capture.observations.last().foreground);
    host.dismissWindow();
    placement.placementError = QStringLiteral("menu-input-region-failed");
    capture.toggle();
    QTRY_COMPARE(capture.observations.last().event, QStringLiteral("presentation-canceled"));
    QCOMPARE(capture.observations.last().errorCode, QStringLiteral("menu-input-region-failed"));
    QVERIFY(!window.isVisible());
    placement.placementError.clear();
    placement.afterConfigure = [&] { window.close(); };
    capture.toggle();
    QTRY_COMPARE(capture.observations.last().errorCode,
                 QStringLiteral("window-closed-before-presentation"));
    QCOMPARE(capture.observations.last().event, QStringLiteral("presentation-canceled"));
    QVERIFY(!capture.observations.last().nativeMeasured);
    QVERIFY(!window.isVisible());
}

void DesktopWindowHostTest::startButtonOldQueuedFailureCannotDisableNewSession() {
    auto state = std::make_shared<AnchorState>();
    xdstarter::DesktopShellService shell(std::make_unique<AnchorPlatform>(state));
    MenuWindow window;
    PlacementPlatform placement;
    FakeTray tray;
    RecordingStartButtonCapture capture;
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachStartButtonCapture(capture);
    host.start(trayIcon());
    const auto oldGeneration = capture.generation;
    capture.queueFailure(oldGeneration);
    host.toggleStartButtonCapture(); // Pause before the queued failure is delivered.
    host.toggleStartButtonCapture(); // A new real WinKeyCapture event session.
    QVERIFY(capture.generation != oldGeneration);
    QCoreApplication::processEvents();
    QVERIFY(window.property("startButtonCaptureActive").toBool());
    QVERIFY(capture.active);
    capture.toggle();
    QTRY_VERIFY(window.isVisible()); // The replacement session still reaches host.
    capture.active = false;
    capture.queueFailure(capture.generation);
    QTRY_VERIFY(!window.property("startButtonCaptureActive").toBool());
    QVERIFY(window.property("startButtonCaptureStatus")
                .toString()
                .contains(QStringLiteral("Delayed query failure")));
}

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
