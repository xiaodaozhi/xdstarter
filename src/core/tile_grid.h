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
    [[nodiscard]] std::optional<TileRect> rectangle(int column, int row, TileSize size) const;
    [[nodiscard]] int width() const;
};

} // namespace xdstarter
