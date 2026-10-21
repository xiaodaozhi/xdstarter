#include "stub_desktop_tray.h"

namespace xdstarter {

DesktopActionResult StubDesktopTray::start(const QImage&) {
    return {false, QStringLiteral("此平台不提供 Windows 托盘恢复入口。"),
            QStringLiteral("unsupported-platform")};
}

} // namespace xdstarter
