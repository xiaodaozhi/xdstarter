#include "services/prototype_service.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
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

bool sameRectangle(const xdstarter::TileRect& first, const xdstarter::TileRect& second) {
    return first.x == second.x && first.y == second.y && first.width == second.width &&
           first.height == second.height;
}

bool overlap(const xdstarter::TileRect& first, const xdstarter::TileRect& second) {
    return first.x < second.x + second.width && second.x < first.x + first.width &&
           first.y < second.y + second.height && second.y < first.y + first.height;
}

const xdstarter::PreviewTile* findTile(const QVector<xdstarter::PreviewGroup>& groups,
                                       const char* tileId) {
    for (const auto& group : groups) {
        for (const auto& tile : group.tiles) {
            if (tile.id == QLatin1String(tileId)) {
                return &tile;
            }
        }
    }
    return nullptr;
}

void expectNeighbor(TestContext& test, const xdstarter::PrototypeService& service, const char* from,
                    const char* direction, const char* expected, std::string_view description) {
    test.expect(service.neighborTileId(QString::fromLatin1(from), QString::fromLatin1(direction)) ==
                    QLatin1String(expected),
                description);
}

void checkGroupManagement(TestContext& test, const xdstarter::TileGrid& grid) {
    using namespace xdstarter;
    PrototypeService service(grid);
    const auto initialGroups = service.previewGroups();
    const auto initial = service.snapshot();
    const auto renamed = service.renameGroup(QStringLiteral("work"), QStringLiteral("  办公工具  "));
    auto expected = initial;
    auto expectedGroups = expected.value(QStringLiteral("groups")).toArray();
    auto work = expectedGroups[0].toObject();
    work.insert(QStringLiteral("name"), QStringLiteral("办公工具"));
    expectedGroups[0] = work;
    expected.insert(QStringLiteral("groups"), expectedGroups);
    test.expect(renamed.success && renamed.groupId == QLatin1String("work") &&
                    renamed.tileId.isEmpty() && service.snapshot() == expected,
                "rename trims a group name while preserving tile and target snapshots");
    const auto sameName = service.renameGroup(QStringLiteral("work"), QStringLiteral("办公工具"));
    test.expect(sameName.success && service.snapshot() == expected,
                "renaming to the same name succeeds without a layout change");

    test.expect(service.moveGroupBy(QStringLiteral("daily"), -1).success,
                "a group moves one position toward the start");
    expectedGroups = QJsonArray{expectedGroups[1], expectedGroups[0], expectedGroups[2]};
    expected.insert(QStringLiteral("groups"), expectedGroups);
    test.expect(service.snapshot() == expected,
                "group ordering changes only the ordered group array");
    expectNeighbor(test, service, "work-standard-1", "up", "daily-wide",
                   "up navigation follows the newly ordered preceding group");
    expectNeighbor(test, service, "work-standard-1", "down", "entertainment-wide",
                   "down navigation follows the newly ordered following group");
    for (const auto& before : initialGroups) {
        for (const auto& after : service.previewGroups()) {
            if (before.id != after.id) {
                continue;
            }
            test.expect(before.width == after.width && before.height == after.height &&
                            before.tiles.size() == after.tiles.size(),
                        "rename and reorder keep each group's existing geometry");
            for (qsizetype index = 0; index < before.tiles.size(); ++index) {
                const auto& first = before.tiles[index];
                const auto& second = after.tiles[index];
                test.expect(first.id == second.id && first.targetId == second.targetId &&
                                first.column == second.column && first.row == second.row &&
                                first.size == second.size &&
                                sameRectangle(first.rectangle, second.rectangle),
                            "group edits keep tile identity, target, placement and geometry");
            }
        }
    }

    const auto created = service.createGroup(QStringLiteral("  办公工具  "));
    const auto createdGroups = service.previewGroups();
    test.expect(created.success && !created.groupId.isEmpty() && created.tileId.isEmpty() &&
                    created.groupId != QLatin1String("work") && createdGroups.size() == 4 &&
                    createdGroups.last().id == created.groupId &&
                    createdGroups.last().name == QStringLiteral("办公工具") &&
                    createdGroups.last().tiles.isEmpty() && createdGroups.last().width == 552 &&
                    createdGroups.last().height == 216,
                "create appends an independent empty group with a trimmed, non-unique name");
    test.expect(service.moveGroupBy(created.groupId, -1).success,
                "new empty groups can be reordered");
    expectNeighbor(test, service, "work-standard-1", "down", "entertainment-wide",
                   "navigation skips a newly created empty intermediate group");

    const auto beforeInvalid = service.snapshot();
    for (const auto& invalidName :
         {QString{}, QStringLiteral("   "),
          QString(LayoutStore::maximumGroupNameLength + 1, QChar(0x4e2d)),
          QStringLiteral("制\t表"), QStringLiteral("换\n行"), QStringLiteral("\rleading"),
          QStringLiteral("null\0character"), QStringLiteral("line\u2028separator"),
          QStringLiteral("paragraph\u2029separator")}) {
        test.expect(!service.createGroup(invalidName).success &&
                        !service.renameGroup(QStringLiteral("work"), invalidName).success &&
                        service.snapshot() == beforeInvalid,
                    "empty, oversized and control-containing names fail atomically");
    }
    test.expect(!service.renameGroup(QStringLiteral("missing"), QStringLiteral("name")).success &&
                    !service.moveGroupBy(QStringLiteral("missing"), 1).success &&
                    !service.removeGroup(QStringLiteral("missing")).success &&
                    !service.moveGroupBy(QStringLiteral("daily"), -1).success &&
                    !service.moveGroupBy(QStringLiteral("entertainment"), 1).success &&
                    !service.moveGroupBy(QStringLiteral("work"), 0).success &&
                    !service.moveGroupBy(QStringLiteral("work"), 2).success &&
                    !service.removeGroup(QStringLiteral("work")).success &&
                    service.snapshot() == beforeInvalid,
                "unknown ids, boundary moves, invalid deltas and populated deletes preserve state");
    test.expect(service.removeGroup(created.groupId).success && service.snapshot() == expected,
                "deleting an empty group preserves the remaining group and target snapshots");
    test.expect(service.moveGroupBy(QStringLiteral("daily"), 1).success,
                "a group moves one position toward the end");

    const auto destination = service.createGroup(QStringLiteral("固定目标"));
    const auto application = QJsonObject{
        {QStringLiteral("id"), QStringLiteral("group-test-target")},
        {QStringLiteral("type"), QStringLiteral("shortcut")},
        {QStringLiteral("name"), QStringLiteral("分组应用")},
        {QStringLiteral("symbol"), QStringLiteral("G")},
        {QStringLiteral("shortcutPath"), QStringLiteral("C:\\Missing\\GroupTest.lnk")}};
    const auto pinned = service.pinApplication(application, destination.groupId);
    test.expect(destination.success && pinned.success &&
                    service.previewGroups().last().tiles.first().id == pinned.tileId,
                "a newly created group accepts application pinning");
    test.expect(service.moveTile(pinned.tileId, QStringLiteral("work"), 0, 2).success &&
                    service.removeGroup(destination.groupId).success &&
                    service.isApplicationPinned(QStringLiteral("group-test-target")) &&
                    service.targetForTile(pinned.tileId) == application,
                "moving a tile out permits group removal without changing its launch target");

    PrototypeService limits(grid, {}, false);
    const auto lastGroup = limits.previewGroups().first().id;
    const auto beforeLastDelete = limits.snapshot();
    test.expect(!limits.removeGroup(lastGroup).success && limits.snapshot() == beforeLastDelete,
                "the final empty group cannot be deleted");
    const auto longName = QString(LayoutStore::maximumGroupNameLength, QChar(0x4e2d));
    test.expect(limits.renameGroup(lastGroup, longName).success &&
                    limits.previewGroups().first().name == longName,
                "the 128 UTF-16-unit Chinese group-name boundary is accepted");
    const auto emoji = QString::fromUtf8("\xf0\x9f\x98\x80");
    test.expect(limits.renameGroup(lastGroup, emoji.repeated(64)).success,
                "64 supplementary characters fit the 128 UTF-16-unit name limit");
    const auto beforeLongName = limits.snapshot();
    test.expect(!limits.renameGroup(lastGroup, emoji.repeated(65)).success &&
                    limits.snapshot() == beforeLongName,
                "the name limit counts UTF-16 units rather than Unicode scalars");
    QSet<QString> generatedIds{lastGroup};
    for (int count = 1; count < LayoutStore::maximumGroups; ++count) {
        const auto added = limits.createGroup(longName);
        test.expect(added.success && !added.groupId.isEmpty() &&
                        !generatedIds.contains(added.groupId),
                    "each group within the limit receives a unique stable id");
        generatedIds.insert(added.groupId);
    }
    const auto maximumSnapshot = limits.snapshot();
    test.expect(limits.previewGroups().size() == LayoutStore::maximumGroups &&
                    LayoutStore::validate(maximumSnapshot).success &&
                    !limits.createGroup(QStringLiteral("overflow")).success &&
                    limits.snapshot() == maximumSnapshot,
                "32 groups are valid and an additional group fails without changing the layout");
    auto excessiveGroups = maximumSnapshot;
    auto excessiveArray = excessiveGroups.value(QStringLiteral("groups")).toArray();
    auto excess = excessiveArray.last().toObject();
    excess.insert(QStringLiteral("id"), QStringLiteral("thirty-third-group"));
    excessiveArray.append(excess);
    excessiveGroups.insert(QStringLiteral("groups"), excessiveArray);
    test.expect(!LayoutStore::validate(excessiveGroups).success,
                "the persisted group-count boundary uses the same 32-group limit");
    PrototypeService restored(grid, {}, false);
    test.expect(restored.restoreSnapshot(maximumSnapshot).success &&
                    restored.snapshot() == maximumSnapshot,
                "group ids, duplicate names and order survive snapshot restoration");

    auto legacyNames = beforeLastDelete;
    auto legacyGroups = legacyNames.value(QStringLiteral("groups")).toArray();
    auto legacyGroup = legacyGroups[0].toObject();
    legacyGroup.insert(QStringLiteral("name"), QStringLiteral("Old\nName"));
    legacyGroups[0] = legacyGroup;
    legacyNames.insert(QStringLiteral("groups"), legacyGroups);
    test.expect(LayoutStore::validate(legacyNames).success &&
                    restored.restoreSnapshot(legacyNames).success &&
                    restored.snapshot() == legacyNames,
                "new edit-name rules do not invalidate previously legal stored group names");
}

