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
    if (!screen.isValid())
        return TaskbarEdge::Bottom;
    if (!bar.isValid()) {
        // An auto-hide taskbar can be absent during the query. Only infer an
        // edge when the reserved work area identifies one unambiguous strip.
        const QRect area = anchor.workAreaPhysicalRect;
        if (area.isValid() && screen.contains(area)) {
            const std::array<qint64, 4> reserved{
                qint64(screen.bottom()) - area.bottom(), qint64(area.top()) - screen.top(),
                qint64(area.left()) - screen.left(), qint64(screen.right()) - area.right()};
            int found = -1;
            for (int index = 0; index < 4; ++index) {
                if (reserved[index] > 0) {
                    if (found >= 0)
                        return TaskbarEdge::Bottom;
                    found = index;
                }
            }
            if (found >= 0)
                return static_cast<TaskbarEdge>(found);
        }
        return TaskbarEdge::Bottom;
    }
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
        return Qt::BottomEdge | Qt::RightEdge | (anchor.centeredStart ? Qt::TopEdge : Qt::Edges{});
    case TaskbarEdge::Right:
        return Qt::BottomEdge | Qt::LeftEdge | (anchor.centeredStart ? Qt::TopEdge : Qt::Edges{});
    default:
        return Qt::TopEdge | Qt::RightEdge | (anchor.centeredStart ? Qt::LeftEdge : Qt::Edges{});
    }
}

QRect desktopResizePhysicalRect(QRect initial, QRect proposed, Qt::Edges edges,
                                QSize minimumOuterSize, QRect allowedOuterBounds) {
    if (!initial.isValid() || !proposed.isValid() || minimumOuterSize.isEmpty())
        return {};
    if (allowedOuterBounds.isValid() &&
        (qint64(allowedOuterBounds.right()) - allowedOuterBounds.left() + 1 >
             std::numeric_limits<int>::max() ||
         qint64(allowedOuterBounds.bottom()) - allowedOuterBounds.top() + 1 >
             std::numeric_limits<int>::max()))
        return initial;
    if (allowedOuterBounds.isValid() && !allowedOuterBounds.contains(initial)) {
        const qint64 width = std::min<qint64>(qint64(initial.right()) - initial.left() + 1,
                                              allowedOuterBounds.width());
        const qint64 height = std::min<qint64>(qint64(initial.bottom()) - initial.top() + 1,
                                               allowedOuterBounds.height());
        const qint64 x = std::clamp<qint64>(initial.left(), allowedOuterBounds.left(),
                                            qint64(allowedOuterBounds.right()) - width + 1);
        const qint64 y = std::clamp<qint64>(initial.top(), allowedOuterBounds.top(),
                                            qint64(allowedOuterBounds.bottom()) - height + 1);
        initial = {QPoint(static_cast<int>(x), static_cast<int>(y)),
                   QPoint(static_cast<int>(x + width - 1), static_cast<int>(y + height - 1))};
    }
    const bool left = edges.testFlag(Qt::LeftEdge), right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge), bottom = edges.testFlag(Qt::BottomEdge);
    if ((!left && !right && !top && !bottom) || (left && right) || (top && bottom))
        return initial;
    qint64 x1 = left ? proposed.left() : initial.left();
    qint64 x2 = right ? proposed.right() : initial.right();
    qint64 y1 = top ? proposed.top() : initial.top();
    qint64 y2 = bottom ? proposed.bottom() : initial.bottom();
    if (allowedOuterBounds.isValid()) {
        x1 = std::max<qint64>(x1, allowedOuterBounds.left());
        x2 = std::min<qint64>(x2, allowedOuterBounds.right());
        y1 = std::max<qint64>(y1, allowedOuterBounds.top());
        y2 = std::min<qint64>(y2, allowedOuterBounds.bottom());
        minimumOuterSize.setWidth(static_cast<int>(std::min<qint64>(
            minimumOuterSize.width(),
            left ? x2 - allowedOuterBounds.left() + 1 : allowedOuterBounds.right() - x1 + 1)));
        minimumOuterSize.setHeight(static_cast<int>(std::min<qint64>(
            minimumOuterSize.height(),
            top ? y2 - allowedOuterBounds.top() + 1 : allowedOuterBounds.bottom() - y1 + 1)));
        if (minimumOuterSize.isEmpty())
            return initial;
    }
    if ((left || right) && x2 - x1 + 1 < minimumOuterSize.width()) {
        if (left)
            x1 = x2 - minimumOuterSize.width() + 1;
        else
            x2 = x1 + minimumOuterSize.width() - 1;
    }
    if ((top || bottom) && y2 - y1 + 1 < minimumOuterSize.height()) {
        if (top)
            y1 = y2 - minimumOuterSize.height() + 1;
        else
            y2 = y1 + minimumOuterSize.height() - 1;
    }
    if (x1 < std::numeric_limits<int>::min() || y1 < std::numeric_limits<int>::min() ||
        x2 > std::numeric_limits<int>::max() || y2 > std::numeric_limits<int>::max() ||
        x2 - x1 + 1 > std::numeric_limits<int>::max() ||
        y2 - y1 + 1 > std::numeric_limits<int>::max())
        return initial;
    return {QPoint(static_cast<int>(x1), static_cast<int>(y1)),
            QPoint(static_cast<int>(x2), static_cast<int>(y2))};
}

