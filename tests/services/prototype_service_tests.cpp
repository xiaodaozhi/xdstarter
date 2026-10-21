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

} // namespace

int main() {
    using namespace xdstarter;
    TestContext test;
    const TileGrid grid;
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
    test.expect(!service.moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 0)
                        .success &&
                    service.snapshot() == initialSnapshot,
                "overlapping drop rejects atomically without removing the source");
    test.expect(!service.resizeTile(QStringLiteral("work-standard-1"), TileSize::Wide).success &&
                    service.snapshot() == initialSnapshot,
                "resize preserves the top-left anchor and rejects occupied adjacent cells");
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
    invalidSnapshot.insert(QStringLiteral("schemaVersion"), 3);
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
                    applications.snapshot() == fullSnapshot,
                "a full 64-row group reports failure instead of overwriting or exceeding bounds");
    test.expect(applications.unpinTile(QStringLiteral("full-0-0")).success &&
                    applications.isApplicationPinned(QStringLiteral("full-target")) &&
                    applications.snapshot().value(QStringLiteral("targets")).toArray().size() ==
                        1 &&
                    LayoutStore::validate(applications.snapshot()).success,
                "unpin retains target metadata when another restored tile still references it");

    if (test.failures == 0) {
        std::cout << "All prototype service checks passed.\n";
    }
    return test.failures == 0 ? 0 : 1;
}
