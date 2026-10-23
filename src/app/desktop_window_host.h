#pragma once

#include "platform/desktop_platform.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QImage;
class QEvent;
class QScreen;
class QWindow;

namespace xdstarter {
class DesktopPlatform;
class DesktopShellService;
class DesktopTray;
class WinKeyCapture;

// A native-free readiness gate. A screen move can update Qt's screen identity
// before its render DPR and native surface have caught up. Two matching samples
// on separate event-loop turns are required, and every mismatch resets the gate.
class HiddenWindowReadiness final {
  public:
    bool observe(bool onTargetScreen, qreal targetDpr, qreal renderDpr,
                 QSize logicalSize, const DesktopWindowMetricsResult &native);
    void reset();

  private:
    qreal dpr_ = 0;
    QSize logicalSize_;
    QSize physicalSize_;
    bool sampled_ = false;
};

// GUI boundary for a recoverable menu window. All Shell/UIA queries stay
// in DesktopShellService; only short native placement runs on this thread.
class DesktopWindowHost final : public QObject {
    Q_OBJECT
  public:
    DesktopWindowHost(QWindow &window, DesktopShellService &shell, DesktopPlatform &placement,
                      DesktopTray &tray, QObject *parent = nullptr);
    ~DesktopWindowHost() override;
    void start(const QImage& icon, bool showInitially = false);
    void attachWinKeyCapture(WinKeyCapture &capture);
    void attachStartButtonCapture(WinKeyCapture& capture);
    void showWindow();
    void toggleWindow();
    void keepReachableAfterLaunch();
    void shutdown();

  public slots:
    void dismissWindow();
    void toggleWinKeyCapture();
    void toggleStartButtonCapture();

  private slots:
    void scheduleFocusCheck();
    void retryTrayRegistration();

  signals:
    void exitRequested();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void requestPlacement();
    void toggleWindowFrom(const QString &source);
    void updateRecovery(bool available, const QString &message);
    void publishStatus();
    void publishWinKeyCapture();
    void publishStartButtonCapture();
    void activatePresentedWindow();
    bool ownsFocus() const;
    void checkFocus();
    void dismiss(const QString &reason);
    void prepareWindow(const char *method);
    void configureAppearance();
    void beginRevealPreparation(const DesktopAnchorResult &anchor);
    void checkRevealPreparation();
    void finishRevealPreparation();
    void cancelRevealPreparation();
    bool isCurrentReveal(quint64 request) const;
    void rememberVisibleWindowSize();
    void updateWindowConstraints();
    void showRecoveryWindow();
    QPointer<QWindow> window_;
    DesktopShellService &shell_;
    DesktopPlatform &placement_;
    DesktopTray &tray_;
    QPointer<WinKeyCapture> winKeyCapture_;
    QPointer<WinKeyCapture> startButtonCapture_;
    bool startButtonRequested_ = false;
    bool startButtonActive_ = false;
    QString startButtonStatus_;
    QString trayStatus_;
    QString anchorStatus_;
    QString appearanceStatus_;
    QString winKeyStatus_;
    QTimer focusTimer_;
    QTimer revealTimer_;
    QTimer dismissTimer_;
    bool dismissPending_ = false;
    QElapsedTimer revealDeadline_;
    HiddenWindowReadiness readiness_;
    DesktopAnchorResult revealAnchor_;
    QPointer<QScreen> targetScreen_;
    // Survives canceled screen migrations, which may temporarily change Qt's
    // logical size while preserving the native physical rectangle.
    QSize intendedWindowSize_;
    QSize preferredMinimumSize_;
    QImage trayIcon_;
    quint64 placementRequest_ = 0;
    quint64 preparationRequest_ = 0;
    bool recoveryAvailable_ = false;
    bool recoveryWindowLayout_ = false;
    bool stopping_ = false;
    bool previousQuitOnClose_ = true;
    bool focusArmed_ = false;
    bool trayMenuActive_ = false;
    bool started_ = false;
    bool pendingReveal_ = false;
    bool winKeyRequested_ = false;
    bool winKeyActive_ = false;
    bool winKeyPending_ = false;
};
} // namespace xdstarter
