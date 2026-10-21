#pragma once

#include <QJsonObject>
#include <QString>

namespace xdstarter {

struct LayoutResult {
    bool success;
    QString message;
    bool readOnly = false;
    QString tileId;
    QString groupId;
};

struct LayoutLoadResult {
    QJsonObject snapshot;
    bool success = false;
    bool usedDefaults = true;
    bool recoveredFromBackup = false;
    bool readOnly = false;
    QString message;
};

class LayoutStore final {
  public:
    static constexpr int schemaVersion = 2;
    static constexpr int maximumGroups = 32;
    static constexpr int maximumGroupNameLength = 128;
    static constexpr int maximumRows = 64;
    static constexpr qint64 maximumFileBytes = 1024 * 1024;

    explicit LayoutStore(QString path);
    [[nodiscard]] LayoutLoadResult load(const QJsonObject& defaultSnapshot) const;
    [[nodiscard]] LayoutResult save(const QJsonObject& snapshot) const;
    [[nodiscard]] static LayoutResult validate(const QJsonObject& snapshot);

  private:
    QString path_;
};

} // namespace xdstarter
