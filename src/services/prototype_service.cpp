#include "prototype_service.h"

#include "xdstarter/grid_tokens.h"

#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace xdstarter {
namespace {

enum class Direction { Left, Right, Up, Down };

LayoutResult validateGroupName(const QString& name) {
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return {false, QStringLiteral("请输入分组名称")};
    }
    if (trimmed.size() > LayoutStore::maximumGroupNameLength) {
        return {false, QStringLiteral("分组名称不能超过 128 个字符")};
    }
    for (const auto character : name) {
        if (character.category() == QChar::Other_Control ||
            character.category() == QChar::Separator_Line ||
            character.category() == QChar::Separator_Paragraph) {
            return {false, QStringLiteral("分组名称不能包含控制字符或换行")};
        }
    }
    return {true, {}};
}

std::optional<Direction> parseDirection(const QString& direction) {
    if (direction == QLatin1String("left")) {
        return Direction::Left;
    }
    if (direction == QLatin1String("right")) {
        return Direction::Right;
    }
    if (direction == QLatin1String("up")) {
        return Direction::Up;
    }
    if (direction == QLatin1String("down")) {
        return Direction::Down;
    }
    return std::nullopt;
}

std::int64_t centerX(const TileRect& rectangle) {
    return 2 * std::int64_t{rectangle.x} + rectangle.width;
}

std::int64_t centerY(const TileRect& rectangle) {
    return 2 * std::int64_t{rectangle.y} + rectangle.height;
}

bool overlaps(int start, int extent, int otherStart, int otherExtent) {
    return start < otherStart + otherExtent && otherStart < start + extent;
}

const PreviewTile* neighborInGroup(const PreviewTile& current, const PreviewGroup& group,
                                   Direction direction) {
    const bool horizontal = direction == Direction::Left || direction == Direction::Right;
    const PreviewTile* best = nullptr;
    bool bestOverlaps = false;
    std::int64_t bestDistance = 0;
    for (const auto& candidate : group.tiles) {
        if (candidate.id == current.id) {
            continue;
        }
        const auto dx = centerX(candidate.rectangle) - centerX(current.rectangle);
        const auto dy = centerY(candidate.rectangle) - centerY(current.rectangle);
        if ((direction == Direction::Left && dx >= 0) ||
            (direction == Direction::Right && dx <= 0) || (direction == Direction::Up && dy >= 0) ||
            (direction == Direction::Down && dy <= 0)) {
            continue;
        }
        const bool aligned = horizontal
                                 ? overlaps(current.rectangle.y, current.rectangle.height,
                                            candidate.rectangle.y, candidate.rectangle.height)
                                 : overlaps(current.rectangle.x, current.rectangle.width,
                                            candidate.rectangle.x, candidate.rectangle.width);
        const auto distance = dx * dx + dy * dy;
        if (!best || (aligned && !bestOverlaps) ||
            (aligned == bestOverlaps && distance < bestDistance)) {
            best = &candidate;
            bestOverlaps = aligned;
            bestDistance = distance;
        }
    }
    return best;
}

const PreviewTile* neighborAcrossGroups(const PreviewTile& current, const PreviewGroup& group,
                                        Direction direction) {
    const PreviewTile* best = nullptr;
    bool bestOverlaps = false;
    std::int64_t bestDistance = 0;
    int bestEdge = 0;
    for (const auto& candidate : group.tiles) {
        const bool aligned = overlaps(current.rectangle.x, current.rectangle.width,
                                      candidate.rectangle.x, candidate.rectangle.width);
        const auto dx = centerX(candidate.rectangle) - centerX(current.rectangle);
        const auto distance = dx < 0 ? -dx : dx;
        const int edge = direction == Direction::Up
                             ? candidate.rectangle.y + candidate.rectangle.height
                             : -candidate.rectangle.y;
        if (!best || (aligned && !bestOverlaps) ||
            (aligned == bestOverlaps && distance < bestDistance) ||
            (aligned == bestOverlaps && distance == bestDistance && edge > bestEdge)) {
            best = &candidate;
            bestOverlaps = aligned;
            bestDistance = distance;
            bestEdge = edge;
        }
    }
    return best;
}

} // namespace

