#pragma once

#include "core/tile_grid.h"
#include "services/layout_store.h"

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace xdstarter {

struct PreviewTile {
    QString id;
    QString name;
    QString symbol;
    int column;
    int row;
    TileSize size;
    TileRect rectangle;
    QString targetId;
    QString iconKey;
    QString displayName;
};

struct PreviewGroup {
    QString id;
    QString name;
    QVector<PreviewTile> tiles;
    int width;
    int height;
};

class PrototypeService final {
  public:
    explicit PrototypeService(const TileGrid& grid, QString configPath = {},
                              bool sampleMode = true);

    [[nodiscard]] QVector<PreviewGroup> previewGroups() const;
    [[nodiscard]] int gridColumns() const;
    [[nodiscard]] LayoutResult setGridColumns(int columns);
    [[nodiscard]] QString neighborTileId(const QString& tileId, const QString& direction) const;
    [[nodiscard]] LayoutResult checkPlacement(const QString& tileId, const QString& groupId,
                                              int column, int row, TileSize size) const;
    [[nodiscard]] LayoutResult moveTile(const QString& tileId, const QString& groupId, int column,
                                        int row);
    [[nodiscard]] LayoutResult resizeTile(const QString& tileId, TileSize size);
    [[nodiscard]] LayoutResult pinApplication(const QJsonObject& target,
                                              const QString& groupId = {});
    [[nodiscard]] LayoutResult checkApplicationPlacement(const QJsonObject& target,
                                                         const QString& groupId, int column,
                                                         int row) const;
    [[nodiscard]] LayoutResult pinApplicationAt(const QJsonObject& target, const QString& groupId,
                                                int column, int row);
    [[nodiscard]] LayoutResult unpinApplication(const QString& applicationId);
    [[nodiscard]] LayoutResult unpinTile(const QString& tileId);
    [[nodiscard]] LayoutResult renameTile(const QString& tileId, const QString& displayName);
    [[nodiscard]] LayoutResult resetTileDisplayName(const QString& tileId);
    [[nodiscard]] LayoutResult createGroup(const QString& name);
    [[nodiscard]] LayoutResult renameGroup(const QString& groupId, const QString& name);
    [[nodiscard]] LayoutResult moveGroupBy(const QString& groupId, int delta);
    [[nodiscard]] LayoutResult removeGroup(const QString& groupId);
    [[nodiscard]] QJsonObject targetForTile(const QString& tileId) const;
    [[nodiscard]] bool isApplicationPinned(const QString& applicationId) const;
    [[nodiscard]] QJsonObject snapshot() const;
    [[nodiscard]] LayoutResult restoreSnapshot(const QJsonObject& snapshot);
    [[nodiscard]] const LayoutLoadResult& loadResult() const;
    [[nodiscard]] bool isReadOnly() const;
    [[nodiscard]] QString configurationPath() const;
    [[nodiscard]] bool sampleMode() const;

  private:
    const TileGrid& grid_;
    int gridColumns_ = 0;
    QVector<PreviewGroup> groups_;
    QJsonObject preferences_;
    QJsonArray targets_;
    LayoutLoadResult loadResult_;
    QString configurationPath_;
    bool sampleMode_ = true;
    [[nodiscard]] QVector<PreviewGroup> makeDefaultGroups() const;
    void updateGroupGeometry(PreviewGroup& group) const;
    [[nodiscard]] LayoutResult planPlacement(const QString& tileId, const QString& groupId,
                                             int column, int row, TileSize size,
                                             QVector<PreviewGroup>& planned,
                                             const PreviewTile* inserted = nullptr) const;
    [[nodiscard]] LayoutResult updateTileDisplayName(const QString& tileId,
                                                    const QString& displayName, bool reset);
};

} // namespace xdstarter
