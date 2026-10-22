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
    [[nodiscard]] QString neighborTileId(const QString& tileId, const QString& direction) const;
    [[nodiscard]] LayoutResult checkPlacement(const QString& tileId, const QString& groupId,
                                              int column, int row, TileSize size) const;
    [[nodiscard]] LayoutResult moveTile(const QString& tileId, const QString& groupId, int column,
                                        int row);
    [[nodiscard]] LayoutResult resizeTile(const QString& tileId, TileSize size);
    [[nodiscard]] LayoutResult pinApplication(const QJsonObject& target,
                                              const QString& groupId = {});
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

  private:
    const TileGrid& grid_;
    QVector<PreviewGroup> groups_;
    QJsonObject preferences_;
    QJsonArray targets_;
    LayoutLoadResult loadResult_;
    QString configurationPath_;
    [[nodiscard]] QVector<PreviewGroup> makeDefaultGroups() const;
    void updateGroupGeometry(PreviewGroup& group) const;
    [[nodiscard]] LayoutResult updateTileDisplayName(const QString& tileId,
                                                    const QString& displayName, bool reset);
};

} // namespace xdstarter