PrototypeService::PrototypeService(const TileGrid& grid, QString configPath, bool sampleMode)
    : grid_(grid), gridColumns_(grid_tokens::columns), configurationPath_(std::move(configPath)),
      sampleMode_(sampleMode) {
    if (sampleMode) {
        groups_ = makeDefaultGroups();
    } else {
        PreviewGroup applications{
            QStringLiteral("default"), QStringLiteral("常用应用"), {}, grid_.width(), 0};
        updateGroupGeometry(applications);
        groups_.append(applications);
    }
    loadResult_ = LayoutStore(configurationPath_).load(snapshot());
    const auto restored = restoreSnapshot(loadResult_.snapshot);
    if (!restored.success) {
        loadResult_.success = false;
        loadResult_.message = restored.message;
    } else if (loadResult_.snapshot.value(QStringLiteral("schemaVersion")).toInt() < LayoutStore::schemaVersion &&
               !loadResult_.readOnly) {
        if (!loadResult_.message.isEmpty()) {
            loadResult_.message += QStringLiteral("；");
        }
        loadResult_.message += QStringLiteral("已读取旧版布局，下次保存升级配置并保留旧版备份");
    }
}

bool PrototypeService::sampleMode() const { return sampleMode_; }

QVector<PreviewGroup> PrototypeService::makeDefaultGroups() const {
    struct PreviewDefinition {
        const char* id;
        const char* name;
        const char* symbol;
        int column;
        int row;
        TileSize size;
    };

    const std::array work{
        PreviewDefinition{"work-standard-1", "标准磁贴示例一", "A", 0, 0, TileSize::Standard},
        PreviewDefinition{"work-standard-long",
                          "标准磁贴长名称示例：中文与 English 名称保持固定网格尺寸", "B", 2, 0,
                          TileSize::Standard},
        PreviewDefinition{"work-standard-3", "标准磁贴示例三", "C", 4, 0, TileSize::Standard},
        PreviewDefinition{"work-wide-long",
                          "宽磁贴长名称示例：中文与 English 文本省略后仍可查看完整提示", "D", 6, 0,
                          TileSize::Wide},
    };
    const std::array daily{
        PreviewDefinition{"daily-wide", "宽磁贴示例", "E", 0, 0, TileSize::Wide},
        PreviewDefinition{"daily-standard-1", "标准磁贴示例四", "F", 4, 0, TileSize::Standard},
        PreviewDefinition{"daily-standard-2", "标准磁贴示例五", "G", 6, 0, TileSize::Standard},
        PreviewDefinition{"daily-small-1", "小磁贴示例一", "1", 8, 0, TileSize::Small},
        PreviewDefinition{"daily-small-2", "小磁贴示例二", "2", 9, 0, TileSize::Small},
        PreviewDefinition{"daily-small-3", "小磁贴示例三", "3", 8, 1, TileSize::Small},
        PreviewDefinition{"daily-small-4", "小磁贴示例四", "4", 9, 1, TileSize::Small},
    };
    const std::array entertainment{
        PreviewDefinition{"entertainment-wide", "宽磁贴示例二", "H", 0, 0, TileSize::Wide},
        PreviewDefinition{"entertainment-standard-1", "标准磁贴示例六", "I", 4, 0,
                          TileSize::Standard},
        PreviewDefinition{"entertainment-standard-2", "标准磁贴示例七", "J", 6, 0,
                          TileSize::Standard},
        PreviewDefinition{"entertainment-standard-3", "标准磁贴示例八", "K", 8, 0,
                          TileSize::Standard},
    };

    const auto makeGroup = [this](const char* id, const char* name,
                                  std::span<const PreviewDefinition> definitions) {
        PreviewGroup group{QString::fromUtf8(id), QString::fromUtf8(name), {}, grid_.width(), 0};
        group.tiles.reserve(static_cast<qsizetype>(definitions.size()));
        for (const auto& definition : definitions) {
            const auto rectangle =
                grid_.rectangle(definition.column, definition.row, definition.size);
            if (rectangle) {
                group.tiles.append(
                    {QString::fromUtf8(definition.id), QString::fromUtf8(definition.name),
                     QString::fromUtf8(definition.symbol), definition.column, definition.row,
                     definition.size, *rectangle,
                     QStringLiteral("prototype-") + QString::fromUtf8(definition.id)});
            }
        }
        updateGroupGeometry(group);
        return group;
    };

    return {makeGroup("work", "工作示例", work), makeGroup("daily", "日常示例", daily),
            makeGroup("entertainment", "娱乐示例", entertainment)};
}

QVector<PreviewGroup> PrototypeService::previewGroups() const { return groups_; }

int PrototypeService::gridColumns() const { return gridColumns_; }

