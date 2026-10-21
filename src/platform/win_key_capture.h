// src/platform/win_key_capture.h

#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace xdstarter {

// GUI-thread owned. Construction never installs a global input hook. Native
// input callbacks only queue a toggle; window/focus policy belongs to the host.
class WinKeyCapture : public QObject {
    Q_OBJECT

  public:
    explicit WinKeyCapture(QObject* parent = nullptr) : QObject(parent) {}
    ~WinKeyCapture() override = default;

    virtual void setEnabled(bool enabled) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isActive() const = 0;
    [[nodiscard]] virtual bool isSupported() const = 0;

  signals:
    void toggleRequested();
    void statusChanged(bool active, QString message, QString errorCode);

  protected:
    // A stopped/replaced session cannot deliver already queued native events.
    [[nodiscard]] quint64 beginEventSession();
    [[nodiscard]] quint64 endEventSession();
    [[nodiscard]] quint64 eventGeneration() const;
    [[nodiscard]] bool isCurrentEventSession(quint64 generation) const;
    void queueToggleRequested(quint64 generation);
    void queueStatusChanged(quint64 generation, bool active, QString message, QString errorCode);

  private:
    std::atomic<quint64> generation_{0};
    std::atomic_bool sessionEnabled_{false};
};

[[nodiscard]] std::unique_ptr<WinKeyCapture> createWinKeyCapture();

} // namespace xdstarter
