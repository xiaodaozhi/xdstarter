// tests/services/layout_import_tests.cpp

#include "services/layout_import.h"

#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "services/application_catalog.h"
#include "services/prototype_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

namespace {

using namespace xdstarter;

QJsonObject fixtureSnapshot(int version = LayoutStore::schemaVersion) {
    return {
        {QStringLiteral("schemaVersion"), version},
        {QStringLiteral("targets"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("import-target")},
                                {QStringLiteral("type"), QStringLiteral("prototype")},
                                {QStringLiteral("name"), QStringLiteral("原始工具")},
                                {QStringLiteral("symbol"), QStringLiteral("原")}}}},
        {QStringLiteral("groups"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("import-group")},
             {QStringLiteral("name"), QStringLiteral("导入分组")},
             {QStringLiteral("tiles"),
              QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("import-tile")},
                                     {QStringLiteral("targetId"), QStringLiteral("import-target")},
                                     {QStringLiteral("column"), 0},
                                     {QStringLiteral("row"), 0},
                                     {QStringLiteral("size"), QStringLiteral("standard")}}}}}}},
        {QStringLiteral("preferences"),
         QJsonObject{{QStringLiteral("theme"), QStringLiteral("light")}}}};
}

QJsonObject changedTile(QJsonObject snapshot, const QString& key, const QJsonValue& value) {
    auto groups = snapshot.value(QStringLiteral("groups")).toArray();
    auto group = groups[0].toObject();
    auto tiles = group.value(QStringLiteral("tiles")).toArray();
    auto tile = tiles[0].toObject();
    tile.insert(key, value);
    tiles[0] = tile;
    group.insert(QStringLiteral("tiles"), tiles);
    groups[0] = group;
    snapshot.insert(QStringLiteral("groups"), groups);
    return snapshot;
}

QByteArray bytes(const QJsonObject& snapshot) {
    return QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
}

bool writeBytes(const QString& path, const QByteArray& value) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(value) == value.size();
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

class LayoutImportTests final : public QObject {
    Q_OBJECT

  private slots:
    void selectedFileNeverRecoversItsBackup();
    void invalidSelectedInput_data();
    void invalidSelectedInput();
    void oldVersionsNormalizeWithoutWriting_data();
    void oldVersionsNormalizeWithoutWriting();
    void currentOverridesAndEmptyLayoutsArePreserved();
    void previewSnapshotDoesNotFollowSourceChanges();
    void bothSnapshotsAreValidatedBeforeAnyWrite_data();
    void bothSnapshotsAreValidatedBeforeAnyWrite();
    void writerFailurePreservesStepAndOrder_data();
    void writerFailurePreservesStepAndOrder();
    void unchangedCurrentWritesOnlyReplacement_data();
    void unchangedCurrentWritesOnlyReplacement();
    void successfulCommitBacksUpTheCurrentInMemoryState();
    void unchangedLegacyCurrentPreservesOriginalBackupBytes_data();
    void unchangedLegacyCurrentPreservesOriginalBackupBytes();
    void realBackupFailureStopsBeforeReplacement_data();
    void realBackupFailureStopsBeforeReplacement();
};

void LayoutImportTests::selectedFileNeverRecoversItsBackup() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("选中 布局 &.json"));
    const QByteArray selected("{broken-selected-json");
    const auto backup = bytes(fixtureSnapshot());
    QVERIFY(writeBytes(path, selected));
    QVERIFY(writeBytes(path + QStringLiteral(".bak"), backup));
    const auto recoverable = LayoutStore(path).load(fixtureSnapshot());
    QVERIFY(recoverable.success && recoverable.recoveredFromBackup);
    const auto preview = readLayoutImport(path);
    QVERIFY(!preview.success);
    QVERIFY(!preview.message.isEmpty());
    QVERIFY(preview.snapshot.isEmpty());
    QCOMPARE(readBytes(path), selected);
    QCOMPARE(readBytes(path + QStringLiteral(".bak")), backup);
}

