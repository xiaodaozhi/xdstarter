#include "services/layout_store.h"
#include "services/prototype_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>

#include <iostream>
#include <string_view>

namespace {

class TestContext {
  public:
    void expect(bool condition, std::string_view description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }
    int failures = 0;
};

bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QJsonObject changeTile(QJsonObject snapshot, const QString& key, const QJsonValue& value) {
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

QJsonObject nearLimitSnapshot() {
    QJsonArray targets;
    QJsonArray tiles;
    for (int index = 0; index < 512; ++index) {
        const auto targetId =
            QStringLiteral("target-%1-").arg(index).leftJustified(128, QLatin1Char('t'));
        const auto tileId =
            QStringLiteral("tile-%1-").arg(index).leftJustified(128, QLatin1Char('x'));
        targets.append(QJsonObject{{QStringLiteral("id"), targetId},
                                   {QStringLiteral("type"), QStringLiteral("prototype")},
                                   {QStringLiteral("name"), QString(500, QChar(0x4e2d))},
                                   {QStringLiteral("symbol"), QString(16, QLatin1Char('S'))}});
        tiles.append(QJsonObject{{QStringLiteral("id"), tileId},
                                 {QStringLiteral("targetId"), targetId},
                                 {QStringLiteral("column"), index % 10},
                                 {QStringLiteral("row"), index / 10},
                                 {QStringLiteral("size"), QStringLiteral("small")}});
    }
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("targets"), targets},
            {QStringLiteral("groups"),
             QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("large")},
                                    {QStringLiteral("name"), QStringLiteral("Large prototype")},
                                    {QStringLiteral("tiles"), tiles}}}},
            {QStringLiteral("preferences"), QJsonObject{}}};
}

} // namespace

