#include "desktop_platform.h"

#include <algorithm>
#include <limits>

#ifdef Q_OS_WIN
#include "windows/windows_desktop_platform.h"
#else
#include "stub/stub_desktop_platform.h"
#endif

namespace xdstarter {

bool isSupportedDesktopAction(const QString& actionId) {
    return actionId == QStringLiteral("documents") || actionId == QStringLiteral("downloads") ||
           actionId == QStringLiteral("settings") || actionId == QStringLiteral("account");
}

DesktopAnchorResult DesktopPlatform::queryMenuAnchor() {
    return queryStartButtonAnchor();
}

QRect desktopPopupPhysicalRect(const QSize& windowPhysicalSize, const DesktopAnchorResult& anchor,
                               int gapPhysicalPixels) {
    const QRect area = anchor.workAreaPhysicalRect;
    if (!area.isValid() || windowPhysicalSize.width() <= 0 || windowPhysicalSize.height() <= 0) {
        return {};
    }
    // Valid QRect endpoints can still describe a span wider than int. Keep
    // width, center and placement arithmetic in the wider coordinate domain.
    const qint64 left = area.left();
    const qint64 top = area.top();
    const qint64 right = area.right();
    const qint64 bottom = area.bottom();
    const qint64 areaWidth = right - left + 1;
    const qint64 areaHeight = bottom - top + 1;
    if (areaWidth > std::numeric_limits<int>::max() ||
        areaHeight > std::numeric_limits<int>::max()) {
        return {};
    }
    const qint64 gap = std::clamp(gapPhysicalPixels, 0, 128);
    const qint64 width = std::min<qint64>(windowPhysicalSize.width(), areaWidth);
    const qint64 height = std::min<qint64>(windowPhysicalSize.height(), areaHeight);
    qint64 x = left + gap;
    qint64 y = bottom - height + 1 - gap;
    // UI Automation may transiently fail while Explorer is animating. The
    // button's trust state must never change placement on the same work area.
    x = std::clamp(x, left, right - width + 1);
    y = std::clamp(y, top, bottom - height + 1);
    // The endpoint constructor avoids x + width - 1 overflowing at INT_MAX.
    return {QPoint(static_cast<int>(x), static_cast<int>(y)),
            QPoint(static_cast<int>(x + width - 1), static_cast<int>(y + height - 1))};
}

DesktopAppearanceResult DesktopPlatform::configureMenuWindow(quintptr) {
    return {false,
            false,
            QStringLiteral("此平台不配置 Windows 原生装饰，菜单使用自绘外观。"),
            QStringLiteral("unsupported-platform")};
}

DesktopPlacementResult DesktopPlatform::placeWindow(quintptr, const DesktopAnchorResult&, int,
                                                    bool, int) {
    return {false,
            {},
            QStringLiteral("此平台不提供 Windows 原生窗口定位。"),
            QStringLiteral("unsupported-platform")};
}

std::unique_ptr<DesktopPlatform> createDesktopPlatform() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsDesktopPlatform>();
#else
    return std::make_unique<StubDesktopPlatform>();
#endif
}

} // namespace xdstarter
