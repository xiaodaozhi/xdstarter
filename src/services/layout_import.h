// src/services/layout_import.h

#pragma once

#include "services/layout_store.h"

#include <QJsonObject>
#include <QString>

#include <functional>

namespace xdstarter {

struct LayoutImportPreview {
    bool success = false;
    QString message;
    QJsonObject snapshot;
    int sourceVersion = 0;
    int groupCount = 0;
    int tileCount = 0;
    int targetCount = 0;
};

// Reads precisely the selected file, never its backup or the current configuration.
[[nodiscard]] LayoutImportPreview readLayoutImport(const QString& path);

struct LayoutImportCommitResult {
    LayoutResult result;
    bool currentSaved = false;
};

using LayoutSnapshotWriter = std::function<LayoutResult(const QJsonObject&)>;

// Run on the existing save worker. Publish replacement only when result.success is true.
[[nodiscard]] LayoutImportCommitResult commitLayoutImport(const QJsonObject& current,
                                                          const QJsonObject& replacement,
                                                          const LayoutSnapshotWriter& writer,
                                                          bool saveCurrent = true);

} // namespace xdstarter