LayoutResult PrototypeService::setGridColumns(int columns) {
    if (isReadOnly())
        return {false, QStringLiteral("当前配置只读，不能调整网格列数")};
    if (columns < TileGrid::minimumColumns || columns > TileGrid::maximumColumns)
        return {false, QStringLiteral("网格列数必须在4到64之间")};
    if (columns == gridColumns_)
        return {true, {}};
    auto planned = groups_;
    for (auto& group : planned) {
        std::array<bool, TileGrid::maximumColumns * LayoutStore::maximumRows> occupied{};
        const auto fits = [&](const PreviewTile& tile, int column, int row) {
            const auto rectangle = grid_.rectangle(column, row, tile.size, columns);
            const int rows =
                tile.size == TileSize::Small ? grid_tokens::smallRows : grid_tokens::standardRows;
            if (!rectangle || row > LayoutStore::maximumRows - rows)
                return false;
            const int span =
                (rectangle->width + grid_tokens::gap) / (grid_tokens::cell + grid_tokens::gap);
            for (int y = row; y < row + rows; ++y)
                for (int x = column; x < column + span; ++x)
                    if (occupied[y * columns + x])
                        return false;
            return true;
        };
        const auto mark = [&](const PreviewTile& tile) {
            const auto rectangle = *grid_.rectangle(tile.column, tile.row, tile.size, columns);
            const int span =
                (rectangle.width + grid_tokens::gap) / (grid_tokens::cell + grid_tokens::gap);
            const int rows =
                tile.size == TileSize::Small ? grid_tokens::smallRows : grid_tokens::standardRows;
            for (int y = tile.row; y < tile.row + rows; ++y)
                for (int x = tile.column; x < tile.column + span; ++x)
                    occupied[y * columns + x] = true;
        };
        QVector<qsizetype> displaced;
        for (qsizetype index = 0; index < group.tiles.size(); ++index) {
            const auto& tile = group.tiles[index];
            if (fits(tile, tile.column, tile.row))
                mark(tile);
            else
                displaced.append(index);
        }
        std::stable_sort(displaced.begin(), displaced.end(), [&](qsizetype a, qsizetype b) {
            const auto& left = group.tiles[a];
            const auto& right = group.tiles[b];
            return left.row != right.row ? left.row < right.row : left.column < right.column;
        });
        for (const auto index : displaced) {
            auto& tile = group.tiles[index];
            bool placed = false;
            for (int cell = 0; cell < columns * LayoutStore::maximumRows; ++cell) {
                if (!fits(tile, cell % columns, cell / columns))
                    continue;
                tile.column = cell % columns;
                tile.row = cell / columns;
                mark(tile);
                placed = true;
                break;
            }
            if (!placed)
                return {false, QStringLiteral("当前宽度无法容纳全部磁贴，已保留原布局")};
        }
    }
    gridColumns_ = columns;
    for (auto& group : planned)
        updateGroupGeometry(group);
    groups_ = std::move(planned);
    return {true, {}};
}

void PrototypeService::updateGroupGeometry(PreviewGroup& group) const {
    int bottom = 0;
    for (auto& tile : group.tiles) {
        const auto rectangle = grid_.rectangle(tile.column, tile.row, tile.size, gridColumns_);
        if (rectangle) {
            tile.rectangle = *rectangle;
            bottom = std::max(bottom, rectangle->y + rectangle->height);
        }
    }
    constexpr int pitch = grid_tokens::cell + grid_tokens::gap;
    group.width = grid_.width(gridColumns_);
    group.height = std::max(4 * pitch - grid_tokens::gap, bottom + 2 * pitch);
}