QRect desktopMenuWorkArea(const DesktopAnchorResult& anchor) {
    QRect area = anchor.workAreaPhysicalRect;
    const QRect overlap = area.intersected(anchor.taskbarPhysicalRect);
    if (!area.isValid() || !overlap.isValid())
        return area;
    switch (desktopTaskbarEdge(anchor)) {
    case TaskbarEdge::Bottom:
        if (overlap.bottom() == area.bottom()) {
            if (overlap.top() <= area.top())
                return {};
            area.setBottom(overlap.top() - 1);
        }
        break;
    case TaskbarEdge::Top:
        if (overlap.top() == area.top()) {
            if (overlap.bottom() >= area.bottom())
                return {};
            area.setTop(overlap.bottom() + 1);
        }
        break;
    case TaskbarEdge::Left:
        if (overlap.left() == area.left()) {
            if (overlap.right() >= area.right())
                return {};
            area.setLeft(overlap.right() + 1);
        }
        break;
    case TaskbarEdge::Right:
        if (overlap.right() == area.right()) {
            if (overlap.left() <= area.left())
                return {};
            area.setRight(overlap.left() - 1);
        }
        break;
    }
    return area;
}

QRect desktopPopupPhysicalRect(const QSize& windowPhysicalSize, const DesktopAnchorResult& anchor,
                               int gapPhysicalPixels) {
    const QRect area = desktopMenuPanelBounds(anchor, gapPhysicalPixels);
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
    const qint64 width = std::min<qint64>(windowPhysicalSize.width(), areaWidth);
    const qint64 height = std::min<qint64>(windowPhysicalSize.height(), areaHeight);
    qint64 x = left;
    qint64 y = bottom - height + 1;
    switch (desktopTaskbarEdge(anchor)) {
    case TaskbarEdge::Top:
        y = top;
        break;
    case TaskbarEdge::Left:
        y = top;
        break;
    case TaskbarEdge::Right:
        x = right - width + 1;
        y = top;
        break;
    default:
        break;
    }
    if (anchor.centeredStart && (desktopTaskbarEdge(anchor) == TaskbarEdge::Top ||
                                 desktopTaskbarEdge(anchor) == TaskbarEdge::Bottom))
        x = left + (areaWidth - width) / 2;
    else if (anchor.centeredStart)
        y = top + (areaHeight - height) / 2;
    x = std::clamp(x, left, right - width + 1);
    y = std::clamp(y, top, bottom - height + 1);
    // The endpoint constructor avoids x + width - 1 overflowing at INT_MAX.
    return {QPoint(static_cast<int>(x), static_cast<int>(y)),
            QPoint(static_cast<int>(x + width - 1), static_cast<int>(y + height - 1))};
}

QRect desktopMenuPanelBounds(const DesktopAnchorResult& anchor, int gapPhysicalPixels) {
    const QRect area = desktopMenuWorkArea(anchor);
    const qint64 gap = std::clamp(gapPhysicalPixels, 0, 128);
    const qint64 width = qint64(area.right()) - area.left() + 1;
    const qint64 height = qint64(area.bottom()) - area.top() + 1;
    if (!area.isValid() || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max() || width <= 2 * gap || height <= 2 * gap)
        return {};
    return {QPoint(static_cast<int>(qint64(area.left()) + gap),
                   static_cast<int>(qint64(area.top()) + gap)),
            QPoint(static_cast<int>(qint64(area.right()) - gap),
                   static_cast<int>(qint64(area.bottom()) - gap))};
}

QRegion desktopMenuInputRegion(QRect outerPhysical, QRect taskbarPhysical) {
    const qint64 width = qint64(outerPhysical.right()) - outerPhysical.left() + 1;
    const qint64 height = qint64(outerPhysical.bottom()) - outerPhysical.top() + 1;
    if (!outerPhysical.isValid() || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max())
        return {};
    const QRegion full(0, 0, static_cast<int>(width), static_cast<int>(height));
    if (!taskbarPhysical.isValid())
        return full;
    // QRect's generic intersection also normalizes inverted rectangles using
    // left()-1/top()-1. Those intermediates overflow at INT_MIN even for valid
    // small rectangles. Intersect inclusive endpoints in the wider domain and
    // construct only the bounded, nonnegative local region.
    const qint64 left = std::max(qint64(outerPhysical.left()), qint64(taskbarPhysical.left()));
    const qint64 top = std::max(qint64(outerPhysical.top()), qint64(taskbarPhysical.top()));
    const qint64 right = std::min(qint64(outerPhysical.right()), qint64(taskbarPhysical.right()));
    const qint64 bottom =
        std::min(qint64(outerPhysical.bottom()), qint64(taskbarPhysical.bottom()));
    if (left > right || top > bottom)
        return full;
    return full.subtracted(QRegion(
        static_cast<int>(left - outerPhysical.left()), static_cast<int>(top - outerPhysical.top()),
        static_cast<int>(right - left + 1), static_cast<int>(bottom - top + 1)));
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

bool DesktopPlatform::hasOwnedForegroundDialog(quintptr) { return false; }

DesktopPresentationResult DesktopPlatform::presentMenuWindow(quintptr) { return {}; }

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
