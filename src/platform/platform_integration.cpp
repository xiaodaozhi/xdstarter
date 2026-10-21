#include "platform_integration.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include "windows/windows_platform_integration.h"
#else
#include "stub/stub_platform_integration.h"
#endif

namespace xdstarter {

std::unique_ptr<PlatformIntegration> createPlatformIntegration() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsPlatformIntegration>();
#else
    return std::make_unique<StubPlatformIntegration>();
#endif
}

} // namespace xdstarter
