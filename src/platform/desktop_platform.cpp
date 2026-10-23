#include "desktop_platform.h"

#include <QGuiApplication>
#include <QScreen>
#include <QThread>

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

DesktopStartButtonHitResult DesktopPlatform::queryStartButtonHitTarget() {
    DesktopStartButtonHitResult result;
    result.message = QStringLiteral("此平台不提供开始按钮原生命中研究查询。");
    result.errorCode = QStringLiteral("unsupported-platform");
    return result;
}

DesktopStartButtonHitClassification
classifyStartButtonHitTarget(const DesktopStartButtonHitResult& result) {
    using Classification = DesktopStartButtonHitClassification;
    if (!result.anchor.trusted) {
        return Classification::Unidentified;
    }
    if (!result.nativeHandleRead) {
        return Classification::Unavailable;
    }
    if (!result.startWindow.handle) {
        return Classification::NoNativeWindow;
    }
    if (!result.taskbarVerifiedExplorer || !result.taskbar.handle || !result.taskbar.exists ||
        !result.taskbar.visible || !result.startWindow.exists || !result.startWindow.visible ||
        result.taskbar.className.isEmpty() || result.startWindow.className.isEmpty()) {
        return Classification::Unavailable;
    }
    if (!result.uiaProviderProcessId || result.taskbar.processId != result.uiaProviderProcessId ||
        result.taskbar.rootHandle != result.taskbar.handle ||
        result.startWindow.processId != result.uiaProviderProcessId ||
        result.startWindow.rootHandle != result.taskbar.handle) {
        return Classification::IdentityMismatch;
    }
    const QRect& button = result.anchor.startButtonPhysicalRect;
    const QRect& native = result.startWindow.physicalRect;
    const auto boundedButton = [](const QRect& rect) {
        const qint64 width = static_cast<qint64>(rect.right()) - rect.left() + 1;
        const qint64 height = static_cast<qint64>(rect.bottom()) - rect.top() + 1;
        // Keep the center distinct from all four one-pixel-inset corners.
        return width >= 5 && width <= 256 && height >= 5 && height <= 256;
    };
    if (result.startWindow.handle == result.taskbar.handle ||
        (native != button && native.contains(button))) {
        return Classification::SharedHost;
    }
    if (!result.startWindowIsTaskbarChild) {
        return Classification::IdentityMismatch;
    }
    if (!boundedButton(button) || !boundedButton(native) || native != button ||
        !result.taskbar.physicalRect.contains(button) ||
        !result.anchor.monitorPhysicalRect.contains(button)) {
        return Classification::GeometryMismatch;
    }
    if (!result.observationsStable) {
        return Classification::ChangedDuringQuery;
    }
    if (result.sampleCount != result.samples.size()) {
        return Classification::Unavailable;
    }
    const std::array<QPoint, 5> expectedPoints{
        button.center(), QPoint(button.left() + 1, button.top() + 1),
        QPoint(button.right() - 1, button.top() + 1),
        QPoint(button.left() + 1, button.bottom() - 1),
        QPoint(button.right() - 1, button.bottom() - 1)};
    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const auto& sample = result.samples[index];
        if (sample.physicalPoint != expectedPoints[index] || !sample.window.handle ||
            !sample.window.exists || !sample.window.visible || !sample.withinStartWindow ||
            !sample.withinTaskbar || sample.window.className.isEmpty() ||
            sample.window.processId != result.uiaProviderProcessId ||
            sample.window.rootHandle != result.taskbar.handle) {
            return Classification::Occluded;
        }
    }
    return Classification::IndependentCandidate;
}

QString startButtonHitClassificationName(DesktopStartButtonHitClassification value) {
    using Classification = DesktopStartButtonHitClassification;
    switch (value) {
    case Classification::Unsupported:
        return QStringLiteral("unsupported");
    case Classification::Unidentified:
        return QStringLiteral("unidentified");
    case Classification::Unavailable:
        return QStringLiteral("unavailable");
    case Classification::NoNativeWindow:
        return QStringLiteral("no-native-window");
    case Classification::SharedHost:
        return QStringLiteral("shared-host");
    case Classification::IdentityMismatch:
        return QStringLiteral("identity-mismatch");
    case Classification::GeometryMismatch:
        return QStringLiteral("geometry-mismatch");
    case Classification::Occluded:
        return QStringLiteral("occluded");
    case Classification::ChangedDuringQuery:
        return QStringLiteral("changed-during-query");
    case Classification::IndependentCandidate:
        return QStringLiteral("independent-candidate");
    }
    return QStringLiteral("unsupported");
}

TaskbarEdge desktopTaskbarEdge(const DesktopAnchorResult& anchor) {
    const auto bar = anchor.taskbarPhysicalRect;
    const auto screen = anchor.monitorPhysicalRect;
    if (!bar.isValid() || !screen.isValid())
        return TaskbarEdge::Bottom;
    if (bar.width() >= bar.height())
        return std::abs(qint64(bar.center().y()) - screen.top()) <
                       std::abs(qint64(bar.center().y()) - screen.bottom())
                   ? TaskbarEdge::Top
                   : TaskbarEdge::Bottom;
    return std::abs(qint64(bar.center().x()) - screen.left()) <
                   std::abs(qint64(bar.center().x()) - screen.right())
               ? TaskbarEdge::Left
               : TaskbarEdge::Right;
}

Qt::Edges desktopResizeEdges(const DesktopAnchorResult& anchor) {
    switch (desktopTaskbarEdge(anchor)) {
    case TaskbarEdge::Top:
        return Qt::BottomEdge | Qt::RightEdge | (anchor.centeredStart ? Qt::LeftEdge : Qt::Edges{});
    case TaskbarEdge::Left:
        return Qt::BottomEdge | Qt::RightEdge;
    case TaskbarEdge::Right:
        return Qt::BottomEdge | Qt::LeftEdge;
    default:
        return Qt::TopEdge | Qt::RightEdge | (anchor.centeredStart ? Qt::LeftEdge : Qt::Edges{});
    }
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
    switch (desktopTaskbarEdge(anchor)) {
    case TaskbarEdge::Top:
        y = top + gap;
        break;
    case TaskbarEdge::Left:
        y = top + gap;
        break;
    case TaskbarEdge::Right:
        x = right - width + 1 - gap;
        y = top + gap;
        break;
    default:
        break;
    }
    if (anchor.centeredStart && (desktopTaskbarEdge(anchor) == TaskbarEdge::Top ||
                                 desktopTaskbarEdge(anchor) == TaskbarEdge::Bottom))
        x = left + (areaWidth - width) / 2;
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

DesktopWindowMetricsResult DesktopPlatform::queryWindowMetrics(quintptr) {
    return {};
}

bool DesktopPlatform::hasOwnedForegroundWindow(quintptr) {
    return false;
}

QScreen *DesktopPlatform::screenForAnchor(const DesktopAnchorResult &anchor) {
    const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application || QThread::currentThread() != application->thread() ||
        anchor.monitorName.isEmpty())
        return nullptr;
    QScreen *match = nullptr;
    for (auto *screen : QGuiApplication::screens()) {
        if (screen->name() != anchor.monitorName)
            continue;
        if (match)
            return nullptr;
        match = screen;
    }
    return match;
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