void LayoutImportTests::invalidSelectedInput_data() {
    QTest::addColumn<QByteArray>("selected");
    QTest::newRow("not-object") << QByteArray("[]");
    QTest::newRow("over-one-mib") << QByteArray(
        static_cast<qsizetype>(LayoutStore::maximumFileBytes + 1), 'x');
    auto future = fixtureSnapshot(LayoutStore::schemaVersion + 1);
    QTest::newRow("future-version") << bytes(future);
    QTest::newRow("invalid-reference") << bytes(changedTile(
        fixtureSnapshot(), QStringLiteral("targetId"), QStringLiteral("unknown-target")));
    QTest::newRow("unknown-tile-field")
        << bytes(changedTile(fixtureSnapshot(), QStringLiteral("unknownField"), true));
    auto unknownRoot = fixtureSnapshot();
    unknownRoot.insert(QStringLiteral("unexpected"), true);
    QTest::newRow("unknown-root-field") << bytes(unknownRoot);
    auto overlap = fixtureSnapshot();
    auto groups = overlap.value(QStringLiteral("groups")).toArray();
    auto group = groups[0].toObject();
    auto tiles = group.value(QStringLiteral("tiles")).toArray();
    auto duplicate = tiles[0].toObject();
    duplicate.insert(QStringLiteral("id"), QStringLiteral("overlapping-tile"));
    tiles.append(duplicate);
    group.insert(QStringLiteral("tiles"), tiles);
    groups[0] = group;
    overlap.insert(QStringLiteral("groups"), groups);
    QTest::newRow("overlapping-tiles") << bytes(overlap);
    QTest::newRow("legacy-display-name-extension") << bytes(changedTile(
        fixtureSnapshot(2), QStringLiteral("displayName"), QStringLiteral("不允许伪装旧版本")));
    QTest::newRow("invalid-current-display-name") << bytes(
        changedTile(fixtureSnapshot(), QStringLiteral("displayName"), QStringLiteral(" padded ")));
}

void LayoutImportTests::invalidSelectedInput() {
    QFETCH(QByteArray, selected);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("selected.json"));
    QVERIFY(writeBytes(path, selected));
    const auto preview = readLayoutImport(path);
    QVERIFY(!preview.success);
    QVERIFY(!preview.message.isEmpty());
    QVERIFY(preview.snapshot.isEmpty());
    QCOMPARE(preview.sourceVersion, 0);
    QCOMPARE(preview.groupCount, 0);
    QCOMPARE(preview.tileCount, 0);
    QCOMPARE(preview.targetCount, 0);
    QCOMPARE(readBytes(path), selected);
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
}

void LayoutImportTests::oldVersionsNormalizeWithoutWriting_data() {
    QTest::addColumn<int>("version");
    QTest::newRow("schema-one") << 1;
    QTest::newRow("schema-two") << 2;
}

void LayoutImportTests::oldVersionsNormalizeWithoutWriting() {
    QFETCH(int, version);
    auto source = fixtureSnapshot(version);
    if (version == 2) {
        LaunchTarget target;
        target.type = QStringLiteral("shortcut");
        target.name = QStringLiteral("保留原始名称");
        target.symbol = QStringLiteral("保");
        target.shortcutPath = QStringLiteral("C:/XDStarterImportFixture/不存在的中文 路径.lnk");
        target.id = applicationTargetId(target.type, target.shortcutPath);
        target.iconKey = QStringLiteral("fixture-original-icon-key");
        source.insert(QStringLiteral("targets"), QJsonArray{applicationTargetJson(target)});
        source = changedTile(source, QStringLiteral("targetId"), target.id);
    }
    QVERIFY(LayoutStore::validate(source).success);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("旧版源.json"));
    const auto original = QByteArray(" \n") + QJsonDocument(source).toJson() + QByteArray("\n ");
    QVERIFY(writeBytes(path, original));
    const auto preview = readLayoutImport(path);
    QVERIFY2(preview.success, qPrintable(preview.message));
    auto normalized = source;
    normalized.insert(QStringLiteral("schemaVersion"), LayoutStore::schemaVersion);
    QCOMPARE(preview.snapshot, normalized);
    QCOMPARE(preview.sourceVersion, version);
    QCOMPARE(preview.groupCount, 1);
    QCOMPARE(preview.tileCount, 1);
    QCOMPARE(preview.targetCount, 1);
    QCOMPARE(preview.snapshot.value(QStringLiteral("targets")),
             source.value(QStringLiteral("targets")));
    QCOMPARE(readBytes(path), original);
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
}

