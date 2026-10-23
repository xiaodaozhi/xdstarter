// tests/platform/desktop_width_snap_tests.cpp

#include "platform/desktop_width_snap.h"

#include <iostream>
#include <limits>

int main() {
    using namespace xdstarter;
    int failures = 0;
    const auto expect = [&](bool valid, const char* message) {
        if (!valid) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };
    int combinations = 0;
    for (const qreal scale : {1.0, 1.25, 1.5, 1.75, 2.0}) {
        for (const QPoint origin : {QPoint(12, 24), QPoint(-2400, -1600)}) {
            for (const Qt::Edges edges : {Qt::Edges(Qt::LeftEdge), Qt::Edges(Qt::RightEdge),
                                          Qt::Edges(Qt::LeftEdge | Qt::TopEdge),
                                          Qt::Edges(Qt::RightEdge | Qt::BottomEdge)}) {
                for (const int columns : {4, 10, 11, 32, 64}) {
                    const int width = qRound((368 + 48 + columns * 56) * scale);
                    const QRect initial(origin, QSize(width, 800));
                    QRect proposed = initial;
                    if (edges.testFlag(Qt::LeftEdge))
                        proposed.setLeft(initial.left() - 3);
                    else
                        proposed.setRight(initial.right() + 3);
                    const auto snapped = desktopSnapResizeWidth(initial, proposed, edges, 1, 20000,
                                                                scale, 368, 56, 24);
                    expect(snapped == initial,
                           "nearest shared-grid width preserves the fixed edge");
                    ++combinations;
                }
            }
        }
    }
    const QRect initial(12, 24, 1000, 800);
    expect(desktopSnapResizeWidth(initial, initial, Qt::TopEdge, 1, 2000, 1, 368, 56, 24) ==
               initial,
           "vertical resizing does not change width");
    auto proposed = initial;
    proposed.setWidth(1004);
    expect(desktopSnapResizeWidth(initial, proposed, Qt::RightEdge, 528, 1004, 1, 368, 56, 24)
                   .width() == 976,
           "maximum boundary takes priority over a larger nearby snap width");
    proposed.setWidth(1004);
    expect(desktopSnapResizeWidth(initial, proposed, Qt::RightEdge, 528, 2000, 1, 368, 56, 24)
                   .width() == 976,
           "an exact midpoint selects the smaller candidate");
    expect(desktopSnapResizeWidth(initial, initial, Qt::RightEdge, 990, 1000, 1, 368, 56, 24) ==
               initial,
           "no representable grid width keeps the already bounded proposal");
    for (const qreal invalid : {0.0, -1.0, std::numeric_limits<qreal>::infinity(),
                                std::numeric_limits<qreal>::quiet_NaN()})
        expect(desktopSnapResizeWidth(initial, initial, Qt::RightEdge, 1, 2000, invalid, 368, 56,
                                      24) == initial,
               "invalid render scale leaves the proposal unchanged");
    std::cout << combinations << " width snap combinations, " << failures << " failures.\n";
    return failures ? 1 : 0;
}
