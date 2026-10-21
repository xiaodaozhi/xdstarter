#pragma once

#include "platform/desktop_platform.h"

#include <QElapsedTimer>
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
    void start(const QImage &icon);
    void attachWinKeyCapture(WinKeyCapture &capture);
    void showWindow();
    void toggleWindow();
    void keepReachableAfterLaunch();
    void shutdown();

  public slots:
    void dismissWindow();
    void toggleWinKeyCapture();

  private slots:
    void scheduleFocusCheck();

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
    QPointer<QWindow> window_;
    DesktopShellService &shell_;
    DesktopPlatform &placement_;
    DesktopTray &tray_;
    QPointer<WinKeyCapture> winKeyCapture_;
    QString trayStatus_;
    QString anchorStatus_;
    QString appearanceStatus_;
    QString winKeyStatus_;
    QTimer focusTimer_;
    QTimer revealTimer_;
    QElapsedTimer revealDeadline_;
    HiddenWindowReadiness readiness_;
    DesktopAnchorResult revealAnchor_;
    QPointer<QScreen> targetScreen_;
    quint64 placementRequest_ = 0;
    quint64 preparationRequest_ = 0;
    bool recoveryAvailable_ = false;
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
