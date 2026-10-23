#include "windows_platform_integration.h"

#include <windows.h>

namespace xdstarter {

QString WindowsPlatformIntegration::displayName() const {
    return QStringLiteral("Windows 普通窗口");
}

QString WindowsPlatformIntegration::statusDescription() const {
    return QStringLiteral("开始按钮保持原生行为；Win 键唤起可在电源菜单或托盘中启用。");
}

bool WindowsPlatformIntegration::isSimulated() const { return false; }

QString WindowsPlatformIntegration::sessionKey() const {
    DWORD session = 0;
    return ProcessIdToSessionId(GetCurrentProcessId(), &session) ? QString::number(session)
                                                                 : QStringLiteral("unavailable");
}

void WindowsPlatformIntegration::allowForegroundActivation(qint64 processId) const {
    if (processId > 0 && processId < static_cast<qint64>(MAXDWORD))
        AllowSetForegroundWindow(static_cast<DWORD>(processId));
}

} // namespace xdstarter