LayoutResult PrototypeService::planPlacement(const QString& tileId, const QString& groupId,
                                             int column, int row, TileSize size,
                                             QVector<PreviewGroup>& planned,
                                             const PreviewTile* inserted) const {
    const auto rectangle = grid_.rectangle(column, row, size, gridColumns_);
    const int rows = size == TileSize::Small ? grid_tokens::smallRows : grid_tokens::standardRows;
    if (!rectangle || row < 0 || row > LayoutStore::maximumRows - rows)
        return {false, QStringLiteral("落点超出当前列数或64行网格")};
    planned = groups_;
    PreviewTile moved = inserted ? *inserted : PreviewTile{};
    bool found = inserted != nullptr;
    qsizetype originalIndex = -1;
    PreviewGroup* destination = nullptr;
    for (auto& group : planned) {
        if (group.id == groupId)
            destination = &group;
        for (qsizetype index = 0; !inserted && index < group.tiles.size(); ++index) {
            if (group.tiles[index].id == tileId) {
                if (group.id == groupId)
                    originalIndex = index;
                moved = group.tiles.takeAt(index);
                found = true;
                break;
            }
        }
    }
    if (!found || !destination)
        return {false, QStringLiteral("磁贴或目标分组不存在")};
    moved.column = column;
    moved.row = row;
    moved.size = size;
    moved.rectangle = *rectangle;
    QVector<PreviewTile> ordered = destination->tiles;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.row != b.row ? a.row < b.row : a.column < b.column;
    });
    QVector<PreviewTile> placed{moved};
    std::array<bool, TileGrid::maximumColumns * LayoutStore::maximumRows> occupied{};
    const auto mark = [&](const PreviewTile& tile) {
        const int columns = tile.size == TileSize::Small      ? grid_tokens::smallColumns
                            : tile.size == TileSize::Standard ? grid_tokens::standardColumns
                                                              : grid_tokens::wideColumns;
        const int rows =
            tile.size == TileSize::Small ? grid_tokens::smallRows : grid_tokens::standardRows;
        for (int y = tile.row; y < tile.row + rows; ++y)
            for (int x = tile.column; x < tile.column + columns; ++x)
                occupied[y * gridColumns_ + x] = true;
    };
    mark(moved);
    for (auto tile : ordered) {
        auto free = [&](int candidateColumn, int candidateRow) {
            const auto candidate =
                grid_.rectangle(candidateColumn, candidateRow, tile.size, gridColumns_);
            if (!candidate ||
                candidateRow > LayoutStore::maximumRows - (tile.size == TileSize::Small
                                                               ? grid_tokens::smallRows
                                                               : grid_tokens::standardRows))
                return false;
            const int columns = tile.size == TileSize::Small      ? grid_tokens::smallColumns
                                : tile.size == TileSize::Standard ? grid_tokens::standardColumns
                                                                  : grid_tokens::wideColumns;
            const int rows =
                tile.size == TileSize::Small ? grid_tokens::smallRows : grid_tokens::standardRows;
            for (int y = candidateRow; y < candidateRow + rows; ++y)
                for (int x = candidateColumn; x < candidateColumn + columns; ++x)
                    if (occupied[y * gridColumns_ + x])
                        return false;
            tile.column = candidateColumn;
            tile.row = candidateRow;
            tile.rectangle = *candidate;
            return true;
        };
        bool fitted = free(tile.column, tile.row);
        // Stable row-major displacement. The dragged tile owns its requested
        // cells; every other tile keeps its position until a collision occurs.
        const int start = tile.row * gridColumns_ + tile.column;
        for (int cell = start; !fitted && cell < LayoutStore::maximumRows * gridColumns_; ++cell)
            fitted = free(cell % gridColumns_, cell / gridColumns_);
        for (int cell = 0; !fitted && cell < start; ++cell)
            fitted = free(cell % gridColumns_, cell / gridColumns_);
        if (!fitted)
            return {false, QStringLiteral("分组空间不足，未改变布局")};
        placed.append(tile);
        mark(tile);
    }
    // Keep model order and stable IDs independent from geometric packing.
    for (auto& tile : destination->tiles) {
        const auto match = std::find_if(placed.cbegin(), placed.cend(),
                                        [&](const auto& value) { return value.id == tile.id; });
        tile = *match;
    }
    if (originalIndex >= 0)
        destination->tiles.insert(originalIndex, moved);
    else
        destination->tiles.append(moved);
    for (auto& group : planned)
        updateGroupGeometry(group);
    return {true, {}};
}

LayoutResult PrototypeService::checkPlacement(const QString& tileId, const QString& groupId,
                                              int column, int row, TileSize size) const {
    QVector<PreviewGroup> planned;
    return planPlacement(tileId, groupId, column, row, size, planned);
}

LayoutResult PrototypeService::moveTile(const QString& tileId, const QString& groupId, int column,
                                        int row) {
    if (isReadOnly())
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    for (const auto& group : groups_) {
        for (const auto& tile : group.tiles) {
            if (tile.id != tileId)
                continue;
            QVector<PreviewGroup> planned;
            const auto result = planPlacement(tileId, groupId, column, row, tile.size, planned);
            if (result.success)
                groups_ = std::move(planned);
            return result;
        }
    }
    return {false, QStringLiteral("磁贴不存在")};
}

