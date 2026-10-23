// src/platform/desktop_width_snap.cpp

#include "platform/desktop_width_snap.h"

#include "core/tile_grid.h"

#include <cmath>
#include <limits>

namespace xdstarter {

QRect desktopSnapResizeWidth(QRect initial, QRect proposed, Qt::Edges edges, int minimumOuterWidth,
                             int maximumOuterWidth, qreal renderDpr, qreal basePanelWidthDip,
                             int pitchDip, int shadowInsetDip) {
    const bool left = edges.testFlag(Qt::LeftEdge);
    const bool right = edges.testFlag(Qt::RightEdge);
    if (!initial.isValid() || !proposed.isValid() || left == right || minimumOuterWidth <= 0 ||
        maximumOuterWidth < minimumOuterWidth || !std::isfinite(renderDpr) || renderDpr <= 0 ||
        !std::isfinite(basePanelWidthDip) || basePanelWidthDip < 0 || pitchDip <= 0 ||
        shadowInsetDip < 0)
        return proposed;
    int selected = 0;
    qint64 distance = std::numeric_limits<qint64>::max();
    for (int columns = TileGrid::minimumColumns; columns <= TileGrid::maximumColumns; ++columns) {
        const double physical =
            (basePanelWidthDip + 2.0 * shadowInsetDip + double(columns) * pitchDip) * renderDpr;
        if (!std::isfinite(physical) || physical < minimumOuterWidth - 0.5 ||
            physical > double(maximumOuterWidth) + 0.499)
            continue;
        const qint64 width = qRound64(physical);
        if (width < minimumOuterWidth || width > maximumOuterWidth)
            continue;
        const qint64 difference = std::abs(width - proposed.width());
        if (difference < distance) {
            selected = int(width);
            distance = difference;
        }
    }
    if (!selected)
        return proposed;
    const qint64 x1 = left ? qint64(initial.right()) - selected + 1 : initial.left();
    const qint64 x2 = x1 + selected - 1;
    if (x1 < std::numeric_limits<int>::min() || x2 > std::numeric_limits<int>::max())
        return proposed;
    return {QPoint(int(x1), proposed.top()), QPoint(int(x2), proposed.bottom())};
}

} // namespace xdstarter
