#include "stub_desktop_platform.h"

namespace xdstarter {

DesktopActionResult StubDesktopPlatform::openDesktopAction(const QString& actionId) {
    if (!isSupportedDesktopAction(actionId)) {
        return {false, QStringLiteral("不支持此桌面快捷操作。"),
                QStringLiteral("unsupported-action")};
    }
    return {false, QStringLiteral("此平台不提供 Windows 桌面快捷操作。"),
            QStringLiteral("unsupported-platform")};
}

DesktopAnchorResult StubDesktopPlatform::queryStartButtonAnchor() {
    DesktopAnchorResult result;
    result.message = QStringLiteral("此平台无法探测 Windows 开始按钮，将使用工作区回退位置。");
    result.errorCode = QStringLiteral("unsupported-platform");
    return result;
}

} // namespace xdstarter