LayoutResult PrototypeService::resizeTile(const QString& tileId, TileSize size) {
    if (isReadOnly())
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    for (const auto& group : groups_) {
        for (const auto& tile : group.tiles) {
            if (tile.id != tileId)
                continue;
            const int columns = size == TileSize::Small      ? grid_tokens::smallColumns
                                : size == TileSize::Standard ? grid_tokens::standardColumns
                                                             : grid_tokens::wideColumns;
            QVector<PreviewGroup> planned;
            const auto result =
                planPlacement(tileId, group.id, std::min(tile.column, gridColumns_ - columns),
                              tile.row, size, planned);
            if (result.success)
                groups_ = std::move(planned);
            return result;
        }
    }
    return {false, QStringLiteral("磁贴不存在")};
}

LayoutResult PrototypeService::pinApplication(const QJsonObject& target, const QString& groupId) {
    if (isReadOnly()) {
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    }
    const auto applicationId = target.value(QStringLiteral("id")).toString();
    for (const auto& group : groups_) {
        for (const auto& tile : group.tiles) {
            if (tile.targetId == applicationId) {
                return {true, QStringLiteral("此应用已固定"), false, tile.id};
            }
        }
    }
    if (target.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("shortcut")) &&
        target.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("packaged"))) {
        return {false, QStringLiteral("应用目标类型不受支持")};
    }
    const auto destinationId = groupId.isEmpty() ? groups_.last().id : groupId;
    const auto destination = std::find_if(groups_.cbegin(), groups_.cend(), [&](const auto& group) {
        return group.id == destinationId;
    });
    if (destination == groups_.cend()) {
        return {false, QStringLiteral("目标分组不存在")};
    }
    std::optional<std::pair<int, int>> placement;
    for (int row = 0; row <= LayoutStore::maximumRows - grid_tokens::standardRows && !placement;
         ++row) {
        for (int column = 0; column <= gridColumns_ - grid_tokens::standardColumns; ++column) {
            const auto rectangle = grid_.rectangle(column, row, TileSize::Standard, gridColumns_);
            const auto occupied = std::any_of(
                destination->tiles.cbegin(), destination->tiles.cend(), [&](const auto& other) {
                    return overlaps(rectangle->x, rectangle->width, other.rectangle.x,
                                    other.rectangle.width) &&
                           overlaps(rectangle->y, rectangle->height, other.rectangle.y,
                                    other.rectangle.height);
                });
            if (!occupied) {
                placement = std::pair{column, row};
                break;
            }
        }
    }
    if (!placement) {
        return {false, QStringLiteral("目标分组没有可用的标准磁贴位置")};
    }
    return pinApplicationAt(target, destinationId, placement->first, placement->second);
}

LayoutResult PrototypeService::checkApplicationPlacement(const QJsonObject& target,
                                                         const QString& groupId, int column,
                                                         int row) const {
    // A value copy preserves configuration metadata without loading or saving files.
    auto preview = *this;
    return preview.pinApplicationAt(target, groupId, column, row);
}

LayoutResult PrototypeService::pinApplicationAt(const QJsonObject& target, const QString& groupId,
                                                int column, int row) {
    if (isReadOnly())
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    const auto applicationId = target.value(QStringLiteral("id")).toString();
    for (const auto& group : groups_)
        for (const auto& tile : group.tiles)
            if (tile.targetId == applicationId)
                return {true, QStringLiteral("此应用已固定"), false, tile.id};
    if (target.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("shortcut")) &&
        target.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("packaged")))
        return {false, QStringLiteral("应用目标类型不受支持")};
    const auto tileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const PreviewTile inserted{tileId,
                               target.value(QStringLiteral("name")).toString(),
                               target.value(QStringLiteral("symbol")).toString(),
                               column,
                               row,
                               TileSize::Standard,
                               {},
                               applicationId,
                               target.value(QStringLiteral("iconKey")).toString(),
                               {}};
    QVector<PreviewGroup> planned;
    const auto placement =
        planPlacement(tileId, groupId, column, row, TileSize::Standard, planned, &inserted);
    if (!placement.success)
        return placement;
    auto targets = targets_;
    bool replaced = false;
    for (qsizetype index = 0; index < targets.size(); ++index) {
        if (targets[index].toObject().value(QStringLiteral("id")).toString() == applicationId) {
            targets[index] = target;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        targets.append(target);
    }
    auto candidate = *this;
    candidate.groups_ = std::move(planned);
    candidate.targets_ = std::move(targets);
    auto result = restoreSnapshot(candidate.snapshot());
    if (result.success) {
        result.tileId = tileId;
    }
    return result;
}