void LayoutImportTests::currentOverridesAndEmptyLayoutsArePreserved() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("selected.json"));
    const auto source = changedTile(fixtureSnapshot(), QStringLiteral("displayName"),
                                    QStringLiteral("<b>独立显示名称</b>"));
    QVERIFY(writeBytes(path, bytes(source)));
    const auto named = readLayoutImport(path);
    QVERIFY2(named.success, qPrintable(named.message));
    QCOMPARE(named.snapshot, source);
    const TileGrid grid;
    PrototypeService empty(grid, {}, false);
    for (const int version : {2, LayoutStore::schemaVersion}) {
        auto snapshot = empty.snapshot();
        snapshot.insert(QStringLiteral("schemaVersion"), version);
        QVERIFY(writeBytes(path, bytes(snapshot)));
        const auto preview = readLayoutImport(path);
        QVERIFY2(preview.success, qPrintable(preview.message));
        QCOMPARE(preview.groupCount, 1);
        QCOMPARE(preview.tileCount, 0);
        QCOMPARE(preview.targetCount, 0);
        QCOMPARE(preview.snapshot, empty.snapshot());
    }
}

void LayoutImportTests::previewSnapshotDoesNotFollowSourceChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("selected.json"));
    const auto source =
        changedTile(fixtureSnapshot(), QStringLiteral("displayName"), QStringLiteral("已预览名称"));
    QVERIFY(writeBytes(path, bytes(source)));
    const auto preview = readLayoutImport(path);
    QVERIFY(preview.success);
    const auto altered = QByteArray("not the file that was previewed");
    QVERIFY(writeBytes(path, altered));
    QCOMPARE(preview.snapshot, source);
    QVector<QJsonObject> writes;
    const auto committed = commitLayoutImport(fixtureSnapshot(), preview.snapshot,
                                              [&writes](const QJsonObject& snapshot) {
                                                  writes.append(snapshot);
                                                  return LayoutResult{true, {}};
                                              });
    QVERIFY(committed.result.success && committed.currentSaved);
    QCOMPARE(writes.size(), 2);
    QCOMPARE(writes.last(), source);
    QCOMPARE(readBytes(path), altered);
}

void LayoutImportTests::bothSnapshotsAreValidatedBeforeAnyWrite_data() {
    QTest::addColumn<bool>("saveCurrent");
    QTest::newRow("dirty-current") << true;
    QTest::newRow("clean-current") << false;
}

void LayoutImportTests::bothSnapshotsAreValidatedBeforeAnyWrite() {
    QFETCH(bool, saveCurrent);
    auto invalid = fixtureSnapshot();
    invalid.insert(QStringLiteral("schemaVersion"), LayoutStore::schemaVersion + 1);
    int writes = 0;
    const auto writer = [&writes](const QJsonObject&) {
        ++writes;
        return LayoutResult{true, {}};
    };
    const auto currentInvalid = commitLayoutImport(invalid, fixtureSnapshot(), writer, saveCurrent);
    QVERIFY(!currentInvalid.result.success && !currentInvalid.currentSaved);
    QCOMPARE(writes, 0);
    const auto replacementInvalid =
        commitLayoutImport(fixtureSnapshot(), invalid, writer, saveCurrent);
    QVERIFY(!replacementInvalid.result.success && !replacementInvalid.currentSaved);
    QCOMPARE(writes, 0);
}

