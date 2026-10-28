#pragma once

#include <QString>
#include <memory>

namespace xdstarter {
enum class InstallationMode { Runtime, Install };

// Per-user, cross-session coordination. It does not replace SessionInstance IPC.
class InstallationGuard {
  public:
    virtual ~InstallationGuard() = default;
    virtual bool acquire() = 0;
    [[nodiscard]] virtual QString errorString() const = 0;
};

[[nodiscard]] std::unique_ptr<InstallationGuard>
createInstallationGuard(InstallationMode mode = InstallationMode::Runtime);
} // namespace xdstarter