void checkTileDisplayNames(TestContext& test, const xdstarter::TileGrid& grid) {
    using namespace xdstarter;
    PrototypeService service(grid);
    const auto before = service.snapshot();
    const auto beforeTiles = service.previewGroups();
    const auto id = QStringLiteral("work-standard-1");
    const auto originalTarget = service.targetForTile(id);
    test.expect(service.renameTile(id, QStringLiteral("  <b>办公工具</b>  ")).success,
                "display name trims independently from the original target");
    const auto renamed = service.snapshot();
    test.expect(renamed.value(QStringLiteral("targets")) == before.value(QStringLiteral("targets")) &&
                    service.targetForTile(id) == originalTarget,
                "display editing does not mutate target identity or any launch descriptor");
    const auto afterTiles = service.previewGroups();
    const auto* tile = findTile(afterTiles, "work-standard-1");
    const auto* original = findTile(beforeTiles, "work-standard-1");
    test.expect(tile && original && tile->name == QStringLiteral("<b>办公工具</b>") &&
                    tile->displayName == tile->name && tile->targetId == original->targetId &&
                    tile->size == original->size && sameRectangle(tile->rectangle, original->rectangle),
                "effective label changes while shared geometry and target stay identical");
    test.expect(service.renameTile(id, QStringLiteral("<b>办公工具</b>")).success &&
                    service.snapshot() == renamed,
                "same display name is a no-op");
    for (const auto& invalid : {QString{}, QStringLiteral("  "), QString(129, QLatin1Char('x')),
                                QStringLiteral("x\ny"), QStringLiteral("x\ty"),
                                QString(QChar(0xD800)), QString(QChar(0x2028))}) {
        test.expect(!service.renameTile(id, invalid).success && service.snapshot() == renamed,
                    "invalid or oversized display draft never changes the live snapshot");
    }
    const auto emoji = QString::fromUtf8("😀");
    test.expect(service.renameTile(id, emoji.repeated(64)).success &&
                    !service.renameTile(id, emoji.repeated(65)).success,
                "128 UTF-16 units accept a complete emoji boundary without truncation");
    test.expect(!service.renameTile(QStringLiteral("missing-tile"), QStringLiteral("name")).success &&
                    !service.resetTileDisplayName(QStringLiteral("missing-tile")).success,
                "missing tile names cannot create a target or tile");
    test.expect(service.resetTileDisplayName(id).success && service.snapshot() == before,
                "reset removes only the optional display override and restores the original label");
    test.expect(service.resetTileDisplayName(id).success && service.snapshot() == before,
                "reset of an original name is a no-op");

    auto duplicates = before;
    auto groups = duplicates.value(QStringLiteral("groups")).toArray();
    auto group = groups[0].toObject();
    auto entries = group.value(QStringLiteral("tiles")).toArray();
    auto second = entries[1].toObject();
    second.insert(QStringLiteral("targetId"), entries[0].toObject().value(QStringLiteral("targetId")));
    entries[1] = second;
    group.insert(QStringLiteral("tiles"), entries); groups[0] = group;
    duplicates.insert(QStringLiteral("groups"), groups);
    test.expect(service.restoreSnapshot(duplicates).success &&
                    service.renameTile(id, QStringLiteral("第一入口")).success &&
                    service.renameTile(QStringLiteral("work-standard-long"), QStringLiteral("第二入口")).success,
                "restored shared targets allow independent per-tile overrides");
    const auto duplicateTiles = service.previewGroups();
    const auto* first = findTile(duplicateTiles, "work-standard-1");
    const auto* secondTile = findTile(duplicateTiles, "work-standard-long");
    test.expect(first && secondTile && first->name != secondTile->name &&
                    first->targetId == secondTile->targetId &&
                    service.snapshot().value(QStringLiteral("targets")) == before.value(QStringLiteral("targets")),
                "one shared target descriptor is unchanged by multiple independent labels");
}