LayoutResult PrototypeService::unpinApplication(const QString& applicationId) {
    if (isReadOnly())
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    auto planned = groups_;
    bool removed = false;
    for (auto& group : planned) {
        const auto before = group.tiles.size();
        group.tiles.erase(
            std::remove_if(group.tiles.begin(), group.tiles.end(),
                           [&](const auto& tile) { return tile.targetId == applicationId; }),
            group.tiles.end());
        removed = removed || before != group.tiles.size();
        updateGroupGeometry(group);
    }
    if (!removed)
        return {false, QStringLiteral("此应用尚未固定")};
    auto targets = targets_;
    for (qsizetype index = targets.size(); index-- > 0;)
        if (targets[index].toObject().value(QStringLiteral("id")).toString() == applicationId)
            targets.removeAt(index);
    // Every reference to this app is removed in one transaction, including restored duplicates.
    groups_ = std::move(planned);
    targets_ = std::move(targets);
    return {true, {}};
}

LayoutResult PrototypeService::unpinTile(const QString& tileId) {
    if (isReadOnly()) {
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    }
    for (auto& group : groups_) {
        for (qsizetype index = 0; index < group.tiles.size(); ++index) {
            if (group.tiles[index].id != tileId) {
                continue;
            }
            const auto targetId = group.tiles[index].targetId;
            group.tiles.removeAt(index);
            updateGroupGeometry(group);
            if (!isApplicationPinned(targetId)) {
                for (qsizetype targetIndex = targets_.size(); targetIndex-- > 0;) {
                    if (targets_[targetIndex].toObject().value(QStringLiteral("id")).toString() ==
                        targetId) {
                        targets_.removeAt(targetIndex);
                    }
                }
            }
            return {true, {}, false, tileId};
        }
    }
    return {false, QStringLiteral("磁贴不存在")};
}

LayoutResult PrototypeService::renameTile(const QString& tileId, const QString& displayName) {
    return updateTileDisplayName(tileId, displayName, false);
}

LayoutResult PrototypeService::resetTileDisplayName(const QString& tileId) {
    return updateTileDisplayName(tileId, {}, true);
}

LayoutResult PrototypeService::updateTileDisplayName(const QString& tileId,
                                                     const QString& displayName, bool reset) {
    if (isReadOnly())
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    const auto name = displayName.trimmed();
    if (!reset && (name.isEmpty() || name.size() > LayoutStore::maximumTileDisplayNameLength ||
        !displayName.isValidUtf16() ||
        std::any_of(displayName.cbegin(), displayName.cend(), [](QChar character) {
            return character.category() == QChar::Other_Control ||
                   character.category() == QChar::Separator_Line ||
                   character.category() == QChar::Separator_Paragraph;
        })))
        return {false, QStringLiteral("磁贴显示名称须为 1 至 128 个字符，不能包含换行或控制字符")};
    auto candidate = snapshot();
    auto groups = candidate.value(QStringLiteral("groups")).toArray();
    for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        auto group = groups[groupIndex].toObject();
        auto tiles = group.value(QStringLiteral("tiles")).toArray();
        for (qsizetype tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
            auto tile = tiles[tileIndex].toObject();
            if (tile.value(QStringLiteral("id")).toString() != tileId)
                continue;
            if (reset)
                tile.remove(QStringLiteral("displayName"));
            else
                tile.insert(QStringLiteral("displayName"), name);
            tiles[tileIndex] = tile;
            group.insert(QStringLiteral("tiles"), tiles);
            groups[groupIndex] = group;
            candidate.insert(QStringLiteral("groups"), groups);
            auto result = restoreSnapshot(candidate);
            if (result.success)
                result.tileId = tileId;
            return result;
        }
    }
    return {false, QStringLiteral("磁贴不存在")};
}

LayoutResult PrototypeService::createGroup(const QString& name) {
    if (isReadOnly()) {
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    }
    const auto nameValidation = validateGroupName(name);
    if (!nameValidation.success) {
        return nameValidation;
    }
    if (groups_.size() >= LayoutStore::maximumGroups) {
        return {false, QStringLiteral("最多只能创建 32 个分组")};
    }
    PreviewGroup group{QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(), {},
                       grid_.width(), 0};
    updateGroupGeometry(group);
    auto candidate = snapshot();
    auto groups = candidate.value(QStringLiteral("groups")).toArray();
    groups.append(QJsonObject{{QStringLiteral("id"), group.id},
                              {QStringLiteral("name"), group.name},
                              {QStringLiteral("tiles"), QJsonArray{}}});
    candidate.insert(QStringLiteral("groups"), groups);
    const auto validation = LayoutStore::validate(candidate);
    if (!validation.success) {
        return validation;
    }
    groups_.append(group);
    return {true, {}, false, {}, group.id};
}

