// src/services/layout_import.cpp

#include "layout_import.h"

#include "core/tile_grid.h"
#include "services/prototype_service.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace xdstarter {

LayoutImportPreview readLayoutImport(const QString& path) {
    LayoutImportPreview preview;
    QFile file(path);
    if (path.isEmpty() || !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
        preview.message = QStringLiteral("无法读取所选布局文件，请检查文件是否存在及读取权限。");
        return preview;
    }
    const auto bytes = file.read(LayoutStore::maximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        preview.message = QStringLiteral("读取所选布局文件失败，当前布局未更改。");
        return preview;
    }
    if (bytes.size() > LayoutStore::maximumFileBytes) {
        preview.message = QStringLiteral("所选布局文件超过 1 MiB，不能导入。");
        return preview;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        preview.message = QStringLiteral("所选文件不是有效的 JSON 布局，当前布局未更改。");
        return preview;
    }
    const auto source = document.object();
    const auto validation = LayoutStore::validate(source);
    if (!validation.success) {
        preview.message = validation.message;
        return preview;
    }
    // Normalize old versions using precisely the same service restore path as startup.
    const TileGrid grid;
    PrototypeService candidate(grid, {}, false);
    const auto restored = candidate.restoreSnapshot(source);
    if (!restored.success) {
        preview.message = restored.message;
        return preview;
    }
    preview.snapshot = candidate.snapshot();
    preview.sourceVersion = source.value(QStringLiteral("schemaVersion")).toInt();
    const auto groups = preview.snapshot.value(QStringLiteral("groups")).toArray();
    preview.groupCount = int(groups.size());
    preview.targetCount = int(preview.snapshot.value(QStringLiteral("targets")).toArray().size());
    for (const auto& group : groups)
        preview.tileCount += int(group.toObject().value(QStringLiteral("tiles")).toArray().size());
    preview.success = true;
    return preview;
}

LayoutImportCommitResult commitLayoutImport(const QJsonObject& current,
                                            const QJsonObject& replacement,
                                            const LayoutSnapshotWriter& writer, bool saveCurrent) {
    const auto currentValidation = LayoutStore::validate(current);
    if (!currentValidation.success)
        return {currentValidation, false};
    const auto replacementValidation = LayoutStore::validate(replacement);
    if (!replacementValidation.success)
        return {replacementValidation, false};
    if (saveCurrent) {
        const auto saved = writer(current);
        if (!saved.success)
            return {saved, false};
    }
    return {writer(replacement), saveCurrent};
}

} // namespace xdstarter
