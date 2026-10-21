#include "stub_application_platform.h"

namespace xdstarter {

CatalogSnapshot StubApplicationPlatform::scanApplications() {
    return {{}, {QStringLiteral("当前平台不提供 Windows 应用目录。")}, true};
}

QImage StubApplicationPlatform::loadIcon(const LaunchTarget&, int) { return {}; }

LaunchResult StubApplicationPlatform::launch(const LaunchTarget&) {
    return {false, QStringLiteral("当前平台不支持 Windows 应用启动。"),
            QStringLiteral("unsupported-platform")};
}

LaunchResult StubApplicationPlatform::checkAvailability(const LaunchTarget& target) {
    return launch(target);
}

} // namespace xdstarter