LayoutResult PrototypeService::renameGroup(const QString& groupId, const QString& name) {
    if (isReadOnly()) {
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    }
    const auto nameValidation = validateGroupName(name);
    if (!nameValidation.success) {
        return nameValidation;
    }
    for (qsizetype index = 0; index < groups_.size(); ++index) {
        if (groups_[index].id != groupId) {
            continue;
        }
        auto candidate = snapshot();
        auto groups = candidate.value(QStringLiteral("groups")).toArray();
        auto group = groups[index].toObject();
        group.insert(QStringLiteral("name"), name.trimmed());
        groups[index] = group;
        candidate.insert(QStringLiteral("groups"), groups);
        const auto validation = LayoutStore::validate(candidate);
        if (!validation.success) {
            return validation;
        }
        groups_[index].name = name.trimmed();
        return {true, {}, false, {}, groupId};
    }
    return {false, QStringLiteral("分组不存在")};
}

LayoutResult PrototypeService::moveGroupBy(const QString& groupId, int delta) {
    if (isReadOnly()) {
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    }
    if (delta != -1 && delta != 1) {
        return {false, QStringLiteral("分组每次只能向上或向下移动一位")};
    }
    for (qsizetype index = 0; index < groups_.size(); ++index) {
        if (groups_[index].id != groupId) {
            continue;
        }
        const auto destination = index + delta;
        if (destination < 0 || destination >= groups_.size()) {
            return {false, QStringLiteral("分组已在列表边界")};
        }
        groups_.swapItemsAt(index, destination);
        return {true, {}, false, {}, groupId};
    }
    return {false, QStringLiteral("分组不存在")};
}

LayoutResult PrototypeService::removeGroup(const QString& groupId) {
    if (isReadOnly()) {
        return {false, QStringLiteral("更新版本配置当前为只读"), true};
    }
    for (qsizetype index = 0; index < groups_.size(); ++index) {
        if (groups_[index].id != groupId) {
            continue;
        }
        if (groups_.size() == 1) {
            return {false, QStringLiteral("至少需要保留一个分组")};
        }
        if (!groups_[index].tiles.isEmpty()) {
            return {false, QStringLiteral("只能删除空分组，请先移动或取消固定其中的磁贴")};
        }
        groups_.removeAt(index);
        return {true, {}, false, {}, groupId};
    }
    return {false, QStringLiteral("分组不存在")};
}

QJsonObject PrototypeService::targetForTile(const QString& tileId) const {
    for (const auto& group : groups_) {
        for (const auto& tile : group.tiles) {
            if (tile.id == tileId) {
                for (const auto& target : targets_) {
                    const auto object = target.toObject();
                    if (object.value(QStringLiteral("id")).toString() == tile.targetId) {
                        return object;
                    }
                }
            }
        }
    }
    return {};
}

bool PrototypeService::isApplicationPinned(const QString& applicationId) const {
    for (const auto& group : groups_) {
        for (const auto& tile : group.tiles) {
            if (tile.targetId == applicationId) {
                return true;
            }
        }
    }
    return false;
}

QJsonObject PrototypeService::snapshot() const {
    const auto sizeName = [](TileSize size) {
        switch (size) {
        case TileSize::Small:
            return QStringLiteral("small");
        case TileSize::Standard:
            return QStringLiteral("standard");
        case TileSize::Wide:
            return QStringLiteral("wide");
        }
        return QString{};
    };
    QJsonArray groups;
    QJsonArray targets;
    QSet<QString> seenTargets;
    for (const auto& group : groups_) {
        QJsonArray tiles;
        for (const auto& tile : group.tiles) {
            QJsonObject entry{{QStringLiteral("id"), tile.id},
                              {QStringLiteral("targetId"), tile.targetId},
                              {QStringLiteral("column"), tile.column},
                              {QStringLiteral("row"), tile.row},
                              {QStringLiteral("size"), sizeName(tile.size)}};
            if (!tile.displayName.isEmpty())
                entry.insert(QStringLiteral("displayName"), tile.displayName);
            tiles.append(entry);
            if (!seenTargets.contains(tile.targetId)) {
                targets.append(QJsonObject{{QStringLiteral("id"), tile.targetId},
                                           {QStringLiteral("type"), QStringLiteral("prototype")},
                                           {QStringLiteral("name"), tile.name},
                                           {QStringLiteral("symbol"), tile.symbol}});
                seenTargets.insert(tile.targetId);
            }
        }
        groups.append(QJsonObject{{QStringLiteral("id"), group.id},
                                  {QStringLiteral("name"), group.name},
                                  {QStringLiteral("tiles"), tiles}});
    }
    return {{QStringLiteral("schemaVersion"), LayoutStore::schemaVersion},
            {QStringLiteral("gridColumns"), gridColumns_},
            {QStringLiteral("targets"), targets_.isEmpty() ? targets : targets_},
            {QStringLiteral("groups"), groups},
            {QStringLiteral("preferences"), preferences_}};
}