void LayoutImportTests::writerFailurePreservesStepAndOrder_data() {
    QTest::addColumn<int>("failedStep");
    QTest::newRow("current-save-fails") << 1;
    QTest::newRow("replacement-save-fails") << 2;
}

void LayoutImportTests::writerFailurePreservesStepAndOrder() {
    QFETCH(int, failedStep);
    const auto current = fixtureSnapshot();
    const auto replacement =
        changedTile(current, QStringLiteral("displayName"), QStringLiteral("替换名称"));
    QVector<QJsonObject> writes;
    const auto result = commitLayoutImport(
        current, replacement, [&writes, failedStep](const QJsonObject& snapshot) {
            writes.append(snapshot);
            return writes.size() == failedStep
                       ? LayoutResult{false, QStringLiteral("controlled writer failure"), true}
                       : LayoutResult{true, {}};
        });
    QVERIFY(!result.result.success);
    QCOMPARE(result.result.message, QStringLiteral("controlled writer failure"));
    QVERIFY(result.result.readOnly);
    QCOMPARE(result.currentSaved, failedStep == 2);
    QCOMPARE(writes.size(), failedStep);
    QCOMPARE(writes.first(), current);
    if (failedStep == 2)
        QCOMPARE(writes.last(), replacement);
}

void LayoutImportTests::unchangedCurrentWritesOnlyReplacement_data() {
    QTest::addColumn<bool>("writerSucceeds");
    QTest::newRow("replacement-succeeds") << true;
    QTest::newRow("replacement-fails") << false;
}

void LayoutImportTests::unchangedCurrentWritesOnlyReplacement() {
    QFETCH(bool, writerSucceeds);
    const auto current = fixtureSnapshot();
    const auto replacement =
        changedTile(current, QStringLiteral("displayName"), QStringLiteral("直接导入"));
    QVector<QJsonObject> writes;
    const auto result = commitLayoutImport(
        current, replacement,
        [&writes, writerSucceeds](const QJsonObject& snapshot) {
            writes.append(snapshot);
            return writerSucceeds
                       ? LayoutResult{true, {}}
                       : LayoutResult{false, QStringLiteral("replacement failure"), true};
        },
        false);
    QCOMPARE(result.result.success, writerSucceeds);
    QVERIFY(!result.currentSaved);
    QCOMPARE(writes.size(), 1);
    QCOMPARE(writes.first(), replacement);
    if (!writerSucceeds) {
        QCOMPARE(result.result.message, QStringLiteral("replacement failure"));
        QVERIFY(result.result.readOnly);
    }
}

void LayoutImportTests::successfulCommitBacksUpTheCurrentInMemoryState() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("active.json"));
    const LayoutStore store(path);
    const auto previousDisk = fixtureSnapshot();
    const auto current = changedTile(previousDisk, QStringLiteral("displayName"),
                                     QStringLiteral("尚未自动保存的编辑"));
    const auto replacement =
        changedTile(previousDisk, QStringLiteral("displayName"), QStringLiteral("导入替换"));
    QVERIFY(store.save(previousDisk).success);
    QByteArray expectedBackup;
    int writes = 0;
    const auto committed =
        commitLayoutImport(current, replacement, [&](const QJsonObject& snapshot) {
            ++writes;
            const auto result = store.save(snapshot);
            if (writes == 1 && result.success)
                expectedBackup = readBytes(path);
            return result;
        });
    QVERIFY2(committed.result.success, qPrintable(committed.result.message));
    QVERIFY(committed.currentSaved);
    QCOMPARE(writes, 2);
    QVERIFY(!expectedBackup.isEmpty());
    QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object(), replacement);
    QCOMPARE(readBytes(path + QStringLiteral(".bak")), expectedBackup);
    QCOMPARE(QJsonDocument::fromJson(expectedBackup).object(), current);
    QVERIFY(writeBytes(path, QByteArray("damaged after successful import")));
    const auto recovered = store.load(previousDisk);
    QVERIFY(recovered.success && recovered.recoveredFromBackup && !recovered.readOnly);
    QCOMPARE(recovered.snapshot, current);
}

