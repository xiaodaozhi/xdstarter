#include "layout_store.h"

#include "core/tile_grid.h"
#include "xdstarter/grid_tokens.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace xdstarter {
namespace {

constexpr int maximumTiles = 512;

LayoutResult failure(const QString& message) { return {false, message}; }

bool integer(const QJsonValue& value, int minimum, int maximum) {
    return value.isDouble() && std::isfinite(value.toDouble()) &&
           value.toDouble() == std::floor(value.toDouble()) && value.toDouble() >= minimum &&
           value.toDouble() <= maximum;
}

bool text(const QJsonValue& value, int maximum = 128) {
    return value.isString() && !value.toString().trimmed().isEmpty() &&
           value.toString().size() <= maximum;
}

bool keys(const QJsonObject& object, const QStringList& allowed) {
    for (const auto& key : object.keys()) {
        if (!allowed.contains(key)) {
            return false;
        }
    }
    return true;
}

bool launchTarget(const QJsonObject& target, int version) {
    if (!keys(target, {QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("name"),
                       QStringLiteral("symbol"), QStringLiteral("iconKey"),
                       QStringLiteral("shortcutPath"), QStringLiteral("aumid")}) ||
        !text(target.value(QStringLiteral("id"))) ||
        !text(target.value(QStringLiteral("name")), 512) ||
        !text(target.value(QStringLiteral("symbol")), 16) ||
        (target.contains(QStringLiteral("iconKey")) &&
         (!target.value(QStringLiteral("iconKey")).isString() ||
          target.value(QStringLiteral("iconKey")).toString().size() > 512))) {
        return false;
    }
    const auto type = target.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("prototype")) {
        return !target.contains(QStringLiteral("shortcutPath")) &&
               !target.contains(QStringLiteral("aumid")) &&
               (version >= 2 || !target.contains(QStringLiteral("iconKey")));
    }
    if (version < 2) {
        return false;
    }
    if (type == QLatin1String("shortcut")) {
        const auto value = target.value(QStringLiteral("shortcutPath"));
        const auto path = value.toString();
        const auto normalized =
            QDir::fromNativeSeparators(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
        static const QRegularExpression absolutePath(
            QStringLiteral("^(?:[A-Za-z]:/|//[^/?]+/[^/]+/).+\\.lnk\\z"),
            QRegularExpression::CaseInsensitiveOption);
        const auto pathBody = normalized.mid(normalized.startsWith(QLatin1String("//")) ? 0 : 2);
        return !target.contains(QStringLiteral("aumid")) && text(value, 32767) &&
               absolutePath.match(normalized).hasMatch() && !pathBody.contains(QLatin1Char(':')) &&
               std::none_of(path.cbegin(), path.cend(), [](QChar character) {
                   return character.category() == QChar::Other_Control ||
                          character == QLatin1Char('"') || character == QLatin1Char('<') ||
                          character == QLatin1Char('>') || character == QLatin1Char('|') ||
                          character == QLatin1Char('*') || character == QLatin1Char('?');
               });
    }
    if (type == QLatin1String("packaged")) {
        static const QRegularExpression packagedId(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*![A-Za-z0-9][A-Za-z0-9._-]*\\z"));
        return !target.contains(QStringLiteral("shortcutPath")) &&
               text(target.value(QStringLiteral("aumid")), 256) &&
               packagedId.match(target.value(QStringLiteral("aumid")).toString()).hasMatch();
    }
    return false;
}

std::optional<TileSize> tileSize(const QJsonValue& value) {
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto size = value.toString();
    if (size == QLatin1String("small")) {
        return TileSize::Small;
    }
    if (size == QLatin1String("standard")) {
        return TileSize::Standard;
    }
    if (size == QLatin1String("wide")) {
        return TileSize::Wide;
    }
    return std::nullopt;
}

bool overlap(const TileRect& first, const TileRect& second) {
    return first.x < second.x + second.width && second.x < first.x + first.width &&
           first.y < second.y + second.height && second.y < first.y + first.height;
}

struct ReadResult {
    bool exists = false;
    bool valid = false;
    bool future = false;
    bool oversized = false;
    QJsonObject snapshot;
    QByteArray bytes;
    QString error;
};

ReadResult readSnapshot(const QString& path) {
    ReadResult result;
    result.exists = QFileInfo::exists(path);
    if (!result.exists) {
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取配置：%1").arg(file.errorString());
        return result;
    }
    const auto data = file.read(LayoutStore::maximumFileBytes + 1);
    if (data.size() > LayoutStore::maximumFileBytes) {
        result.oversized = true;
        result.error = QStringLiteral("配置超过 1 MiB 限制，无法安全读取");
        return result;
    }
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("配置读取失败：%1").arg(file.errorString());
        return result;
    }
    result.bytes = data;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("配置不是有效的 JSON 对象");
        return result;
    }
    result.snapshot = document.object();
    const auto version = result.snapshot.value(QStringLiteral("schemaVersion"));
    result.future = version.isDouble() && std::isfinite(version.toDouble()) &&
                    version.toDouble() == std::floor(version.toDouble()) &&
                    version.toDouble() > LayoutStore::schemaVersion;
    if (result.future) {
        result.error = QStringLiteral("配置来自更新版本，当前窗口只读且不会覆盖原文件");
        return result;
    }
    const auto validation = LayoutStore::validate(result.snapshot);
    result.valid = validation.success;
    result.error = validation.message;
    return result;
}