LayoutResult PrototypeService::restoreSnapshot(const QJsonObject& snapshot) {
    const auto validation = LayoutStore::validate(snapshot);
    if (!validation.success) {
        return validation;
    }
    gridColumns_ = snapshot.value(QStringLiteral("schemaVersion")).toInt() >= 4
                       ? snapshot.value(QStringLiteral("gridColumns")).toInt()
                       : grid_tokens::columns;
    QMap<QString, QJsonObject> targets;
    for (const auto& target : snapshot.value(QStringLiteral("targets")).toArray()) {
        const auto object = target.toObject();
        targets.insert(object.value(QStringLiteral("id")).toString(), object);
    }
    QVector<PreviewGroup> restoredGroups;
    for (const auto& value : snapshot.value(QStringLiteral("groups")).toArray()) {
        const auto object = value.toObject();
        PreviewGroup group{object.value(QStringLiteral("id")).toString(),
                           object.value(QStringLiteral("name")).toString(),
                           {},
                           grid_.width(),
                           0};
        for (const auto& tileValue : object.value(QStringLiteral("tiles")).toArray()) {
            const auto tile = tileValue.toObject();
            const auto targetId = tile.value(QStringLiteral("targetId")).toString();
            const auto target = targets.value(targetId);
            const auto sizeText = tile.value(QStringLiteral("size")).toString();
            const auto size = sizeText == QLatin1String("small")  ? TileSize::Small
                              : sizeText == QLatin1String("wide") ? TileSize::Wide
                                                                  : TileSize::Standard;
            group.tiles.append({tile.value(QStringLiteral("id")).toString(),
                                tile.contains(QStringLiteral("displayName"))
                                    ? tile.value(QStringLiteral("displayName")).toString()
                                    : target.value(QStringLiteral("name")).toString(),
                                target.value(QStringLiteral("symbol")).toString(),
                                tile.value(QStringLiteral("column")).toInt(),
                                tile.value(QStringLiteral("row")).toInt(),
                                size,
                                {},
                                targetId,
                                target.value(QStringLiteral("iconKey")).toString(),
                                tile.value(QStringLiteral("displayName")).toString()});
        }
        updateGroupGeometry(group);
        restoredGroups.append(group);
    }
    groups_ = std::move(restoredGroups);
    preferences_ = snapshot.value(QStringLiteral("preferences")).toObject();
    targets_ = snapshot.value(QStringLiteral("targets")).toArray();
    return {true, {}};
}

const LayoutLoadResult& PrototypeService::loadResult() const { return loadResult_; }

bool PrototypeService::isReadOnly() const { return loadResult_.readOnly; }

QString PrototypeService::configurationPath() const { return configurationPath_; }

QString PrototypeService::neighborTileId(const QString& tileId, const QString& direction) const {
    const auto parsedDirection = parseDirection(direction);
    if (!parsedDirection) {
        return {};
    }
    const auto groups = previewGroups();
    for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const auto& group = groups[groupIndex];
        for (const auto& tile : group.tiles) {
            if (tile.id != tileId) {
                continue;
            }
            if (const auto* neighbor = neighborInGroup(tile, group, *parsedDirection)) {
                return neighbor->id;
            }
            const bool up = *parsedDirection == Direction::Up;
            const bool down = *parsedDirection == Direction::Down;
            if ((!up && !down) || (up && groupIndex == 0) ||
                (down && groupIndex + 1 == groups.size())) {
                return {};
            }
            for (auto adjacent = groupIndex + (up ? -1 : 1);
                 adjacent >= 0 && adjacent < groups.size(); adjacent += up ? -1 : 1) {
                if (const auto* neighbor =
                        neighborAcrossGroups(tile, groups[adjacent], *parsedDirection)) {
                    return neighbor->id;
                }
            }
            return {};
        }
    }
    return {};
}

} // namespace xdstarter
