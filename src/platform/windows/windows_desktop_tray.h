#pragma once

#include "platform/desktop_tray.h"

namespace xdstarter {

class WindowsDesktopTray final : public DesktopTray {
  public:
    explicit WindowsDesktopTray(QObject* parent = nullptr);
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
