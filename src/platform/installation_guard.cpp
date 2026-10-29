#include "installation_guard.h"

#ifndef Q_OS_WIN
namespace xdstarter {
namespace {
class StubInstallationGuard final : public InstallationGuard {
  public:
    bool acquire() override { return true; }
    QString errorString() const override { return {}; }
};
} // namespace
std::unique_ptr<InstallationGuard> createInstallationGuard(InstallationMode) {
    return std::make_unique<StubInstallationGuard>();
}
} // namespace xdstarter
#endif