void checkDynamicColumns(TestContext& test, const xdstarter::TileGrid& grid) {
    using namespace xdstarter;
    PrototypeService service(grid);
    const auto original = service.snapshot();
    const auto originalGroups = service.previewGroups();
    test.expect(service.setGridColumns(11).success && service.gridColumns() == 11 &&
                    service.previewGroups().first().width == grid.width(11),
                "expanding adds actual logical placement columns");
    for (qsizetype group = 0; group < originalGroups.size(); ++group) {
        const auto expanded = service.previewGroups()[group];
        for (qsizetype tile = 0; tile < expanded.tiles.size(); ++tile)
            test.expect(sameRectangle(originalGroups[group].tiles[tile].rectangle,
                                      expanded.tiles[tile].rectangle),
                        "expanding columns preserves each original tile position");
    }
    test.expect(service.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 10, 3)
                        .success == false,
                "a two-cell tile still needs its full span at the dynamic edge");
    test.expect(
        service.setGridColumns(12).success &&
            service.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 10, 3)
                .success,
        "a wider budget permits a real position beyond legacy column nine");
    test.expect(service.setGridColumns(4).success && service.gridColumns() == 4 &&
                    LayoutStore::validate(service.snapshot()).success,
                "shrinking reflows overflow while preserving all layout invariants");
    const auto shrunk = service.snapshot();
    test.expect(shrunk.value(QStringLiteral("targets")) ==
                    original.value(QStringLiteral("targets")),
                "responsive reflow retains original target descriptors");
    const auto shrunkGroups = service.previewGroups();
    for (qsizetype group = 0; group < originalGroups.size(); ++group)
        for (qsizetype tile = 0; tile < originalGroups[group].tiles.size(); ++tile)
            test.expect(
                originalGroups[group].tiles[tile].id == shrunkGroups[group].tiles[tile].id &&
                    originalGroups[group].tiles[tile].targetId ==
                        shrunkGroups[group].tiles[tile].targetId &&
                    originalGroups[group].tiles[tile].size == shrunkGroups[group].tiles[tile].size,
                "reflow preserves model order, stable IDs and tile size");
    PrototypeService repeated(grid);
    test.expect(
        repeated.setGridColumns(12).success &&
            repeated.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 10, 3)
                .success &&
            repeated.setGridColumns(4).success && repeated.snapshot() == shrunk,
        "the same resize sequence has deterministic displacement");
    for (const int invalid : {0, 3, 65})
        test.expect(!service.setGridColumns(invalid).success && service.snapshot() == shrunk,
                    "invalid column changes leave the complete snapshot unchanged");
    test.expect(
        service.setGridColumns(64).success &&
            service.moveTile(QStringLiteral("work-wide-long"), QStringLiteral("work"), 60, 62)
                .success &&
            LayoutStore::validate(service.snapshot()).success,
        "the supported maximum accepts a wide tile at its final valid boundary");
    PrototypeService restarted(grid);
    test.expect(restarted.restoreSnapshot(service.snapshot()).success &&
                    restarted.gridColumns() == 64 && restarted.snapshot() == service.snapshot(),
                "dynamic columns and distant positions round trip without truncation");
    auto full = original;
    auto groups = full.value(QStringLiteral("groups")).toArray();
    auto group = groups[0].toObject();
    QJsonArray tiles;
    for (int row = 0; row < 64; ++row)
        for (int column = 0; column < 10; ++column) {
            if (tiles.size() == 300)
                break;
            tiles.append(QJsonObject{
                {QStringLiteral("id"), QStringLiteral("full-responsive-%1").arg(tiles.size())},
                {QStringLiteral("targetId"), QStringLiteral("prototype-work-standard-1")},
                {QStringLiteral("column"), column},
                {QStringLiteral("row"), row},
                {QStringLiteral("size"), QStringLiteral("small")}});
        }
    group.insert(QStringLiteral("tiles"), tiles);
    full.insert(QStringLiteral("groups"), QJsonArray{group});
    PrototypeService packed(grid);
    test.expect(packed.restoreSnapshot(full).success, "dense responsive fixture is valid");
    test.expect(!packed.setGridColumns(4).success && packed.snapshot() == full,
                "insufficient narrow capacity rejects the entire reflow without losing tiles");
}

} // namespace