LayoutResult writeBytes(const QString& path, const QByteArray& bytes) {
    if (bytes.size() > LayoutStore::maximumFileBytes) {
        return failure(QStringLiteral("配置超过 1 MiB 限制"));
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return failure(QStringLiteral("无法创建配置临时文件：%1").arg(file.errorString()));
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return failure(QStringLiteral("配置原子写入失败：%1").arg(file.errorString()));
    }
    return {true, {}};
}

LayoutResult writeSnapshot(const QString& path, const QJsonObject& snapshot) {
    return writeBytes(path, QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
}

} // namespace

LayoutStore::LayoutStore(QString path) : path_(std::move(path)) {}

LayoutResult LayoutStore::validate(const QJsonObject& snapshot) {
    if (!keys(snapshot, {QStringLiteral("schemaVersion"), QStringLiteral("targets"),
                         QStringLiteral("groups"), QStringLiteral("preferences")}) ||
        !integer(snapshot.value(QStringLiteral("schemaVersion")), 1, schemaVersion) ||
        !snapshot.value(QStringLiteral("targets")).isArray() ||
        !snapshot.value(QStringLiteral("groups")).isArray() ||
        !snapshot.value(QStringLiteral("preferences")).isObject()) {
        return failure(QStringLiteral("配置版本或根结构不受支持"));
    }
    const auto targets = snapshot.value(QStringLiteral("targets")).toArray();
    const auto groups = snapshot.value(QStringLiteral("groups")).toArray();
    const auto version = snapshot.value(QStringLiteral("schemaVersion")).toInt();
    if ((version == 1 && targets.isEmpty()) || targets.size() > maximumTiles || groups.isEmpty() ||
        groups.size() > maximumGroups) {
        return failure(QStringLiteral("配置的目标或分组数量超出限制"));
    }
    const auto preferences = snapshot.value(QStringLiteral("preferences")).toObject();
    if (!keys(preferences, {QStringLiteral("theme")}) ||
        (preferences.contains(QStringLiteral("theme")) &&
         preferences.value(QStringLiteral("theme")) != QJsonValue(QStringLiteral("dark")) &&
         preferences.value(QStringLiteral("theme")) != QJsonValue(QStringLiteral("light")))) {
        return failure(QStringLiteral("主题偏好必须为 dark 或 light"));
    }
    QSet<QString> targetIds;
    for (const auto& value : targets) {
        if (!value.isObject()) {
            return failure(QStringLiteral("目标必须是对象"));
        }
        const auto target = value.toObject();
        const auto id = target.value(QStringLiteral("id"));
        if (targetIds.contains(id.toString()) || !launchTarget(target, version)) {
            return failure(QStringLiteral("目标标识、类型或显示名称无效"));
        }
        targetIds.insert(id.toString());
    }
    QSet<QString> groupIds;
    QSet<QString> tileIds;
    const TileGrid grid;
    int count = 0;
    for (const auto& value : groups) {
        if (!value.isObject()) {
            return failure(QStringLiteral("分组必须是对象"));
        }
        const auto group = value.toObject();
        const auto id = group.value(QStringLiteral("id"));
        if (!keys(group, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("tiles")}) ||
            !text(id) || groupIds.contains(id.toString()) ||
            !text(group.value(QStringLiteral("name")), maximumGroupNameLength) ||
            !group.value(QStringLiteral("tiles")).isArray()) {
            return failure(QStringLiteral("分组标识、名称或磁贴列表无效"));
        }
        groupIds.insert(id.toString());
        QVector<TileRect> rectangles;
        for (const auto& tileValue : group.value(QStringLiteral("tiles")).toArray()) {
            if (++count > maximumTiles || !tileValue.isObject()) {
                return failure(QStringLiteral("磁贴数量超出限制或磁贴结构无效"));
            }
            const auto tile = tileValue.toObject();
            const auto tileId = tile.value(QStringLiteral("id"));
            const auto size = tileSize(tile.value(QStringLiteral("size")));
            const auto column = tile.value(QStringLiteral("column"));
            const auto row = tile.value(QStringLiteral("row"));
            if (!keys(tile,
                      {QStringLiteral("id"), QStringLiteral("targetId"), QStringLiteral("column"),
                       QStringLiteral("row"), QStringLiteral("size")}) ||
                !text(tileId) || tileIds.contains(tileId.toString()) ||
                !tile.value(QStringLiteral("targetId")).isString() ||
                !targetIds.contains(tile.value(QStringLiteral("targetId")).toString()) || !size ||
                !integer(column, 0, grid_tokens::columns - 1) ||
                !integer(row, 0, maximumRows - 1) ||
                row.toInt() + (*size == TileSize::Small      ? grid_tokens::smallRows
                               : *size == TileSize::Standard ? grid_tokens::standardRows
                                                             : grid_tokens::wideRows) >
                    maximumRows) {
                return failure(QStringLiteral("磁贴标识、引用、尺寸或格坐标无效"));
            }
            const auto rectangle = grid.rectangle(column.toInt(), row.toInt(), *size);
            if (!rectangle) {
                return failure(QStringLiteral("磁贴超出十列网格"));
            }
            for (const auto& other : rectangles) {
                if (overlap(*rectangle, other)) {
                    return failure(QStringLiteral("同组磁贴发生重叠"));
                }
            }
            rectangles.append(*rectangle);
            tileIds.insert(tileId.toString());
        }
    }
    if (version == 1 && count == 0) {
        return failure(QStringLiteral("原型配置至少需要一枚磁贴"));
    }
    if (QJsonDocument(snapshot).toJson(QJsonDocument::Compact).size() > maximumFileBytes) {
        return failure(QStringLiteral("配置超过 1 MiB 限制"));
    }
    return {true, {}};
}

