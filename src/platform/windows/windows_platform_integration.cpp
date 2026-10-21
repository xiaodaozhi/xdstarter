#include "windows_platform_integration.h"

namespace xdstarter {

QString WindowsPlatformIntegration::displayName() const {
    return QStringLiteral("Windows 普通窗口");
}

QString WindowsPlatformIntegration::statusDescription() const {
    return QStringLiteral("开始按钮保持原生行为；Win 键唤起可在电源菜单或托盘中启用。");
}

bool WindowsPlatformIntegration::isSimulated() const { return false; }

} // namespace xdstarter