int main() {
    using namespace xdstarter;
    TestContext test;
    const TileGrid grid;
    checkGroupManagement(test, grid);
    checkTileDisplayNames(test, grid);
    checkDynamicColumns(test, grid);
    PrototypeService service(grid);
    const auto groups = service.previewGroups();
    test.expect(groups.size() == 3, "preview contains three independent groups");

    QSet<QString> groupIds;
    QSet<QString> tileIds;
    QSet<int> sizes;
    bool longStandard = false;
    bool longWide = false;
    for (const auto& group : groups) {
        test.expect(!group.id.isEmpty() && !group.name.isEmpty(),
                    "group has stable identity and name");
        test.expect(!groupIds.contains(group.id), "group ids are unique");
        groupIds.insert(group.id);
        test.expect(group.width == 552 && group.width == grid.width(),
                    "every group retains the shared ten-column width");
        test.expect(group.height == 216, "preview groups retain two empty drop rows");
        int bottom = 0;
        for (qsizetype index = 0; index < group.tiles.size(); ++index) {
            const auto& tile = group.tiles[index];
            test.expect(!tile.id.isEmpty() && !tile.name.isEmpty() && !tile.symbol.isEmpty(),
                        "tile exposes a stable id, accessible name and generic symbol");
            test.expect(!tileIds.contains(tile.id), "tile ids are unique across all groups");
            tileIds.insert(tile.id);
            sizes.insert(static_cast<int>(tile.size));
            const auto rectangle = grid.rectangle(tile.column, tile.row, tile.size);
            test.expect(rectangle && sameRectangle(*rectangle, tile.rectangle),
                        "tile rectangle is derived from its declared logical placement");
            const auto& actual = tile.rectangle;
            test.expect(actual.x >= 0 && actual.y >= 0 && actual.x + actual.width <= group.width &&
                            actual.y + actual.height <= group.height,
                        "tile remains inside its group");
            bottom = std::max(bottom, actual.y + actual.height);
            for (qsizetype other = index + 1; other < group.tiles.size(); ++other) {
                test.expect(!overlap(actual, group.tiles[other].rectangle),
                            "preview tiles never overlap within a group");
            }
            const bool longName =
                tile.name.size() > 24 && tile.name.contains(QLatin1String("English"));
            longStandard = longStandard || (tile.size == TileSize::Standard && longName);
            longWide = longWide || (tile.size == TileSize::Wide && longName);
        }
        test.expect(bottom + 112 == group.height,
                    "group height reserves two drop rows below actual tile geometry");
    }
    test.expect(groupIds == QSet<QString>{QStringLiteral("work"), QStringLiteral("daily"),
                                          QStringLiteral("entertainment")},
                "group identity remains stable for rendering and navigation");
    test.expect(tileIds.size() == 15, "all fifteen preview tiles are present");
    test.expect(sizes.size() == 3, "preview exercises all three tile sizes");
    test.expect(longStandard && longWide,
                "standard and wide tiles include mixed-language long names");

    const auto* firstSmall = findTile(groups, "daily-small-1");
    const auto* secondSmall = findTile(groups, "daily-small-2");
    const auto* thirdSmall = findTile(groups, "daily-small-3");
    const auto* fourthSmall = findTile(groups, "daily-small-4");
    const auto* alignedStandard = findTile(groups, "entertainment-standard-3");
    const auto* firstStandard = findTile(groups, "work-standard-1");
    const auto* secondStandard = findTile(groups, "work-standard-long");
    const auto* alignedWide = findTile(groups, "daily-wide");
    test.expect(firstSmall && secondSmall && thirdSmall && fourthSmall && alignedStandard &&
                    firstStandard && secondStandard && alignedWide,
                "alignment examples are present in the rendered model");
    if (firstSmall && secondSmall && thirdSmall && fourthSmall && alignedStandard &&
        firstStandard && secondStandard && alignedWide) {
        test.expect(firstSmall->column == 8 && firstSmall->row == 0 && secondSmall->column == 9 &&
                        secondSmall->row == 0 && thirdSmall->column == 8 && thirdSmall->row == 1 &&
                        fourthSmall->column == 9 && fourthSmall->row == 1,
                    "daily small tiles form the intended two-by-two logical block");
        test.expect(firstSmall->rectangle.width == 48 && firstSmall->rectangle.height == 48 &&
                        secondSmall->rectangle.x -
                                (firstSmall->rectangle.x + firstSmall->rectangle.width) ==
                            8 &&
                        thirdSmall->rectangle.y -
                                (firstSmall->rectangle.y + firstSmall->rectangle.height) ==
                            8,
                    "rendered small tile examples retain both eight-DIP internal gaps");
        const TileRect quartet{
            firstSmall->rectangle.x, firstSmall->rectangle.y,
            fourthSmall->rectangle.x + fourthSmall->rectangle.width - firstSmall->rectangle.x,
            fourthSmall->rectangle.y + fourthSmall->rectangle.height - firstSmall->rectangle.y};
        test.expect(sameRectangle(quartet, alignedStandard->rectangle),
                    "four rendered small tiles align with a standard tile in the next group");
        const TileRect pair{firstStandard->rectangle.x, firstStandard->rectangle.y,
                            secondStandard->rectangle.x + secondStandard->rectangle.width -
                                firstStandard->rectangle.x,
                            firstStandard->rectangle.height};
        test.expect(secondStandard->rectangle.x -
                                (firstStandard->rectangle.x + firstStandard->rectangle.width) ==
                            8 &&
                        sameRectangle(pair, alignedWide->rectangle),
                    "two rendered standard tiles and their gap align with a wide tile");
    }

    expectNeighbor(test, service, "work-standard-1", "right", "work-standard-long",
                   "right arrow selects the nearest tile in the same row");
    expectNeighbor(test, service, "work-wide-long", "left", "work-standard-3",
                   "left arrow from a wide tile uses its actual rectangle");
    expectNeighbor(test, service, "daily-small-1", "down", "daily-small-3",
                   "vertical navigation prefers an overlapping column over a nearer diagonal");
    expectNeighbor(test, service, "daily-small-3", "up", "daily-small-1",
                   "up arrow stays within the small tile column");
    expectNeighbor(test, service, "daily-small-3", "right", "daily-small-4",
                   "horizontal navigation prefers the overlapping row");
    expectNeighbor(test, service, "daily-standard-2", "right", "daily-small-1",
                   "equal-distance candidates keep stable model order");
    expectNeighbor(test, service, "work-standard-1", "down", "daily-wide",
                   "down arrow enters the next group near the same column");
    expectNeighbor(test, service, "daily-small-1", "up", "work-wide-long",
                   "up arrow enters the previous group at the corresponding column");
    expectNeighbor(test, service, "entertainment-standard-3", "up", "daily-small-3",
                   "up arrow enters the bottom edge of the previous group");
    expectNeighbor(test, service, "daily-small-4", "down", "entertainment-standard-3",
                   "down arrow reaches the next group without changing tile geometry");
    expectNeighbor(test, service, "work-standard-1", "left", "",
                   "left arrow never wraps into another group");
    expectNeighbor(test, service, "daily-small-4", "right", "",
                   "right arrow never wraps into another group");
    expectNeighbor(test, service, "work-standard-1", "up", "", "up arrow stops at the first group");
    expectNeighbor(test, service, "entertainment-wide", "down", "",
                   "down arrow stops at the final group");
    expectNeighbor(test, service, "missing-tile", "right", "", "unknown tile has no neighbor");
    expectNeighbor(test, service, "work-standard-1", "diagonal", "",
                   "unknown direction has no neighbor");

    for (const auto& group : groups) {
        for (const auto& tile : group.tiles) {
            for (const auto* direction : {"left", "right", "up", "down"}) {
                QSet<QString> visited;
                auto current = tile.id;
                while (!current.isEmpty() && !visited.contains(current)) {
                    visited.insert(current);
                    current = service.neighborTileId(current, QString::fromLatin1(direction));
                    test.expect(current.isEmpty() || tileIds.contains(current),
                                "navigation returns only rendered tile identities");
                }
                test.expect(current.isEmpty(),
                            "repeated arrow navigation reaches an edge without cycling");
            }
        }
    }

    const auto initialSnapshot = service.snapshot();
    test.expect(LayoutStore::validate(initialSnapshot).success,
                "default snapshot uses the supported schema and prototype targets");
    test.expect(service
                    .checkPlacement(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0,
                                    2, TileSize::Standard)
                    .success,
                "cross-group preview accepts an unoccupied logical drop point");
    test.expect(service.snapshot() == initialSnapshot,
                "valid placement preview and cancellation never mutate the layout");
    test.expect(service.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 0)
                        .success &&
                    LayoutStore::validate(service.snapshot()).success,
                "occupied cross-group drop displaces existing tiles without overlap");
    test.expect(service.restoreSnapshot(initialSnapshot).success &&
                    service.resizeTile(QStringLiteral("work-standard-1"), TileSize::Wide).success &&
                    LayoutStore::validate(service.snapshot()).success,
                "non-final resize grows and displaces adjacent tiles atomically");
    test.expect(service.restoreSnapshot(initialSnapshot).success, "restore before boundary checks");
    test.expect(!service.checkPlacement(QStringLiteral("work-standard-1"), QStringLiteral("work"),
                                        -1, 2, TileSize::Small)
                        .success &&
                    !service
                         .checkPlacement(QStringLiteral("work-standard-1"), QStringLiteral("work"),
                                         8, 2, TileSize::Wide)
                         .success &&
                    !service
                         .checkPlacement(QStringLiteral("work-standard-1"), QStringLiteral("work"),
                                         0, 63, TileSize::Standard)
                         .success,
                "negative coordinates, horizontal overflow and final-row overflow reject");
    test.expect(
        service.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 2).success,
        "a legal cross-group move commits once");
    auto movedGroups = service.previewGroups();
    const auto* moved = findTile(movedGroups, "work-standard-1");
    test.expect(moved && moved->column == 0 && moved->row == 2 &&
                    movedGroups[0].tiles.size() == 3 && movedGroups[1].tiles.size() == 8 &&
                    movedGroups[1].height == 328,
                "cross-group move keeps identity and grows the destination drop area");
    test.expect(
        service.resizeTile(QStringLiteral("work-standard-1"), TileSize::Small).success &&
            service.resizeTile(QStringLiteral("work-standard-1"), TileSize::Wide).success &&
            service.resizeTile(QStringLiteral("work-standard-1"), TileSize::Standard).success,
        "all three sizes work when the anchored cells are free");
    PrototypeService restored(grid);
    test.expect(restored.restoreSnapshot(service.snapshot()).success &&
                    restored.snapshot() == service.snapshot(),
                "snapshot round-trip restores edited positions, identity and sizes");
    auto invalidSnapshot = service.snapshot();
    invalidSnapshot.insert(QStringLiteral("schemaVersion"), LayoutStore::schemaVersion + 1);
    const auto beforeInvalidRestore = restored.snapshot();
    test.expect(!restored.restoreSnapshot(invalidSnapshot).success &&
                    restored.snapshot() == beforeInvalidRestore,
                "invalid restore cannot partially overwrite current state");
    auto withEmptyGroup = initialSnapshot;
    auto sparseGroups = withEmptyGroup.value(QStringLiteral("groups")).toArray();
    auto emptyGroup = sparseGroups[1].toObject();
    emptyGroup.insert(QStringLiteral("tiles"), QJsonArray{});
    sparseGroups[1] = emptyGroup;
    withEmptyGroup.insert(QStringLiteral("groups"), sparseGroups);
    test.expect(restored.restoreSnapshot(withEmptyGroup).success &&
                    restored.snapshot() == withEmptyGroup,
                "empty groups and currently unused target metadata survive snapshot restore");
    expectNeighbor(test, restored, "work-standard-1", "down", "entertainment-wide",
                   "keyboard navigation skips an emptied intermediate group");

    PrototypeService applications(grid, {}, false);
    test.expect(applications.previewGroups().size() == 1 &&
                    applications.previewGroups().first().id == QLatin1String("default") &&
                    applications.previewGroups().first().tiles.isEmpty() &&
                    LayoutStore::validate(applications.snapshot()).success,
                "real first run has an empty writable group without synthetic applications");
    const auto shortcut = [](const QString& id) {
        return QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("type"), QStringLiteral("shortcut")},
                           {QStringLiteral("name"), QStringLiteral("Missing application")},
                           {QStringLiteral("symbol"), QStringLiteral("M")},
                           {QStringLiteral("shortcutPath"), QStringLiteral("C:\\Missing\\App.lnk")},
                           {QStringLiteral("iconKey"), id}};
    };
    const auto firstPin = applications.pinApplication(shortcut(QStringLiteral("app-one")));
    const auto secondPin = applications.pinApplication(shortcut(QStringLiteral("app-two")));
    test.expect(firstPin.success && !firstPin.tileId.isEmpty() && secondPin.success &&
                    secondPin.tileId != firstPin.tileId &&
                    applications.previewGroups().first().tiles[0].column == 0 &&
                    applications.previewGroups().first().tiles[1].column == 2,
                "pinning uses unique stable tile identities and the first free standard footprint");
    const auto beforeDuplicate = applications.snapshot();
    const auto duplicate = applications.pinApplication(shortcut(QStringLiteral("app-one")));
    test.expect(duplicate.success && duplicate.tileId == firstPin.tileId &&
                    applications.snapshot() == beforeDuplicate &&
                    applications.isApplicationPinned(QStringLiteral("app-one")),
                "pinning the same application focuses its existing tile without duplication");
    test.expect(
        applications.targetForTile(firstPin.tileId) == shortcut(QStringLiteral("app-one")) &&
            applications.targetForTile(QStringLiteral("missing")).isEmpty() &&
            applications.previewGroups().first().tiles[0].iconKey == QLatin1String("app-one"),
        "target metadata remains separate from placement and exposes its icon cache key");
    test.expect(
        !applications
                .pinApplication(shortcut(QStringLiteral("new")), QStringLiteral("absent-group"))
                .success &&
            applications.snapshot() == beforeDuplicate,
        "pinning to a missing group rejects without changing the layout");
    auto invalidApplication = shortcut(QStringLiteral("invalid"));
    invalidApplication.insert(QStringLiteral("arguments"), QStringLiteral("untrusted"));
    test.expect(!applications.pinApplication(invalidApplication).success &&
                    applications.snapshot() == beforeDuplicate,
                "unsupported launch fields are rejected before committing a tile");
    test.expect(applications.unpinTile(firstPin.tileId).success &&
                    !applications.isApplicationPinned(QStringLiteral("app-one")) &&
                    applications.snapshot().value(QStringLiteral("targets")).toArray().size() == 1,
                "unpin removes only the tile and its unused target metadata");
    const auto replacementPin = applications.pinApplication(shortcut(QStringLiteral("app-three")));
    const auto replacement = applications.previewGroups().first().tiles.last();
    test.expect(replacementPin.success && replacement.column == 0 && replacement.row == 0,
                "pinning reuses the earliest vacant footprint");
    test.expect(applications.unpinTile(secondPin.tileId).success &&
                    applications.unpinTile(replacementPin.tileId).success &&
                    applications.previewGroups().first().tiles.isEmpty() &&
                    applications.snapshot().value(QStringLiteral("targets")).toArray().isEmpty() &&
                    LayoutStore::validate(applications.snapshot()).success,
                "all applications can be unpinned while keeping a valid empty persistent group");
    const auto emptySnapshot = applications.snapshot();
    test.expect(!applications.unpinTile(QStringLiteral("missing")).success &&
                    applications.snapshot() == emptySnapshot,
                "unpinning an unknown tile is a nonmutating failure");
    const QJsonObject packaged{
        {QStringLiteral("id"), QStringLiteral("packaged-app")},
        {QStringLiteral("type"), QStringLiteral("packaged")},
        {QStringLiteral("name"), QStringLiteral("Packaged application")},
        {QStringLiteral("symbol"), QStringLiteral("P")},
        {QStringLiteral("aumid"), QStringLiteral("Example.Package_123456789!App")}};
    const auto packagedPin = applications.pinApplication(packaged);
    test.expect(packagedPin.success && applications.targetForTile(packagedPin.tileId) == packaged,
                "packaged identity is accepted without requiring the application to be installed");
    PrototypeService applicationRestart(grid, {}, false);
    test.expect(applicationRestart.restoreSnapshot(applications.snapshot()).success &&
                    applicationRestart.snapshot() == applications.snapshot() &&
                    applicationRestart.isApplicationPinned(QStringLiteral("packaged-app")),
                "application identities, positions and pin state survive a snapshot round trip");

    auto fullSnapshot = emptySnapshot;
    QJsonArray fullTiles;
    for (int row = 0; row < LayoutStore::maximumRows; row += 2) {
        for (int column = 0; column < 10; column += 2) {
            fullTiles.append(QJsonObject{
                {QStringLiteral("id"), QStringLiteral("full-%1-%2").arg(column).arg(row)},
                {QStringLiteral("targetId"), QStringLiteral("full-target")},
                {QStringLiteral("column"), column},
                {QStringLiteral("row"), row},
                {QStringLiteral("size"), QStringLiteral("standard")}});
        }
    }
    auto fullGroups = fullSnapshot.value(QStringLiteral("groups")).toArray();
    auto fullGroup = fullGroups[0].toObject();
    fullGroup.insert(QStringLiteral("tiles"), fullTiles);
    fullGroups[0] = fullGroup;
    fullSnapshot.insert(QStringLiteral("groups"), fullGroups);
    fullSnapshot.insert(QStringLiteral("targets"),
                        QJsonArray{shortcut(QStringLiteral("full-target"))});
    test.expect(applications.restoreSnapshot(fullSnapshot).success &&
                    !applications.pinApplication(shortcut(QStringLiteral("overflow"))).success &&
                    !applications
                         .checkApplicationPlacement(shortcut(QStringLiteral("overflow")),
                                                    QStringLiteral("default"), 0, 0)
                         .success &&
                    !applications
                         .pinApplicationAt(shortcut(QStringLiteral("overflow")),
                                           QStringLiteral("default"), 0, 0)
                         .success &&
                    applications.snapshot() == fullSnapshot,
                "a full 64-row group reports failure instead of overwriting or exceeding bounds");
    test.expect(applications.unpinTile(QStringLiteral("full-0-0")).success &&
                    applications.isApplicationPinned(QStringLiteral("full-target")) &&
                    applications.snapshot().value(QStringLiteral("targets")).toArray().size() ==
                        1 &&
                    LayoutStore::validate(applications.snapshot()).success,
                "unpin retains target metadata when another restored tile still references it");

    PrototypeService catalogPins(grid, {}, false);
    const auto earlierGroup = catalogPins.createGroup(QStringLiteral("Earlier"));
    const auto lastGroup = catalogPins.createGroup(QStringLiteral("Last"));
    const auto lastPin = catalogPins.pinApplication(shortcut(QStringLiteral("last-app")));
    test.expect(earlierGroup.success && lastGroup.success && lastPin.success &&
                    catalogPins.previewGroups().last().id == lastGroup.groupId &&
                    catalogPins.previewGroups().last().tiles.first().id == lastPin.tileId,
                "catalog context pinning defaults to the last group");
    test.expect(catalogPins.moveGroupBy(lastGroup.groupId, -1).success &&
                    catalogPins.pinApplication(shortcut(QStringLiteral("ordered-app"))).success &&
                    catalogPins.previewGroups().last().id == earlierGroup.groupId &&
                    catalogPins.previewGroups().last().tiles.first().targetId ==
                        QLatin1String("ordered-app"),
                "default destination follows current group order");
    const auto beforeCatalogPreview = catalogPins.snapshot();
    const auto newTarget = shortcut(QStringLiteral("dragged-app"));
    const auto catalogPreview =
        catalogPins.checkApplicationPlacement(newTarget, lastGroup.groupId, 0, 0);
    test.expect(catalogPreview.success && catalogPins.snapshot() == beforeCatalogPreview,
                "catalog drag preview does not insert target metadata or a tile");
    const auto dropped = catalogPins.pinApplicationAt(newTarget, lastGroup.groupId, 0, 0);
    const auto destinationAfterDrop = catalogPins.previewGroups()[1];
    test.expect(dropped.success && destinationAfterDrop.tiles.size() == 2 &&
                    destinationAfterDrop.tiles.first().id == lastPin.tileId &&
                    destinationAfterDrop.tiles.first().column == 2 &&
                    destinationAfterDrop.tiles.last().id == dropped.tileId &&
                    destinationAfterDrop.tiles.last().column == 0 &&
                    LayoutStore::validate(catalogPins.snapshot()).success,
                "dropping a new catalog tile reserves its cells and displaces existing tiles");
    const auto afterDrop = catalogPins.snapshot();
    test.expect(!catalogPins
                        .pinApplicationAt(shortcut(QStringLiteral("outside-app")),
                                          lastGroup.groupId, catalogPins.gridColumns() - 1, 0)
                        .success &&
                    catalogPins.snapshot() == afterDrop &&
                    catalogPins.pinApplicationAt(newTarget, earlierGroup.groupId, 4, 4).tileId ==
                        dropped.tileId &&
                    catalogPins.snapshot() == afterDrop,
                "invalid drops roll back and already pinned applications never duplicate or move");
    auto restoredDuplicates = catalogPins.snapshot();
    auto duplicateGroups = restoredDuplicates.value(QStringLiteral("groups")).toArray();
    auto duplicateGroup = duplicateGroups[0].toObject();
    auto duplicateTiles = duplicateGroup.value(QStringLiteral("tiles")).toArray();
    duplicateTiles.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("restored-duplicate")},
                                      {QStringLiteral("targetId"), QStringLiteral("last-app")},
                                      {QStringLiteral("column"), 0},
                                      {QStringLiteral("row"), 20},
                                      {QStringLiteral("size"), QStringLiteral("standard")}});
    duplicateGroup.insert(QStringLiteral("tiles"), duplicateTiles);
    duplicateGroups[0] = duplicateGroup;
    restoredDuplicates.insert(QStringLiteral("groups"), duplicateGroups);
    test.expect(catalogPins.restoreSnapshot(restoredDuplicates).success &&
                    catalogPins.unpinApplication(QStringLiteral("last-app")).success &&
                    !catalogPins.isApplicationPinned(QStringLiteral("last-app")) &&
                    catalogPins.isApplicationPinned(QStringLiteral("ordered-app")) &&
                    catalogPins.isApplicationPinned(QStringLiteral("dragged-app")) &&
                    catalogPins.targetForTile(lastPin.tileId).isEmpty() &&
                    LayoutStore::validate(catalogPins.snapshot()).success,
                "app unpin atomically removes all restored duplicate references across groups");
    const auto afterAppUnpin = catalogPins.snapshot();
    test.expect(!catalogPins.unpinApplication(QStringLiteral("absent-app")).success &&
                    catalogPins.snapshot() == afterAppUnpin,
                "unknown app unpin is a nonmutating failure");

    PrototypeService packed(grid);
    for (int iteration = 0; iteration < 180; ++iteration) {
        const auto before = packed.snapshot();
        const auto allGroups = packed.previewGroups();
        const auto& from = allGroups[iteration % allGroups.size()];
        if (from.tiles.isEmpty())
            continue;
        const auto& tile = from.tiles[(iteration / 3) % from.tiles.size()];
        const auto& destination = allGroups[(iteration / 7) % allGroups.size()];
        const int column = (iteration * 7) % 12 - 1;
        const int row = (iteration * 3) % 67 - 1;
        const auto preview = packed.checkPlacement(tile.id, destination.id, column, row, tile.size);
        test.expect(packed.snapshot() == before, "reflow preview is nonmutating");
        const auto result = packed.moveTile(tile.id, destination.id, column, row);
        test.expect(result.success == preview.success, "reflow preview and commit agree");
        if (!result.success)
            test.expect(packed.snapshot() == before,
                        "failed reflow keeps the complete source snapshot");
        test.expect(LayoutStore::validate(packed.snapshot()).success,
                    "reflow preserves IDs, bounds, references and no overlap");
        const auto resize = packed.resizeTile(tile.id, static_cast<TileSize>(iteration % 3));
        test.expect(LayoutStore::validate(packed.snapshot()).success,
                    "all resize variants preserve a valid layout");
        (void)resize;
        PrototypeService restart(grid);
        test.expect(restart.restoreSnapshot(packed.snapshot()).success &&
                        restart.snapshot() == packed.snapshot(),
                    "displaced layouts round trip exactly");
    }
    test.expect(applications.restoreSnapshot(fullSnapshot).success,
                "restore a completely full fixture");
    const auto fullBefore = applications.snapshot();
    test.expect(!applications.resizeTile(QStringLiteral("full-2-0"), TileSize::Wide).success &&
                    applications.snapshot() == fullBefore,
                "insufficient total space rolls back every displacement");
    test.expect(applications.moveTile(QStringLiteral("full-0-0"), QStringLiteral("default"), 8, 62)
                        .success &&
                    LayoutStore::validate(applications.snapshot()).success,
                "a full-grid move can reuse the vacated source cells without dropping a tile");

    if (test.failures == 0) {
        std::cout << "All prototype service checks passed.\n";
    }
    return test.failures == 0 ? 0 : 1;
}
