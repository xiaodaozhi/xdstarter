#pragma once

#include <optional>

namespace xdstarter {

enum class TileSize { Small, Standard, Wide };

struct TileRect {
    int x;
    int y;
    int width;
    int height;
};

class TileGrid {
  public:
    static constexpr int minimumColumns = 4;
    static constexpr int maximumColumns = 64;
    [[nodiscard]] std::optional<TileRect> rectangle(int column, int row, TileSize size) const;
    [[nodiscard]] std::optional<TileRect> rectangle(int column, int row, TileSize size,
                                                    int columns) const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int width(int columns) const;
    [[nodiscard]] static int columnsForWidth(double availableWidth);
};

} // namespace xdstarter