void LayoutImportTests::unchangedLegacyCurrentPreservesOriginalBackupBytes_data() {
    QTest::addColumn<int>("version");
    QTest::newRow("schema-one") << 1;
    QTest::newRow("schema-two") << 2;
}

void LayoutImportTests::unchangedLegacyCurrentPreservesOriginalBackupBytes() {
    QFETCH(int, version);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("legacy-active.json"));
    const auto legacy = fixtureSnapshot(version);
    const auto originalBytes =
        QByteArray(" \n") + QJsonDocument(legacy).toJson() + QByteArray("\n ");
    QVERIFY(writeBytes(path, originalBytes));
    const auto current = fixtureSnapshot();
    const auto replacement =
        changedTile(current, QStringLiteral("displayName"), QStringLiteral("覆盖旧版布局"));
    const LayoutStore store(path);
    QVector<QJsonObject> writes;
    const auto committed = commitLayoutImport(
        current, replacement,
        [&](const QJsonObject& snapshot) {
            writes.append(snapshot);
            return store.save(snapshot);
        },
        false);
    QVERIFY2(committed.result.success, qPrintable(committed.result.message));
    QVERIFY(!committed.currentSaved);
    QCOMPARE(writes.size(), 1);
    QCOMPARE(writes.first(), replacement);
    QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object(), replacement);
    QCOMPARE(readBytes(path + QStringLiteral(".bak")), originalBytes);
    QCOMPARE(QJsonDocument::fromJson(readBytes(path + QStringLiteral(".bak"))).object(), legacy);
    QVERIFY(writeBytes(path, QByteArray("damage imported primary")));
    const auto recovered = store.load(current);
    QVERIFY(recovered.success && recovered.recoveredFromBackup && !recovered.readOnly);
    QCOMPARE(recovered.snapshot, legacy);
}

void LayoutImportTests::realBackupFailureStopsBeforeReplacement_data() {
    QTest::addColumn<bool>("saveCurrent");
    QTest::newRow("dirty-flush-backup-fails") << true;
    QTest::newRow("clean-replacement-backup-fails") << false;
}

void LayoutImportTests::realBackupFailureStopsBeforeReplacement() {
    QFETCH(bool, saveCurrent);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("active.json"));
    const auto diskBefore = bytes(fixtureSnapshot());
    QVERIFY(writeBytes(path, diskBefore));
    QVERIFY(QDir().mkdir(path + QStringLiteral(".bak")));
    const auto current = changedTile(fixtureSnapshot(), QStringLiteral("displayName"),
                                     QStringLiteral("当前未保存编辑"));
    const auto replacement = changedTile(fixtureSnapshot(), QStringLiteral("displayName"),
                                         QStringLiteral("不能提交的导入"));
    const LayoutStore store(path);
    int writes = 0;
    const auto failed = commitLayoutImport(
        current, replacement,
        [&](const QJsonObject& snapshot) {
            ++writes;
            return store.save(snapshot);
        },
        saveCurrent);
    QVERIFY(!failed.result.success && !failed.currentSaved);
    QVERIFY(!failed.result.message.isEmpty());
    QCOMPARE(writes, 1);
    QCOMPARE(readBytes(path), diskBefore);
    QVERIFY(QFileInfo(path + QStringLiteral(".bak")).isDir());
    QVERIFY(!QFile::exists(path + QStringLiteral(".lock")));
}

} // namespace

QTEST_GUILESS_MAIN(LayoutImportTests)

#include "layout_import_tests.moc"
