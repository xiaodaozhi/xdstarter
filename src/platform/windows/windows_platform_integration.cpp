#include "windows_platform_integration.h"

namespace xdstarter {

QString WindowsPlatformIntegration::displayName() const {
    return QStringLiteral("Windows 普通窗口");
}

QString WindowsPlatformIntegration::statusDescription() const {
    return QStringLiteral("尚未接管 Win 键或开始按钮；系统开始菜单保持原有行为。");
}

bool WindowsPlatformIntegration::isSimulated() const { return false; }

} // namespace xdstarter