int main() {
    using namespace xdstarter;
    TestContext test;
    const TileGrid grid;
    PrototypeService prototype(grid);
    const auto defaults = prototype.snapshot();
    QTemporaryDir directory;
    test.expect(directory.isValid(), "temporary storage directory is available");
    if (!directory.isValid()) {
        return 1;
    }
    const auto path = directory.filePath(QStringLiteral("state/layout.json"));
    const LayoutStore store(path);
    const auto firstRun = store.load(defaults);
    test.expect(firstRun.success && firstRun.usedDefaults && !firstRun.readOnly &&
                    firstRun.snapshot == defaults && !QFile::exists(path),
                "missing config uses explicit prototype defaults without writing during load");
    test.expect(store.save(defaults).success, "initial snapshot saves atomically");
    test.expect(QJsonDocument::fromJson(readBytes(path + QStringLiteral(".bak"))).object() ==
                    defaults,
                "the first successful save immediately creates a validated backup");
    const auto firstSavedBytes = readBytes(path);
    test.expect(writeBytes(path, QByteArray("broken after the first save")) &&
                    store.load(defaults).recoveredFromBackup &&
                    store.load(defaults).snapshot == defaults && writeBytes(path, firstSavedBytes),
                "a damaged first config can recover the first saved state from its backup");
    test.expect(prototype.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 2)
                        .success &&
                    prototype.resizeTile(QStringLiteral("work-standard-1"), TileSize::Wide).success,
                "test edit produces a valid moved and resized snapshot");
    auto edited = prototype.snapshot();
    edited.insert(QStringLiteral("preferences"),
                  QJsonObject{{QStringLiteral("theme"), QStringLiteral("light")}});
    test.expect(store.save(edited).success, "edited snapshot saves with a previous valid backup");
    const PrototypeService restarted(grid, path);
    test.expect(restarted.loadResult().success && !restarted.loadResult().usedDefaults &&
                    restarted.snapshot() == edited && restarted.configurationPath() == path,
                "restart restores positions, sizes and theme from the authoritative snapshot");
    test.expect(QJsonDocument::fromJson(readBytes(path + QStringLiteral(".bak"))).object() ==
                    defaults,
                "backup contains the previous fully validated state");
    QLockFile competingSave(path + QStringLiteral(".lock"));
    test.expect(competingSave.tryLock(0), "cooperating save lock fixture acquires the layout lock");
    const auto beforeLockedPrimary = readBytes(path);
    const auto beforeLockedBackup = readBytes(path + QStringLiteral(".bak"));
    const auto lockedSave = store.save(defaults);
    test.expect(!lockedSave.success && !lockedSave.readOnly && !lockedSave.message.isEmpty() &&
                    readBytes(path) == beforeLockedPrimary &&
                    readBytes(path + QStringLiteral(".bak")) == beforeLockedBackup,
                "a competing writer fails promptly without changing primary or backup");
    competingSave.unlock();

    const QByteArray corrupt("{ damaged JSON");
    test.expect(writeBytes(path, corrupt), "corrupt primary fixture writes");
    const auto recovered = store.load(defaults);
    test.expect(recovered.success && recovered.recoveredFromBackup && !recovered.usedDefaults &&
                    recovered.snapshot == defaults && !recovered.message.isEmpty() &&
                    readBytes(path) == corrupt,
                "corrupt primary recovers a valid backup and preserves the damaged original");
    test.expect(store.save(edited).success &&
                    QJsonDocument::fromJson(readBytes(path + QStringLiteral(".bak"))).object() ==
                        defaults,
                "saving recovered edits never turns a corrupt primary into the valid backup");
    const auto preserved = QDir(QFileInfo(path).absolutePath())
                               .entryList({QStringLiteral("layout.json.corrupt-*")}, QDir::Files);
    test.expect(preserved.size() == 1 && readBytes(QFileInfo(path).absolutePath() +
                                                   QLatin1Char('/') + preserved[0]) == corrupt,
                "overwritten damaged config is retained as a diagnostic artifact");

    auto future = edited;
    future.insert(QStringLiteral("schemaVersion"), 3);
    const auto futureBytes = QJsonDocument(future).toJson();
    test.expect(writeBytes(path, futureBytes), "future schema fixture writes");
    const auto futureLoad = store.load(defaults);
    const auto futureSave = store.save(edited);
    test.expect(futureLoad.readOnly && futureLoad.recoveredFromBackup && !futureSave.success &&
                    futureSave.readOnly && readBytes(path) == futureBytes,
                "future schema is read-only and save rechecks the actual on-disk version");
    PrototypeService futureService(grid, path);
    const auto futureBefore = futureService.snapshot();
    test.expect(
        futureService.isReadOnly() && futureService.pinApplication(QJsonObject{}).readOnly &&
            futureService.unpinTile(QStringLiteral("work-standard-1")).readOnly &&
            !futureService
                 .moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 2)
                 .success &&
            !futureService.resizeTile(QStringLiteral("work-standard-1"), TileSize::Small).success &&
            futureService.snapshot() == futureBefore,
        "service refuses in-memory edits when a future version was loaded");

    test.expect(writeBytes(path, QJsonDocument(edited).toJson()),
                "valid primary is restored for write failure");
    test.expect(QFile::remove(path + QStringLiteral(".bak")) &&
                    QDir().mkdir(path + QStringLiteral(".bak")),
                "backup destination is blocked by a directory");
    const auto beforeFailedSave = readBytes(path);
    test.expect(!store.save(defaults).success && readBytes(path) == beforeFailedSave,
                "backup write failure never damages the previous authoritative config");
    test.expect(QDir().rmdir(path + QStringLiteral(".bak")), "blocked backup fixture cleans up");
    test.expect(writeBytes(path, corrupt), "unrecoverable corruption fixture writes");
    const auto fallback = store.load(defaults);
    test.expect(!fallback.success && fallback.usedDefaults && !fallback.message.isEmpty() &&
                    fallback.snapshot == defaults && readBytes(path) == corrupt,
                "unrecoverable corruption falls back with a visible reason and preserves input");

    for (const auto& invalid :
         {changeTile(defaults, QStringLiteral("column"), 0.5),
          changeTile(defaults, QStringLiteral("row"), -1),
          changeTile(defaults, QStringLiteral("row"), LayoutStore::maximumRows),
          changeTile(defaults, QStringLiteral("row"), LayoutStore::maximumRows - 1),
          changeTile(defaults, QStringLiteral("size"), QStringLiteral("giant")),
          changeTile(defaults, QStringLiteral("targetId"), QStringLiteral("missing")),
          changeTile(defaults, QStringLiteral("column"), 9),
          changeTile(defaults, QStringLiteral("column"), 2),
          changeTile(defaults, QStringLiteral("id"), QStringLiteral("work-standard-long"))}) {
        test.expect(!LayoutStore::validate(invalid).success,
                    "invalid integer, bound, size, reference, collision or duplicate rejects");
    }
    auto invalidVersion = defaults;
    invalidVersion.insert(QStringLiteral("schemaVersion"), 1.5);
    test.expect(!LayoutStore::validate(invalidVersion).success,
                "fractional schema version rejects rather than truncating");
    auto wrongType = defaults;
    wrongType.insert(QStringLiteral("groups"), QStringLiteral("not an array"));
    test.expect(!LayoutStore::validate(wrongType).success, "wrong root data type rejects");
    auto duplicateTarget = defaults;
    auto targets = duplicateTarget.value(QStringLiteral("targets")).toArray();
    targets.append(targets[0]);
    duplicateTarget.insert(QStringLiteral("targets"), targets);
    test.expect(!LayoutStore::validate(duplicateTarget).success,
                "duplicate target identity rejects");
    auto duplicateGroup = defaults;
    auto groups = duplicateGroup.value(QStringLiteral("groups")).toArray();
    groups.append(groups[0]);
    duplicateGroup.insert(QStringLiteral("groups"), groups);
    test.expect(!LayoutStore::validate(duplicateGroup).success, "duplicate group identity rejects");
    test.expect(writeBytes(path, QByteArray(LayoutStore::maximumFileBytes + 1, ' ')),
                "oversized input fixture writes");
    test.expect(store.load(defaults).usedDefaults && !store.load(defaults).success,
                "read is bounded before parsing oversized input");
    test.expect(
        store.load(defaults).readOnly && store.save(defaults).readOnly,
        "oversized input is read-only rather than being overwritten as ordinary corruption");
    auto oversizedFuture = future;
    oversizedFuture.insert(QStringLiteral("futurePayload"),
                           QString(LayoutStore::maximumFileBytes, QLatin1Char('x')));
    const auto oversizedFutureBytes = QJsonDocument(oversizedFuture).toJson(QJsonDocument::Compact);
    test.expect(writeBytes(path, oversizedFutureBytes), "oversized future schema fixture writes");
    const auto oversizedFutureLoad = store.load(defaults);
    const auto oversizedFutureSave = store.save(defaults);
    test.expect(oversizedFutureLoad.readOnly && !oversizedFutureSave.success &&
                    oversizedFutureSave.readOnly && readBytes(path) == oversizedFutureBytes,
                "oversized future-schema bytes remain untouched without unbounded parsing");
    test.expect(writeBytes(path, QJsonDocument(defaults).toJson(QJsonDocument::Compact)) &&
                    writeBytes(path + QStringLiteral(".bak"), oversizedFutureBytes),
                "oversized backup fixture writes beside a valid primary");
    const auto primaryBeforeBlockedBackup = readBytes(path);
    test.expect(
        store.load(defaults).readOnly && !store.load(defaults).usedDefaults &&
            store.save(edited).readOnly && readBytes(path) == primaryBeforeBlockedBackup &&
            readBytes(path + QStringLiteral(".bak")) == oversizedFutureBytes,
        "an oversized backup also blocks overwrite while the valid primary remains visible");
    test.expect(!LayoutStore(QString{}).save(defaults).success,
                "empty path never writes an accidental working-directory config");
    const auto large = nearLimitSnapshot();
    test.expect(QJsonDocument(large).toJson(QJsonDocument::Compact).size() <=
                        LayoutStore::maximumFileBytes &&
                    QJsonDocument(large).toJson(QJsonDocument::Indented).size() >
                        LayoutStore::maximumFileBytes &&
                    LayoutStore::validate(large).success,
                "near-limit fixture distinguishes compact and indented byte lengths");
    const auto largePath = directory.filePath(QStringLiteral("near-limit.json"));
    const LayoutStore largeStore(largePath);
    const auto largeSaved = largeStore.save(large);
    const auto largeLoaded = largeStore.load(defaults);
    test.expect(
        largeSaved.success && QFileInfo(largePath).size() <= LayoutStore::maximumFileBytes &&
            largeLoaded.success && !largeLoaded.usedDefaults && largeLoaded.snapshot == large,
        "a valid near-limit save is bounded by the same encoding used on restart");

    auto legacySnapshot = defaults;
    legacySnapshot.insert(QStringLiteral("schemaVersion"), 1);
    const auto legacyPath = directory.filePath(QStringLiteral("legacy.json"));
    const QByteArray legacyBytes = QByteArray(" \n") +
                                   QJsonDocument(legacySnapshot).toJson(QJsonDocument::Indented) +
                                   QByteArray("\n  ");
    test.expect(writeBytes(legacyPath, legacyBytes) &&
                    LayoutStore::validate(legacySnapshot).success,
                "a version-one sample snapshot with original formatting remains valid");
    const PrototypeService migrated(grid, legacyPath, false);
    auto expectedMigration = legacySnapshot;
    expectedMigration.insert(QStringLiteral("schemaVersion"), LayoutStore::schemaVersion);
    test.expect(
        migrated.loadResult().success && !migrated.isReadOnly() &&
            !migrated.loadResult().message.isEmpty() && migrated.snapshot() == expectedMigration &&
            readBytes(legacyPath) == legacyBytes,
        "loading version one migrates only memory without changing sample identities or disk");
    test.expect(LayoutStore(legacyPath).save(migrated.snapshot()).success &&
                    readBytes(legacyPath + QStringLiteral(".bak")) == legacyBytes &&
                    QJsonDocument::fromJson(readBytes(legacyPath)).object() == expectedMigration,
                "first version-two save preserves the exact original version-one bytes as backup");

    PrototypeService realApplications(grid, {}, false);
    const QJsonObject application{
        {QStringLiteral("id"), QStringLiteral("real-shortcut")},
        {QStringLiteral("type"), QStringLiteral("shortcut")},
        {QStringLiteral("name"), QStringLiteral("Gone after pinning")},
        {QStringLiteral("symbol"), QStringLiteral("G")},
        {QStringLiteral("shortcutPath"), QStringLiteral("C:\\Missing\\Gone.LNK")},
        {QStringLiteral("iconKey"), QStringLiteral("real-shortcut")}};
    const auto pinned = realApplications.pinApplication(application);
    const auto applicationsPath = directory.filePath(QStringLiteral("applications.json"));
    const LayoutStore applicationsStore(applicationsPath);
    test.expect(pinned.success && applicationsStore.save(realApplications.snapshot()).success,
                "a structurally valid shortcut can be saved even when its file is missing");
    const PrototypeService applicationRestart(grid, applicationsPath, false);
    test.expect(
        applicationRestart.snapshot() == realApplications.snapshot() &&
            applicationRestart.targetForTile(pinned.tileId) == application &&
            applicationRestart.isApplicationPinned(QStringLiteral("real-shortcut")),
        "restart preserves the missing application target, pin state and stable tile identity");
    test.expect(realApplications.unpinTile(pinned.tileId).success &&
                    applicationsStore.save(realApplications.snapshot()).success,
                "unpinning the last real application saves an empty group");
    const PrototypeService emptyRestart(grid, applicationsPath, false);
    test.expect(emptyRestart.loadResult().success && !emptyRestart.loadResult().usedDefaults &&
                    emptyRestart.snapshot() == realApplications.snapshot() &&
                    emptyRestart.previewGroups().first().tiles.isEmpty(),
                "an empty user layout survives restart without returning prototype samples");

    const auto targetSnapshot = [&](const QJsonObject& target) {
        auto snapshot = realApplications.snapshot();
        snapshot.insert(QStringLiteral("targets"), QJsonArray{target});
        return snapshot;
    };
    auto uncApplication = application;
    uncApplication.insert(QStringLiteral("shortcutPath"),
                          QStringLiteral("\\\\server\\share\\Programs\\Remote.lnk"));
    test.expect(LayoutStore::validate(targetSnapshot(uncApplication)).success,
                "UNC redirected start-menu shortcut metadata is structurally supported");
    for (const auto& invalidPath :
         {QStringLiteral("relative.lnk"), QStringLiteral("C:relative.lnk"),
          QStringLiteral("shell:AppsFolder\\App.lnk"), QStringLiteral("C:\\App.exe"),
          QStringLiteral("C:\\App.lnk --argument"), QStringLiteral("C:\\stream:App.lnk"),
          QStringLiteral("C:\\Wildcard*.lnk"), QStringLiteral("C:\\Line\nBreak.lnk")}) {
        auto invalid = application;
        invalid.insert(QStringLiteral("shortcutPath"), invalidPath);
        test.expect(
            !LayoutStore::validate(targetSnapshot(invalid)).success,
            "launch data rejects relative paths, protocols, raw arguments and invalid paths");
    }
    auto packagedApplication = application;
    packagedApplication.remove(QStringLiteral("shortcutPath"));
    packagedApplication.insert(QStringLiteral("type"), QStringLiteral("packaged"));
    packagedApplication.insert(QStringLiteral("aumid"),
                               QStringLiteral("Example.Package_123456789!Main.App"));
    test.expect(LayoutStore::validate(targetSnapshot(packagedApplication)).success,
                "packaged launch identity validates without checking installation state");
    for (const auto& invalidAumid :
         {QStringLiteral("shell:AppsFolder\\Example!App"), QStringLiteral("Example!App /argument"),
          QStringLiteral("Example!App!Other"), QStringLiteral("Example!App\n"),
          QStringLiteral("ExampleWithoutApplication")}) {
        auto invalid = packagedApplication;
        invalid.insert(QStringLiteral("aumid"), invalidAumid);
        test.expect(!LayoutStore::validate(targetSnapshot(invalid)).success,
                    "packaged target rejects shell prefixes, commands and unsupported identities");
    }
    auto mixedTarget = packagedApplication;
    mixedTarget.insert(QStringLiteral("shortcutPath"), QStringLiteral("C:\\App.lnk"));
    test.expect(!LayoutStore::validate(targetSnapshot(mixedTarget)).success,
                "a launch target cannot mix shortcut and packaged launch fields");
    auto unknownTargetField = application;
    unknownTargetField.insert(QStringLiteral("arguments"), QStringLiteral("untrusted"));
    test.expect(!LayoutStore::validate(targetSnapshot(unknownTargetField)).success,
                "unknown target fields never become an implicit command-line escape hatch");
    if (test.failures == 0) {
        std::cout << "All layout persistence checks passed.\n";
    }
    return test.failures == 0 ? 0 : 1;
}
