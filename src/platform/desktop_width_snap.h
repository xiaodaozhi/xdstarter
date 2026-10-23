// src/platform/desktop_width_snap.h

#pragma once

#include <QRect>
#include <Qt>

namespace xdstarter {

[[nodiscard]] QRect desktopSnapResizeWidth(QRect initial, QRect proposed, Qt::Edges edges,
                                           int minimumOuterWidth, int maximumOuterWidth,
                                           qreal renderDpr, qreal basePanelWidthDip, int pitchDip,
                                           int shadowInsetDip);

} // namespace xdstarter
