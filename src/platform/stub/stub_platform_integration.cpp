#include "stub_platform_integration.h"

namespace xdstarter {

QString StubPlatformIntegration::displayName() const { return QStringLiteral("通用平台模拟"); }

QString StubPlatformIntegration::statusDescription() const {
    return QStringLiteral("当前使用模拟平台，仅供界面开发；不代表 Windows 系统集成或兼容性通过。");
}

bool StubPlatformIntegration::isSimulated() const { return true; }

} // namespace xdstarter
