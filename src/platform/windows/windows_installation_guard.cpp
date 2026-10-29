#include "platform/installation_guard.h"

#include <windows.h>

#include <sddl.h>
#include <vector>

namespace xdstarter {
namespace {
QString currentSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    const bool valid = size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!valid)
        return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text))
        return {};
    const QString sid = QString::fromWCharArray(text);
    LocalFree(text);
    return sid;
}

class WindowsInstallationGuard final : public InstallationGuard {
  public:
    explicit WindowsInstallationGuard(InstallationMode mode) : mode_(mode) {}
    ~WindowsInstallationGuard() override {
        if (running_)
            CloseHandle(running_);
        if (gate_) {
            ReleaseMutex(gate_);
            CloseHandle(gate_);
        }
    }
    bool acquire() override {
        if (running_ || gate_)
            return true;
        const QString sid = currentSid();
        if (sid.isEmpty())
            return fail(QStringLiteral("无法确定当前用户，未启动或修改安装。"));
        const QString prefix = QStringLiteral("Global\\XDStarter.") + sid;
        HANDLE gate =
            CreateMutexW(nullptr, FALSE,
                         reinterpret_cast<LPCWSTR>((prefix + QStringLiteral(".install")).utf16()));
        if (!gate)
            return fail(QStringLiteral("无法取得安装协调锁，未启动或修改安装。"));
        const DWORD wait = WaitForSingleObject(gate, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(gate);
            return fail(QStringLiteral("XDStarter 正在安装或卸载，请稍后重试。"));
        }
        const QString runningName = prefix + QStringLiteral(".running");
        if (mode_ == InstallationMode::Install) {
            HANDLE existing =
                OpenMutexW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(runningName.utf16()));
            const DWORD error = GetLastError();
            if (existing || error != ERROR_FILE_NOT_FOUND) {
                if (existing)
                    CloseHandle(existing);
                ReleaseMutex(gate);
                CloseHandle(gate);
                return fail(
                    QStringLiteral("请先从托盘正常退出所有 XDStarter 实例，再安装或卸载。"));
            }
            gate_ = gate;
            return true;
        }
        running_ = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(runningName.utf16()));
        ReleaseMutex(gate);
        CloseHandle(gate);
        if (!running_)
            return fail(QStringLiteral("无法登记运行状态，未启动菜单或接管输入。"));
        return true;
    }
    QString errorString() const override { return error_; }

  private:
    bool fail(const QString& error) {
        error_ = error;
        return false;
    }
    InstallationMode mode_;
    HANDLE running_ = nullptr;
    HANDLE gate_ = nullptr;
    QString error_;
};
} // namespace
std::unique_ptr<InstallationGuard> createInstallationGuard(InstallationMode mode) {
    return std::make_unique<WindowsInstallationGuard>(mode);
}
} // namespace xdstarter