LayoutLoadResult LayoutStore::load(const QJsonObject& defaultSnapshot) const {
    LayoutLoadResult result;
    result.snapshot = defaultSnapshot;
    const auto defaultsValid = validate(defaultSnapshot);
    if (!defaultsValid.success) {
        result.message = QStringLiteral("默认布局无效：%1").arg(defaultsValid.message);
        return result;
    }
    if (path_.isEmpty()) {
        result.success = true;
        return result;
    }
    const auto primary = readSnapshot(path_);
    const auto backup = readSnapshot(path_ + QStringLiteral(".bak"));
    const bool oversized = primary.oversized || backup.oversized;
    result.readOnly = primary.future || backup.future || oversized;
    const auto protectedMessage =
        oversized ? QStringLiteral("配置或备份超过 1 MiB，当前只读显示，原文件保留")
                  : QStringLiteral("检测到更新版本配置，当前只读显示，原文件保留");
    if (primary.valid) {
        result.snapshot = primary.snapshot;
        result.success = true;
        result.usedDefaults = false;
        if (result.readOnly) {
            result.message = protectedMessage;
        }
        return result;
    }
    if (backup.valid) {
        result.snapshot = backup.snapshot;
        result.success = true;
        result.usedDefaults = false;
        result.recoveredFromBackup = true;
        result.message = result.readOnly
                             ? protectedMessage + QStringLiteral("；已读取有效备份")
                             : QStringLiteral("主配置无法读取；已从有效备份恢复，原文件保留");
        return result;
    }
    if (!primary.exists && !backup.exists) {
        result.success = true;
        return result;
    }
    result.message = result.readOnly ? protectedMessage + QStringLiteral("；显示默认布局")
                                     : QStringLiteral("配置及备份不可用；显示默认布局，原文件保留");
    return result;
}

LayoutResult LayoutStore::save(const QJsonObject& snapshot) const {
    const auto validation = validate(snapshot);
    if (!validation.success) {
        return validation;
    }
    if (path_.isEmpty()) {
        return failure(QStringLiteral("未设置配置路径，当前仅使用内存布局"));
    }
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        return failure(QStringLiteral("无法创建配置目录"));
    }
    QLockFile lock(path_ + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) {
        return failure(lock.error() == QLockFile::LockFailedError
                           ? QStringLiteral("其他实例正在保存布局，请稍后重试")
                           : QStringLiteral("无法创建布局保存锁，请检查配置目录权限后重试"));
    }
    const auto primary = readSnapshot(path_);
    const auto backup = readSnapshot(path_ + QStringLiteral(".bak"));
    if (primary.oversized || backup.oversized) {
        return {false, QStringLiteral("配置或备份超过 1 MiB，无法安全读取，已阻止覆盖"), true};
    }
    if (primary.future || backup.future) {
        return {false, QStringLiteral("检测到更新版本配置，已阻止覆盖"), true};
    }
    if (primary.valid) {
        const auto backedUp = writeBytes(path_ + QStringLiteral(".bak"), primary.bytes);
        if (!backedUp.success) {
            return failure(
                QStringLiteral("备份写入失败，原配置保持不变：%1").arg(backedUp.message));
        }
    } else if (primary.exists) {
        const auto preserved =
            path_ + QStringLiteral(".corrupt-") +
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) +
            QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QFile::copy(path_, preserved)) {
            return failure(QStringLiteral("无法保留损坏配置，已取消保存"));
        }
    }
    if (!primary.valid && !backup.valid) {
        const auto backedUp = writeSnapshot(path_ + QStringLiteral(".bak"), snapshot);
        if (!backedUp.success) {
            return failure(
                QStringLiteral("备份写入失败，原配置保持不变：%1").arg(backedUp.message));
        }
    }
    return writeSnapshot(path_, snapshot);
}

} // namespace xdstarter
