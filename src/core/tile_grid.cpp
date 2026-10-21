#include "core/tile_grid.h"

#include "xdstarter/grid_tokens.h"

#include <cstdint>
#include <limits>

namespace xdstarter {
namespace {

struct TileSpan {
    int columns;
    int rows;
};

std::optional<TileSpan> spanFor(TileSize size) {
    switch (size) {
    case TileSize::Small:
        return TileSpan{grid_tokens::smallColumns, grid_tokens::smallRows};
    case TileSize::Standard:
        return TileSpan{grid_tokens::standardColumns, grid_tokens::standardRows};
    case TileSize::Wide:
        return TileSpan{grid_tokens::wideColumns, grid_tokens::wideRows};
    }
    return std::nullopt;
}

constexpr std::int64_t pitch = std::int64_t{grid_tokens::cell} + grid_tokens::gap;

constexpr std::int64_t extent(int span) {
    return std::int64_t{span} * grid_tokens::cell + std::int64_t{span - 1} * grid_tokens::gap;
}

} // namespace

std::optional<TileRect> TileGrid::rectangle(int column, int row, TileSize size) const {
    const auto span = spanFor(size);
    if (!span || column < 0 || row < 0 || column >= grid_tokens::columns ||
        span->columns > grid_tokens::columns - column) {
        return std::nullopt;
    }

    const std::int64_t x = std::int64_t{column} * pitch;
    const std::int64_t y = std::int64_t{row} * pitch;
    const std::int64_t tileWidth = extent(span->columns);
    const std::int64_t tileHeight = extent(span->rows);
    constexpr auto maximum = std::numeric_limits<int>::max();
    if (x > maximum - tileWidth || y > maximum - tileHeight) {
        return std::nullopt;
    }

    return TileRect{static_cast<int>(x), static_cast<int>(y), static_cast<int>(tileWidth),
                    static_cast<int>(tileHeight)};
}

int TileGrid::width() const { return static_cast<int>(extent(grid_tokens::columns)); }

} // namespace xdstarter
