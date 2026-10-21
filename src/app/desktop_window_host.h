#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class QImage;
class QEvent;
class QWindow;

namespace xdstarter {
class DesktopPlatform;
class DesktopShellService;
class DesktopTray;

// GUI boundary for a recoverable menu window. All Shell/UIA queries stay
// in DesktopShellService; only short native placement runs on this thread.
class DesktopWindowHost final : public QObject {
    Q_OBJECT
  public:
    DesktopWindowHost(QWindow &window, DesktopShellService &shell, DesktopPlatform &placement,
                      DesktopTray &tray, QObject *parent = nullptr);
    ~DesktopWindowHost() override;
    void start(const QImage &icon);
    void showWindow();
    void toggleWindow();
    void keepReachableAfterLaunch();
    void shutdown();

  public slots:
    void dismissWindow();

  private slots:
    void scheduleFocusCheck();

  signals:
    void exitRequested();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void requestPlacement();
    void updateRecovery(bool available, const QString &message);
    void publishStatus();
    bool ownsFocus() const;
    void checkFocus();
    void dismiss(const QString &reason);
    void prepareWindow(const char *method);
    void configureAppearance();
    QPointer<QWindow> window_;
    DesktopShellService &shell_;
    DesktopPlatform &placement_;
    DesktopTray &tray_;
    QString trayStatus_;
    QString anchorStatus_;
    QString appearanceStatus_;
    QTimer focusTimer_;
    quint64 placementRequest_ = 0;
    bool recoveryAvailable_ = false;
    bool stopping_ = false;
    bool previousQuitOnClose_ = true;
    bool focusArmed_ = false;
    bool trayMenuActive_ = false;
    bool started_ = false;
    bool pendingReveal_ = false;
};
} // namespace xdstarter
