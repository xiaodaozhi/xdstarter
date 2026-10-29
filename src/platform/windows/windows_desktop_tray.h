#pragma once

#include "platform/desktop_tray.h"

namespace xdstarter {

class WindowsDesktopTray final : public DesktopTray {
  public:
    // The optional operation is used by owned-window tests; production always
    // delegates to Shell_NotifyIconW when none is supplied.
    using NotificationOperation = std::function<bool(quint32 command, void* data)>;
    explicit WindowsDesktopTray(QObject* parent = nullptr, NotificationOperation operation = {});
    ~WindowsDesktopTray() override;

    [[nodiscard]] DesktopActionResult start(const QImage& icon) override;
    void stop() override;
    [[nodiscard]] bool isAvailable() const override;
    void showContextMenu() override;
    void setWinKeyCaptureState(bool active, bool canEnable, const QString& status) override;
    void setStartButtonCaptureState(bool active, bool canEnable, const QString& status) override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xdstarter
