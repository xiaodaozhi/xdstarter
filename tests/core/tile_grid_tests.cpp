#include "core/tile_grid.h"

#include "xdstarter/grid_tokens.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

class TestContext {
  public:
    void expect(bool condition, std::string_view description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }

    int failures = 0;
};

void expectRectangle(TestContext& test, const std::optional<xdstarter::TileRect>& actual,
                     xdstarter::TileRect expected, std::string_view description) {
    test.expect(actual.has_value(), description);
    if (actual) {
        test.expect(actual->x == expected.x && actual->y == expected.y &&
                        actual->width == expected.width && actual->height == expected.height,
                    description);
    }
}

} // namespace

int main() {
    using namespace xdstarter;
    constexpr int cell = grid_tokens::cell;
    constexpr int gap = grid_tokens::gap;
    constexpr int pitch = cell + gap;
    constexpr int standardWidth = 2 * cell + gap;
    constexpr int wideWidth = 4 * cell + 3 * gap;
    const TileGrid grid;
    TestContext test;

    expectRectangle(test, grid.rectangle(0, 0, TileSize::Small), {0, 0, cell, cell},
                    "small tile occupies one cell");
    expectRectangle(test, grid.rectangle(2, 3, TileSize::Standard),
                    {2 * pitch, 3 * pitch, standardWidth, standardWidth},
                    "standard tile uses the shared pitch and a square two-cell extent");
    expectRectangle(test, grid.rectangle(4, 1, TileSize::Wide),
                    {4 * pitch, pitch, wideWidth, standardWidth},
                    "wide tile occupies four columns and two rows");
    test.expect(grid.width() == 10 * cell + 9 * gap, "group width includes nine internal gaps");
    for (int columns = TileGrid::minimumColumns; columns <= TileGrid::maximumColumns; ++columns) {
        test.expect(grid.width(columns) == columns * pitch - gap,
                    "dynamic columns preserve the shared extent");
        test.expect(TileGrid::columnsForWidth(grid.width(columns)) == columns &&
                        TileGrid::columnsForWidth(grid.width(columns) + pitch - 0.01) == columns,
                    "only a complete additional column increases the width budget");
        test.expect(grid.rectangle(columns - 4, 2, TileSize::Wide, columns).has_value() &&
                        !grid.rectangle(columns - 3, 2, TileSize::Wide, columns),
                    "wide tile boundary follows each active column budget");
    }
    for (const int columns : {-1, 0, 3, 65, std::numeric_limits<int>::max()})
        test.expect(!grid.rectangle(0, 0, TileSize::Small, columns) && grid.width(columns) == 0,
                    "unsupported dynamic column counts are rejected");
    test.expect(TileGrid::columnsForWidth(std::numeric_limits<double>::infinity()) ==
                        TileGrid::minimumColumns &&
                    TileGrid::columnsForWidth(-100) == TileGrid::minimumColumns &&
                    TileGrid::columnsForWidth(1e20) == TileGrid::maximumColumns,
                "viewport column budgets are finite and bounded");

    const auto smallTopLeft = grid.rectangle(0, 0, TileSize::Small);
    const auto smallBottomRight = grid.rectangle(1, 1, TileSize::Small);
    const auto standard = grid.rectangle(0, 0, TileSize::Standard);
    const auto secondStandard = grid.rectangle(2, 0, TileSize::Standard);
    const auto wide = grid.rectangle(0, 0, TileSize::Wide);
    test.expect(smallTopLeft && smallBottomRight && standard && secondStandard && wide,
                "alignment fixtures are valid");
    if (smallTopLeft && smallBottomRight && standard && secondStandard && wide) {
        test.expect(smallBottomRight->x - smallTopLeft->width == gap &&
                        smallBottomRight->y - smallTopLeft->height == gap,
                    "adjacent small tiles preserve the shared gap");
        test.expect(smallBottomRight->x + smallBottomRight->width == standard->width &&
                        smallBottomRight->y + smallBottomRight->height == standard->height,
                    "four small tiles share the standard tile outer boundary");
        test.expect(secondStandard->x - standard->width == gap &&
                        secondStandard->x + secondStandard->width == wide->width &&
                        secondStandard->height == wide->height,
                    "two standard tiles share the wide tile outer boundary");
    }

    struct SizeCase {
        TileSize size;
        int columns;
    };
    constexpr std::array sizeCases{SizeCase{TileSize::Small, 1}, SizeCase{TileSize::Standard, 2},
                                   SizeCase{TileSize::Wide, 4}};
    for (const auto& sizeCase : sizeCases) {
        const auto last = grid.rectangle(grid_tokens::columns - sizeCase.columns, 0, sizeCase.size);
        test.expect(last && last->x + last->width == grid.width(),
                    "tile may end exactly at the group boundary");
        test.expect(!grid.rectangle(grid_tokens::columns - sizeCase.columns + 1, 0, sizeCase.size),
                    "tile cannot extend beyond the group boundary");
        test.expect(!grid.rectangle(-1, 0, sizeCase.size), "negative column is rejected");
        test.expect(!grid.rectangle(0, -1, sizeCase.size), "negative row is rejected");
        test.expect(!grid.rectangle(std::numeric_limits<int>::max(), 0, sizeCase.size),
                    "extreme column is rejected before coordinate multiplication");
        test.expect(!grid.rectangle(0, std::numeric_limits<int>::max(), sizeCase.size),
                    "overflowing row is rejected");
    }
    test.expect(!grid.rectangle(0, 0, static_cast<TileSize>(-1)),
                "negative enum value is rejected");
    test.expect(!grid.rectangle(0, 0, static_cast<TileSize>(3)), "unknown enum value is rejected");
    test.expect(!grid.rectangle(std::numeric_limits<int>::min(), 0, TileSize::Small),
                "minimum integer column is rejected");
    test.expect(!grid.rectangle(0, std::numeric_limits<int>::min(), TileSize::Small),
                "minimum integer row is rejected");

    constexpr auto maximum = std::numeric_limits<int>::max();
    constexpr int lastSmallRow = (maximum - cell) / pitch;
    const auto lastSmall = grid.rectangle(0, lastSmallRow, TileSize::Small);
    test.expect(lastSmall && std::int64_t{lastSmall->y} + lastSmall->height <= maximum,
                "last representable small tile row is accepted");
    test.expect(!grid.rectangle(0, lastSmallRow + 1, TileSize::Small),
                "small tile bottom edge cannot overflow even when its origin fits");
    constexpr int lastStandardRow = (maximum - standardWidth) / pitch;
    test.expect(grid.rectangle(0, lastStandardRow, TileSize::Standard).has_value(),
                "last representable two-row tile is accepted");
    test.expect(!grid.rectangle(0, lastStandardRow + 1, TileSize::Standard),
                "two-row tile bottom edge cannot overflow");

    if (test.failures == 0) {
        std::cout << "All tile grid checks passed.\n";
    }
    return test.failures == 0 ? 0 : 1;
}
