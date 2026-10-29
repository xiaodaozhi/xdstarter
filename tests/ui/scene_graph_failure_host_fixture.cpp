#include "app/desktop_window_host.h"
#include "app/scene_graph_failure_guard.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/win_key_capture.h"
#include "services/desktop_shell_service.h"

#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>

#include <cstdlib>
#include <memory>
#include <utility>

namespace {

class FakePlatform final : public xdstarter::DesktopPlatform {
  public:
    xdstarter::DesktopActionResult openDesktopAction(const QString&) override { return {}; }
    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override { return {}; }
};

class FakeTray final : public xdstarter::DesktopTray {
  public:
    explicit FakeTray(QStringList& stopped) : stopped_(stopped) {}
    xdstarter::DesktopActionResult start(const QImage&) override {
        active = true;
        return {true, {}, {}};
    }
    void stop() override {
        active = false;
        ++stops;
        stopped_.append(QStringLiteral("tray"));
    }
    bool isAvailable() const override { return active; }
    void showContextMenu() override {}
    bool active = false;
    int stops = 0;

  private:
    QStringList& stopped_;
};

class FakeCapture final : public xdstarter::WinKeyCapture {
  public:
    FakeCapture(QString name, QStringList& stopped) : name_(std::move(name)), stopped_(stopped) {}
    void setEnabled(bool enabled) override {
        active = enabled;
        emit statusChanged(active, QStringLiteral("fixture"), {});
    }
    void stop() override {
        active = false;
        ++stops;
        stopped_.append(name_);
    }
    bool isActive() const override { return active; }
    bool isSupported() const override { return true; }
    bool active = false;
    int stops = 0;

  private:
    QString name_;
    QStringList& stopped_;
};

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    {
        // exit() before exec() cannot stop a future loop; the caller must be
        // able to observe the failure and return before entering it.
        QQuickWindow earlyWindow;
        xdstarter::SceneGraphFailureGuard earlyFailure(earlyWindow, application);
        emit earlyWindow.sceneGraphError(QQuickWindow::ContextNotAvailable,
                                         QStringLiteral("pre-loop fixture failure"));
        if (!earlyFailure.failed())
            return EXIT_FAILURE;
    }
    QQuickWindow window;
    xdstarter::SceneGraphFailureGuard failure(window, application);
    xdstarter::DesktopShellService shell(std::make_unique<FakePlatform>());
    FakePlatform placement;
    QStringList stopped;
    FakeTray tray(stopped);
    FakeCapture startButton(QStringLiteral("start"), stopped);
    FakeCapture winKey(QStringLiteral("win"), stopped);
    xdstarter::DesktopWindowHost host(window, shell, placement, tray);
    host.attachStartButtonCapture(startButton);
    host.attachWinKeyCapture(winKey);
    QImage icon(16, 16, QImage::Format_ARGB32);
    icon.fill(Qt::blue);
    host.start(icon);
    host.toggleWinKeyCapture();
    if (!tray.active || !startButton.active || !winKey.active)
        return EXIT_FAILURE;

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &host,
                     &xdstarter::DesktopWindowHost::shutdown);
    QTimer::singleShot(0, &window, [&window] {
        emit window.sceneGraphError(QQuickWindow::ContextNotAvailable,
                                    QStringLiteral("fixture graphics failure"));
        emit window.sceneGraphError(QQuickWindow::ContextNotAvailable,
                                    QStringLiteral("duplicate failure"));
    });
    QTimer::singleShot(5000, &application, [&application] { application.exit(99); });
    const int result = application.exec();
    if (result != EXIT_FAILURE || !failure.failed() || tray.active || startButton.active ||
        winKey.active || tray.stops != 1 || startButton.stops != 1 || winKey.stops != 1 ||
        stopped !=
            QStringList{QStringLiteral("win"), QStringLiteral("start"), QStringLiteral("tray")})
        return EXIT_FAILURE;

    // Neither stale notifications nor repeated shutdown may restart captures.
    host.toggleStartButtonCapture();
    host.toggleWinKeyCapture();
    host.shutdown();
    return !tray.active && !startButton.active && !winKey.active && tray.stops == 1 &&
                   startButton.stops == 1 && winKey.stops == 1
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
