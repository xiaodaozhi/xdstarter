#pragma once

#include "desktop_platform.h"

#include <QImage>
#include <QObject>
#include <QPoint>

#include <functional>
#include <memory>

namespace xdstarter {

// GUI-thread owned. The native message callback only queues these signals;
// Shell actions, application state and QML remain outside the callback.
class DesktopTray : public QObject {
    Q_OBJECT

  public:
    explicit DesktopTray(QObject* parent = nullptr) : QObject(parent) {}
    ~DesktopTray() override = default;

    [[nodiscard]] virtual DesktopActionResult start(const QImage& icon) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isAvailable() const = 0;
    virtual void showContextMenu() = 0;

  signals:
    void toggleRequested();
    void menuRequested(QPoint physicalPosition);
    void quitRequested();
    void registrationChanged(bool available, QString message);

  protected:
    // Native notifications may already be queued when the host is stopped or
    // restarted. Every queued callback belongs to exactly one live session.
    void beginEventSession();
    void endEventSession();
    [[nodiscard]] quint64 eventSessionGeneration() const { return eventGeneration_; }
    [[nodiscard]] bool isCurrentEventSession(quint64 generation) const;
    void queueSessionCallback(std::function<void()> callback);

  private:
    quint64 eventGeneration_ = 0;
    bool sessionActive_ = false;
};

[[nodiscard]] std::unique_ptr<DesktopTray> createDesktopTray();

} // namespace xdstarter
