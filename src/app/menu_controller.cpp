#include "menu_controller.h"

#include <QCoreApplication>
#include <QKeyEvent>

#include "platform/platform_integration.h"
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/layout_import.h"
#include "services/layout_store.h"
#include "services/prototype_service.h"
#include "services/search_service.h"
#include "xdstarter/grid_tokens.h"

#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <utility>

namespace xdstarter {

// Worker owns result until done is published; the GUI consumes it once, including on shutdown.
struct PendingLayoutImport {
    QJsonObject replacement;
    quint64 revision = 0;
    LayoutImportCommitResult outcome;
    std::atomic_bool done{false};
};

namespace {

QString sizeName(TileSize size) {
    switch (size) {
    case TileSize::Small:
        return QStringLiteral("small");
    case TileSize::Standard:
        return QStringLiteral("standard");
    case TileSize::Wide:
        return QStringLiteral("wide");
    }
    return {};
}

std::optional<TileSize> parseSize(const QString& size) {
    if (size == QLatin1String("small"))
        return TileSize::Small;
    if (size == QLatin1String("standard"))
        return TileSize::Standard;
    if (size == QLatin1String("wide"))
        return TileSize::Wide;
    return std::nullopt;
}

QVariantList tileValues(const QVector<PreviewTile>& tiles) {
    QVariantList values;
    values.reserve(tiles.size());
    for (const auto& tile : tiles) {
        const auto& rectangle = tile.rectangle;
        values.append(QVariantMap{
            {QStringLiteral("id"), tile.id},
            {QStringLiteral("targetId"), tile.targetId},
            {QStringLiteral("name"), tile.name},
            {QStringLiteral("displayName"), tile.displayName},
            {QStringLiteral("symbol"), tile.symbol},
            {QStringLiteral("small"), tile.size == TileSize::Small},
            {QStringLiteral("column"), tile.column},
            {QStringLiteral("row"), tile.row},
            {QStringLiteral("size"), sizeName(tile.size)},
            {QStringLiteral("x"), rectangle.x},
            {QStringLiteral("y"), rectangle.y},
            {QStringLiteral("width"), rectangle.width},
            {QStringLiteral("height"), rectangle.height},
            {QStringLiteral("dimensions"),
             QStringLiteral("%1 × %2 DIP").arg(rectangle.width).arg(rectangle.height)},
        });
    }
    return values;
}

} // namespace

MenuController::MenuController(PrototypeService& prototypeService,
                               const PlatformIntegration& platform, QVariantMap visualTokens,
                               QObject* parent, ApplicationCatalog* catalog,
                               DesktopShellService* desktopShell)
    : QObject(parent), prototypeService_(prototypeService), visualTokens_(std::move(visualTokens)),
      platformName_(platform.displayName()), platformStatus_(platform.statusDescription()),
      simulatedPlatform_(platform.isSimulated()), catalog_(catalog), desktopShell_(desktopShell),
      search_(std::make_unique<SearchService>()) {
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
    connect(search_.get(), &SearchService::resultsChanged, this,
            &MenuController::refreshSearchResults);
    connect(search_.get(), &SearchService::stateChanged, this,
            &MenuController::refreshSearchResults);
    if (catalog_)
        catalog_->refreshApplications();
    readOnly_ = prototypeService.isReadOnly();
    editMode_ = !readOnly_;
    darkTheme_ = prototypeService.snapshot()
                     .value(QStringLiteral("preferences"))
                     .toObject()
                     .value(QStringLiteral("theme"))
                     .toString() != QLatin1String("light");
    persistenceError_ = readOnly_ || !prototypeService.loadResult().success ||
                        prototypeService.loadResult().recoveredFromBackup;
    persistenceStatus_ = prototypeService.loadResult().message;
    if (persistenceStatus_.isEmpty()) {
        persistenceStatus_ = prototypeService.configurationPath().isEmpty()
                                 ? QStringLiteral("本次布局仅在内存中保留。")
                             : prototypeService.loadResult().usedDefaults
                                 ? QStringLiteral("首次使用，编辑后会自动保存布局。")
                                 : QStringLiteral("已恢复保存的布局与主题。");
    }
    refreshGroups();
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(500);
    connect(&saveTimer_, &QTimer::timeout, this, &MenuController::startSave);
    if (!prototypeService.configurationPath().isEmpty()) {
        saveWorker_ = new QObject;
        saveWorker_->moveToThread(&saveThread_);
        connect(&saveThread_, &QThread::finished, saveWorker_, &QObject::deleteLater);
        saveThread_.start();
    }
    if (catalog_) {
        connect(catalog_, &ApplicationCatalog::catalogChanged, this, [this] {
            ++iconRevision_;
            emit iconRevisionChanged();
            if (!catalog_->busy() && !searchTargetIds_.isEmpty()) {
                for (const auto& id : searchTargetIds_)
                    iconVersions_.insert(id, QVariant::fromValue(++nextIconVersion_));
                emit iconVersionsChanged();
            }
            rebuildSearchEntries();
            refreshSearchResults();
            emit catalogChanged();
        });
        connect(catalog_, &ApplicationCatalog::availabilityChanged, this,
                &MenuController::refreshGroups);
        connect(catalog_, &ApplicationCatalog::iconReady, this, [this](const QString& id) {
            if (!pinnedTargetIds_.contains(id) && !searchTargetIds_.contains(id))
                return;
            iconVersions_.insert(id, QVariant::fromValue(++nextIconVersion_));
            emit iconVersionsChanged();
        });
        connect(catalog_, &ApplicationCatalog::launchStateChanged, this,
                &MenuController::launchStateChanged);
        connect(catalog_, &ApplicationCatalog::launchSucceeded, this, [this] {
            setOperationMessage(QStringLiteral("应用已启动。"));
            emit launchSucceeded();
        });
        connect(catalog_, &ApplicationCatalog::launchFailed, this, [this](const QString& message) {
            setOperationMessage(message, true);
            emit launchFailed(message);
        });
    }
    if (desktopShell_) {
        connect(desktopShell_, &DesktopShellService::stateChanged, this, [this] {
            if (desktopShell_ && desktopShell_->busy())
                desktopActionError_ = false;
            setOperationMessage(desktopActionStatus(), desktopActionError_);
            emit desktopActionStateChanged();
        });
        connect(desktopShell_, &DesktopShellService::desktopActionSucceeded, this,
                [this](const QString&) {
                    desktopActionError_ = false;
                    setOperationMessage(desktopActionStatus());
                    emit desktopActionStateChanged();
                    emit desktopActionSucceeded();
                });
        connect(desktopShell_, &DesktopShellService::desktopActionFailed, this,
                [this](const QString&, const QString& message) {
                    desktopActionError_ = true;
                    setOperationMessage(message, true);
                    emit desktopActionStateChanged();
                    emit desktopActionFailed(message);
                });
        connect(desktopShell_, &QObject::destroyed, this,
                &MenuController::desktopActionStateChanged);
    }
}

MenuController::~MenuController() {
    flushSave();
    search_.reset();
    saveThread_.quit();
    saveThread_.wait();
}

void MenuController::refreshGroups() {
    QHash<QString, QVariantMap> previousTiles;
    for (const auto& value : groups_) {
        const auto group = value.toMap();
        for (const auto& tileValue : group.value(QStringLiteral("tiles")).toList()) {
            auto tile = tileValue.toMap();
            tile.insert(QStringLiteral("groupId"), group.value(QStringLiteral("id")));
            previousTiles.insert(tile.value(QStringLiteral("id")).toString(), tile);
        }
    }
    const auto groups = prototypeService_.previewGroups();
    tileTransitions_.clear();
    groups_.clear();
    tiles_.clear();
    previewWidth_ = 0;
    previewHeight_ = 0;
    groups_.reserve(groups.size());
    for (const auto& group : groups) {
        auto tiles = tileValues(group.tiles);
        for (auto& tileValue : tiles) {
            auto tile = tileValue.toMap();
            const auto previous = previousTiles.value(tile.value(QStringLiteral("id")).toString());
            QVariantMap motion{{QStringLiteral("hasPrevious"), !previous.isEmpty()}};
            const bool sameGroup = previous.value(QStringLiteral("groupId")).toString() == group.id;
            for (const auto* coordinate : {"x", "y", "width", "height"})
                motion.insert(QStringLiteral("motionFrom_") + QLatin1String(coordinate),
                              sameGroup ? previous.value(QLatin1String(coordinate))
                                        : tile.value(QLatin1String(coordinate)));
            tileTransitions_.insert(tile.value(QStringLiteral("id")).toString(), motion);
            const auto target =
                prototypeService_.targetForTile(tile.value(QStringLiteral("id")).toString());
            tile.insert(QStringLiteral("originalName"),
                        target.value(QStringLiteral("name")).toString());
            const auto type = target.value(QStringLiteral("type")).toString();
            tile.insert(QStringLiteral("targetType"), type);
            const bool real =
                type == QLatin1String("shortcut") || type == QLatin1String("packaged");
            tile.insert(QStringLiteral("iconSource"),
                        real ? QStringLiteral("image://application-icons/") +
                                   tile.value(QStringLiteral("targetId")).toString()
                             : QString{});
            tile.insert(QStringLiteral("iconKey"),
                        target.value(QStringLiteral("iconKey")).toString());
            const auto available =
                catalog_ && real
                    ? catalog_->availability(tile.value(QStringLiteral("targetId")).toString())
                    : LaunchResult{true, {}, {}};
            tile.insert(QStringLiteral("available"), available.success);
            tile.insert(QStringLiteral("unavailableReason"), available.message);
            tileValue = tile;
        }
        groups_.append(QVariantMap{
            {QStringLiteral("id"), group.id},
            {QStringLiteral("name"), group.name},
            {QStringLiteral("tiles"), tiles},
            {QStringLiteral("width"), group.width},
            {QStringLiteral("height"), group.height},
        });
        previewWidth_ = std::max(previewWidth_, group.width);
    }
    if (!groups.isEmpty()) {
        tiles_ = groups_.front().toMap().value(QStringLiteral("tiles")).toList();
        previewHeight_ = groups.front().height;
    }
    QSet<QString> pinned;
    for (const auto& group : groups)
        for (const auto& tile : group.tiles)
            pinned.insert(tile.targetId);
    bool versionsChanged = false;
    for (auto iterator = iconVersions_.begin(); iterator != iconVersions_.end();) {
        if (!pinned.contains(iterator.key()) && !searchTargetIds_.contains(iterator.key())) {
            iterator = iconVersions_.erase(iterator);
            versionsChanged = true;
        } else {
            ++iterator;
        }
    }
    for (const auto& id : pinned) {
        if (!pinnedTargetIds_.contains(id)) {
            // Re-pinning must not reuse an old placeholder from Qt's image cache.
            iconVersions_.insert(id, QVariant::fromValue(++nextIconVersion_));
            versionsChanged = true;
        }
    }
    pinnedTargetIds_ = std::move(pinned);
    if (versionsChanged)
        emit iconVersionsChanged();
    if (catalog_) {
        const auto snapshot = prototypeService_.snapshot();
        catalog_->registerTargets(snapshot.value(QStringLiteral("targets")).toArray());
        catalog_->setPinnedIds(pinnedTargetIds_);
    }
    emit groupsChanged();
    rebuildSearchEntries();
    refreshSearchResults();
}

QVariantList MenuController::tiles() const { return tiles_; }

QVariantList MenuController::groups() const { return groups_; }

QVariantMap MenuController::visualTokens() const { return visualTokens_; }

QString MenuController::platformName() const { return platformName_; }

QString MenuController::platformStatus() const { return platformStatus_; }

bool MenuController::simulatedPlatform() const { return simulatedPlatform_; }

int MenuController::previewWidth() const { return previewWidth_; }

int MenuController::previewHeight() const { return previewHeight_; }

QString MenuController::neighborTileId(const QString& tileId, const QString& direction) const {
    return prototypeService_.neighborTileId(tileId, direction);
}

int MenuController::gridPitch() const { return grid_tokens::cell + grid_tokens::gap; }
int MenuController::gridGap() const { return grid_tokens::gap; }
int MenuController::minimumGridWidth() const { return TileGrid{}.width(TileGrid::minimumColumns); }
int MenuController::defaultGridWidth() const { return TileGrid{}.width(); }
int MenuController::gridColumns() const { return prototypeService_.gridColumns(); }

bool MenuController::setGridColumnsForWidth(qreal availableWidth) {
    if (readOnly_ || importCommitting() || !std::isfinite(availableWidth) || availableWidth <= 0)
        return false;
    const int columns = TileGrid::columnsForWidth(availableWidth);
    if (columns == gridColumns())
        return true;
    const auto result = prototypeService_.setGridColumns(columns);
    if (!result.success) {
        setOperationMessage(result.message, true);
        return false;
    }
    refreshGroups();
    scheduleSave();
    return true;
}
bool MenuController::editMode() const { return editMode_; }
bool MenuController::darkTheme() const { return darkTheme_; }
bool MenuController::readOnly() const { return readOnly_; }
bool MenuController::saving() const { return saving_; }
QString MenuController::operationMessage() const { return operationMessage_; }
bool MenuController::operationError() const { return operationError_; }
QString MenuController::persistenceStatus() const { return persistenceStatus_; }
bool MenuController::persistenceError() const { return persistenceError_; }

QAbstractItemModel* MenuController::applications() const {
    return catalog_ ? catalog_->applicationsModel() : nullptr;
}
bool MenuController::catalogBusy() const { return catalog_ && catalog_->busy(); }
QString MenuController::catalogStatus() const {
    return catalog_ ? catalog_->status() : QStringLiteral("当前未连接真实应用目录。");
}
bool MenuController::catalogError() const { return catalog_ && catalog_->error(); }
bool MenuController::launchBusy() const { return catalog_ && catalog_->launchBusy(); }
int MenuController::iconRevision() const { return iconRevision_; }
QVariantMap MenuController::iconVersions() const { return iconVersions_; }

QString MenuController::searchQuery() const { return search_->query(); }

void MenuController::setSearchQuery(const QString& query) {
    const auto previous = search_->query();
    search_->setQuery(query);
    if (previous != search_->query())
        emit searchQueryChanged();
}

bool MenuController::searchActive() const { return !search_->query().trimmed().isEmpty(); }
bool MenuController::searchBusy() const { return search_->busy(); }
QVariantList MenuController::searchResults() const { return searchResults_; }

QString MenuController::searchStatus() const {
    QString status;
    if (!searchActive())
        status = QStringLiteral("输入应用名称、英文或拼音。");
    else if (searchBusy())
        status = QStringLiteral("正在搜索…");
    else if (search_->matchCount() == 0)
        status = QStringLiteral("未找到与「%1」匹配的应用。").arg(search_->query());
    else if (search_->matchCount() > SearchIndex::maximumResults)
        status = QStringLiteral("匹配 %1 项，显示前 %2 项。")
                     .arg(search_->matchCount())
                     .arg(SearchIndex::maximumResults);
    else
        status = QStringLiteral("找到 %1 项应用。").arg(search_->matchCount());
    if (catalogBusy())
        status += QStringLiteral(" 当前应用目录正在更新。");
    return status;
}

void MenuController::rebuildSearchEntries() {
    QVector<SearchEntry> entries;
    QHash<QString, qsizetype> positions;
    if (catalog_) {
        const auto applications = catalog_->catalogApplications();
        entries.reserve(applications.size());
        for (const auto& target : applications) {
            if (target.id.isEmpty() || positions.contains(target.id))
                continue;
            positions.insert(target.id, entries.size());
            entries.append({target.id, target.name, {}});
        }
    }
    for (const auto& group : prototypeService_.previewGroups()) {
        for (const auto& tile : group.tiles) {
            if (tile.targetId.isEmpty())
                continue;
            const auto searchId =
                catalog_ ? catalog_->canonicalApplicationId(tile.targetId) : tile.targetId;
            if (!positions.contains(searchId)) {
                const auto target = prototypeService_.targetForTile(tile.id);
                auto name = target.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    name = tile.name;
                positions.insert(searchId, entries.size());
                entries.append({searchId, name, {}});
            }
            auto& entry = entries[positions.value(searchId)];
            const auto originalName =
                prototypeService_.targetForTile(tile.id).value(QStringLiteral("name")).toString();
            if (!originalName.isEmpty() && originalName != entry.displayName &&
                !entry.aliases.contains(originalName))
                entry.aliases.append(originalName);
            if (!tile.displayName.isEmpty() && !entry.tileDisplayNames.contains(tile.displayName))
                entry.tileDisplayNames.append(tile.displayName);
        }
    }
    // Group order is presentation state, not a change to application search identity.
    std::sort(entries.begin(), entries.end(),
              [](const SearchEntry& left, const SearchEntry& right) {
                  return left.targetId < right.targetId;
              });
    for (auto& entry : entries) {
        entry.aliases.sort(Qt::CaseSensitive);
        entry.tileDisplayNames.sort(Qt::CaseSensitive);
    }
    QVariantList state;
    state.reserve(entries.size());
    for (const auto& entry : entries)
        state.append(QVariantMap{{QStringLiteral("id"), entry.targetId},
                                 {QStringLiteral("name"), entry.displayName},
                                 {QStringLiteral("aliases"), entry.aliases},
                                 {QStringLiteral("tileDisplayNames"), entry.tileDisplayNames}});
    if (state == searchSourceState_)
        return;
    searchSourceState_ = std::move(state);
    search_->setEntries(std::move(entries));
}

void MenuController::refreshSearchResults() {
    QVariantList results;
    QSet<QString> ids;
    bool versionsChanged = false;
    for (const auto& match : search_->results()) {
        const auto target = catalog_ ? catalog_->target(match.targetId) : LaunchTarget{};
        const bool real = catalog_ && (target.type == QLatin1String("shortcut") ||
                                       target.type == QLatin1String("packaged"));
        const auto available = real ? catalog_->availability(match.targetId)
                                    : LaunchResult{false, QStringLiteral("样例应用无法启动。"), {}};
        results.append(QVariantMap{
            {QStringLiteral("appId"), match.targetId},
            {QStringLiteral("displayName"), match.displayName},
            {QStringLiteral("kind"), target.type},
            {QStringLiteral("iconSource"),
             real ? QStringLiteral("image://application-icons/") + match.targetId : QString{}},
            {QStringLiteral("available"), available.success},
            {QStringLiteral("unavailableReason"), available.message},
            {QStringLiteral("pinned"), isApplicationPinned(match.targetId)},
        });
        ids.insert(match.targetId);
        if (!iconVersions_.contains(match.targetId)) {
            iconVersions_.insert(match.targetId, QVariant::fromValue(++nextIconVersion_));
            versionsChanged = true;
        }
    }
    searchTargetIds_ = std::move(ids);
    for (auto iterator = iconVersions_.begin(); iterator != iconVersions_.end();) {
        if (!pinnedTargetIds_.contains(iterator.key()) &&
            !searchTargetIds_.contains(iterator.key())) {
            iterator = iconVersions_.erase(iterator);
            versionsChanged = true;
        } else {
            ++iterator;
        }
    }
    if (results != searchResults_) {
        emit searchResultsAboutToChange();
        searchResults_ = std::move(results);
        emit searchResultsChanged();
    }
    if (versionsChanged)
        emit iconVersionsChanged();
    emit searchChanged();
}

bool MenuController::launchSearchResult(const QString& appId) {
    if (!searchActive() || searchBusy() || !searchTargetIds_.contains(appId)) {
        setOperationMessage(QStringLiteral("搜索结果已更新，请重新选择应用。"), true);
        return false;
    }
    if (!catalog_) {
        setOperationMessage(QStringLiteral("当前未连接真实应用目录。"), true);
        return false;
    }
    const auto target = catalog_->target(appId);
    if (target.type != QLatin1String("shortcut") && target.type != QLatin1String("packaged")) {
        setOperationMessage(QStringLiteral("这是样例应用，不会启动应用。"));
        return false;
    }
    const auto available = catalog_->availability(appId);
    if (!available.success) {
        setOperationMessage(available.message, true);
        return false;
    }
    return launchApplication(appId);
}

bool MenuController::desktopActionBusy() const { return desktopShell_ && desktopShell_->busy(); }
bool MenuController::desktopActionError() const { return desktopActionError_; }
QString MenuController::desktopActionStatus() const {
    return desktopShell_ ? desktopShell_->status() : QStringLiteral("当前未连接桌面快捷入口。");
}
bool MenuController::requestDesktopAction(const QString& actionId) {
    if (!desktopShell_) {
        const auto message = QStringLiteral("当前未连接桌面快捷入口。");
        desktopActionError_ = true;
        setOperationMessage(message, true);
        emit desktopActionStateChanged();
        emit desktopActionFailed(message);
        return false;
    }
    return desktopShell_->requestDesktopAction(actionId);
}
void MenuController::requestExit() { emit exitRequested(); }
void MenuController::refreshApplications() {
    if (catalog_)
        catalog_->refreshApplications();
}

bool MenuController::launchApplication(const QString& appId) {
    if (!catalog_) {
        setOperationMessage(QStringLiteral("当前未连接真实应用目录。"), true);
        return false;
    }
    setOperationMessage(QStringLiteral("正在启动应用…"));
    return catalog_->launchApplication(appId);
}

bool MenuController::launchTile(const QString& tileId) {
    const auto target = prototypeService_.targetForTile(tileId);
    if (target.value(QStringLiteral("type")).toString() == QLatin1String("prototype")) {
        setOperationMessage(QStringLiteral("这是样例磁贴，不会启动应用。"));
        return false;
    }
    return launchApplication(target.value(QStringLiteral("id")).toString());
}

bool MenuController::eventFilter(QObject* watched, QEvent* event) {
    bool keyboard = keyboardNavigation_;
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
            keyboard = true;
        else if (key->key() == Qt::Key_Escape)
            keyboard = false;
    } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin ||
               event->type() == QEvent::Wheel || event->type() == QEvent::ApplicationDeactivate) {
        keyboard = false;
    }
    if (keyboard != keyboardNavigation_) {
        keyboardNavigation_ = keyboard;
        emit keyboardNavigationChanged();
    }
    return QObject::eventFilter(watched, event);
}

bool MenuController::pinApplication(const QString& appId, const QString& groupId) {
    if (rejectImportMutation())
        return false;
    if (readOnly_ || !catalog_ || !catalog_->containsApplication(appId)) {
        setOperationMessage(readOnly_ ? QStringLiteral("当前配置只读，不能固定应用。")
                                      : QStringLiteral("应用目录中找不到该应用，请刷新后重试。"),
                            true);
        return false;
    }
    if (isApplicationPinned(appId))
        return true;
    const auto result =
        prototypeService_.pinApplication(applicationTargetJson(catalog_->target(appId)), groupId);
    setOperationMessage(result.success ? QStringLiteral("应用已固定到磁贴布局。") : result.message,
                        !result.success);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

bool MenuController::isApplicationPinned(const QString& appId) const {
    const auto canonical = catalog_ ? catalog_->canonicalApplicationId(appId) : appId;
    for (const auto& pinned : pinnedTargetIds_)
        if ((catalog_ ? catalog_->canonicalApplicationId(pinned) : pinned) == canonical)
            return true;
    return false;
}

QVariantMap MenuController::previewApplicationPlacement(const QString& appId,
                                                        const QString& groupId, int column,
                                                        int row) const {
    LayoutResult result{false, QStringLiteral("应用目录中找不到该应用，请刷新后重试。")};
    if (importCommitting())
        result.message = QStringLiteral("正在替换布局，请稍候。");
    else if (readOnly_)
        result.message = QStringLiteral("当前配置只读，不能固定应用。");
    else if (isApplicationPinned(appId))
        result.message = QStringLiteral("此应用已固定，可在磁贴区域移动。");
    else if (catalog_ && catalog_->containsApplication(appId))
        result = prototypeService_.checkApplicationPlacement(
            applicationTargetJson(catalog_->target(appId)), groupId, column, row);
    return {
        {QStringLiteral("valid"), result.success},
        {QStringLiteral("message"), result.message},
        {QStringLiteral("x"), double(column) * gridPitch()},
        {QStringLiteral("y"), double(row) * gridPitch()},
        {QStringLiteral("width"), grid_tokens::standardColumns * gridPitch() - grid_tokens::gap},
        {QStringLiteral("height"), grid_tokens::standardRows * gridPitch() - grid_tokens::gap}};
}

bool MenuController::pinApplicationAt(const QString& appId, const QString& groupId, int column,
                                      int row) {
    if (rejectImportMutation())
        return false;
    if (readOnly_ || !catalog_ || !catalog_->containsApplication(appId)) {
        setOperationMessage(readOnly_ ? QStringLiteral("当前配置只读，不能固定应用。")
                                      : QStringLiteral("应用目录中找不到该应用，请刷新后重试。"),
                            true);
        return false;
    }
    if (isApplicationPinned(appId)) {
        setOperationMessage(QStringLiteral("此应用已固定，可在磁贴区域移动。"), true);
        return false;
    }
    const auto result = prototypeService_.pinApplicationAt(
        applicationTargetJson(catalog_->target(appId)), groupId, column, row);
    setOperationMessage(result.success ? QStringLiteral("应用已固定到磁贴布局。") : result.message,
                        !result.success);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

bool MenuController::unpinApplication(const QString& appId) {
    if (rejectImportMutation())
        return false;
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能取消固定。"), true);
        return false;
    }
    const auto result = prototypeService_.unpinApplications(
        catalog_ ? catalog_->equivalentApplicationIds(appId) : QSet<QString>{appId});
    setOperationMessage(result.success ? QStringLiteral("已取消此应用的全部固定磁贴。")
                                       : result.message,
                        !result.success);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

bool MenuController::unpinTile(const QString& tileId) {
    if (rejectImportMutation())
        return false;
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能取消固定。"), true);
        return false;
    }
    const auto result = prototypeService_.unpinTile(tileId);
    setOperationMessage(result.success ? QStringLiteral("已取消固定，不会卸载应用。")
                                       : result.message,
                        !result.success);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

int MenuController::maximumTileDisplayNameLength() const {
    return LayoutStore::maximumTileDisplayNameLength;
}

bool MenuController::applyTileNameResult(const LayoutResult& result, const QJsonObject& before,
                                         const QString& successMessage) {
    if (!result.success) {
        setOperationMessage(result.message, true);
        return false;
    }
    if (before != prototypeService_.snapshot()) {
        refreshGroups();
        scheduleSave();
    }
    setOperationMessage(successMessage);
    return true;
}

bool MenuController::renameTile(const QString& tileId, const QString& displayName) {
    if (rejectImportMutation())
        return false;
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能修改磁贴显示名称。"), true);
        return false;
    }
    const auto before = prototypeService_.snapshot();
    return applyTileNameResult(prototypeService_.renameTile(tileId, displayName), before,
                               QStringLiteral("磁贴显示名称已更新，应用启动目标保持不变。"));
}

bool MenuController::resetTileDisplayName(const QString& tileId) {
    if (rejectImportMutation())
        return false;
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能恢复磁贴原名称。"), true);
        return false;
    }
    const auto before = prototypeService_.snapshot();
    return applyTileNameResult(prototypeService_.resetTileDisplayName(tileId), before,
                               QStringLiteral("已恢复磁贴原名称。"));
}

int MenuController::maximumGroups() const { return LayoutStore::maximumGroups; }
int MenuController::maximumGroupNameLength() const { return LayoutStore::maximumGroupNameLength; }

bool MenuController::canEditGroups() {
    if (rejectImportMutation())
        return false;
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能编辑分组。"), true);
        return false;
    }
    if (!editMode_) {
        setOperationMessage(QStringLiteral("请先进入编辑布局再管理分组。"), true);
        return false;
    }
    return true;
}

bool MenuController::applyGroupResult(const LayoutResult& result, const QJsonObject& before,
                                      const QString& successMessage) {
    if (!result.success) {
        setOperationMessage(result.message, true);
        return false;
    }
    if (before != prototypeService_.snapshot()) {
        refreshGroups();
        scheduleSave();
    }
    setOperationMessage(successMessage);
    return true;
}

QString MenuController::createGroup(const QString& name) {
    if (!canEditGroups())
        return {};
    const auto before = prototypeService_.snapshot();
    const auto result = prototypeService_.createGroup(name);
    if (!applyGroupResult(result, before, QStringLiteral("已新建分组。")))
        return {};
    return result.groupId;
}

bool MenuController::renameGroup(const QString& groupId, const QString& name) {
    if (!canEditGroups())
        return false;
    const auto before = prototypeService_.snapshot();
    return applyGroupResult(prototypeService_.renameGroup(groupId, name), before,
                            QStringLiteral("分组名称已更新。"));
}

bool MenuController::moveGroupBy(const QString& groupId, int delta) {
    if (!canEditGroups())
        return false;
    const auto before = prototypeService_.snapshot();
    return applyGroupResult(prototypeService_.moveGroupBy(groupId, delta), before,
                            QStringLiteral("分组顺序已更新。"));
}

bool MenuController::removeGroup(const QString& groupId) {
    if (!canEditGroups())
        return false;
    const auto before = prototypeService_.snapshot();
    return applyGroupResult(prototypeService_.removeGroup(groupId), before,
                            QStringLiteral("已删除空分组。"));
}

void MenuController::setEditMode(bool enabled) {
    if (enabled && rejectImportMutation())
        return;
    if (enabled && readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能编辑布局。"), true);
        return;
    }
    if (editMode_ == enabled)
        return;
    editMode_ = enabled;
    emit editModeChanged();
}

void MenuController::setDarkTheme(bool enabled) {
    if (rejectImportMutation() || readOnly_ || darkTheme_ == enabled)
        return;
    darkTheme_ = enabled;
    emit darkThemeChanged();
    scheduleSave();
}

void MenuController::setOperationMessage(QString message, bool error) {
    if (operationMessage_ == message && operationError_ == error)
        return;
    operationMessage_ = std::move(message);
    operationError_ = error;
    emit operationMessageChanged();
}

void MenuController::setPersistenceStatus(QString status, bool error) {
    if (persistenceStatus_ == status && persistenceError_ == error)
        return;
    persistenceStatus_ = std::move(status);
    persistenceError_ = error;
    emit persistenceStatusChanged();
}

QVariantMap MenuController::findTile(const QString& tileId) const {
    for (const auto& groupValue : groups_) {
        const auto group = groupValue.toMap();
        for (const auto& tileValue : group.value(QStringLiteral("tiles")).toList()) {
            auto tile = tileValue.toMap();
            if (tile.value(QStringLiteral("id")).toString() == tileId) {
                tile.insert(QStringLiteral("groupId"), group.value(QStringLiteral("id")));
                return tile;
            }
        }
    }
    return {};
}

QVariantMap MenuController::previewPlacement(const QString& tileId, const QString& groupId,
                                             int column, int row) const {
    const auto tile = findTile(tileId);
    const auto size = parseSize(tile.value(QStringLiteral("size")).toString());
    LayoutResult result{false, QStringLiteral("找不到磁贴。")};
    if (importCommitting())
        result.message = QStringLiteral("正在替换布局，请稍候。");
    else if (readOnly_)
        result.message = QStringLiteral("当前配置只读，不能编辑布局。");
    else if (size)
        result = prototypeService_.checkPlacement(tileId, groupId, column, row, *size);
    return {{QStringLiteral("valid"), result.success},
            {QStringLiteral("message"), result.message},
            {QStringLiteral("x"), double(column) * gridPitch()},
            {QStringLiteral("y"), double(row) * gridPitch()},
            {QStringLiteral("width"), tile.value(QStringLiteral("width"), grid_tokens::cell)},
            {QStringLiteral("height"), tile.value(QStringLiteral("height"), grid_tokens::cell)}};
}

bool MenuController::moveTile(const QString& tileId, const QString& groupId, int column, int row) {
    if (rejectImportMutation())
        return false;
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能移动磁贴。"), true);
        return false;
    }
    const auto result = prototypeService_.moveTile(tileId, groupId, column, row);
    setOperationMessage(result.success ? QStringLiteral("磁贴已移动。") : result.message,
                        !result.success);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

bool MenuController::moveTileBy(const QString& tileId, int dx, int dy) {
    if (!editMode_ || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx != 0 && dy != 0)) {
        setOperationMessage(QStringLiteral("请进入编辑布局后按 Ctrl 加方向键移动一格。"), true);
        return false;
    }
    const auto tile = findTile(tileId);
    if (tile.isEmpty())
        return false;
    return moveTile(tileId, tile.value(QStringLiteral("groupId")).toString(),
                    tile.value(QStringLiteral("column")).toInt() + dx,
                    tile.value(QStringLiteral("row")).toInt() + dy);
}

bool MenuController::resizeTile(const QString& tileId, const QString& sizeNameValue) {
    if (rejectImportMutation())
        return false;
    const auto size = parseSize(sizeNameValue);
    if (!size || readOnly_) {
        setOperationMessage(readOnly_ ? QStringLiteral("当前配置只读，不能调整尺寸。")
                                      : QStringLiteral("未知的磁贴尺寸。"),
                            true);
        return false;
    }
    const auto result = prototypeService_.resizeTile(tileId, *size);
    setOperationMessage(result.success ? QStringLiteral("磁贴尺寸已切换。") : result.message,
                        !result.success);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

QJsonObject MenuController::currentSnapshot() const {
    auto snapshot = prototypeService_.snapshot();
    auto preferences = snapshot.value(QStringLiteral("preferences")).toObject();
    preferences.insert(QStringLiteral("theme"),
                       darkTheme_ ? QStringLiteral("dark") : QStringLiteral("light"));
    snapshot.insert(QStringLiteral("preferences"), preferences);
    return snapshot;
}

void MenuController::scheduleSave() {
    ++revision_;
    if (importReading_ || !importSnapshot_.isEmpty()) {
        cancelLayoutImport();
        importError_ = QStringLiteral("当前布局已更改，请重新选择文件并确认预览。");
        emit importStateChanged();
    }
    if (!saveWorker_) {
        setPersistenceStatus(QStringLiteral("本次布局仅在内存中保留。"));
        return;
    }
    setPersistenceStatus(QStringLiteral("布局已更改，等待保存。"));
    saveTimer_.start();
}

void MenuController::requestSave() {
    if (readOnly_ || importCommitting())
        return;
    if (!saveWorker_) {
        setPersistenceStatus(QStringLiteral("未配置保存路径，本次布局仅在内存中保留。"));
        return;
    }
    if (revision_ == savedRevision_)
        ++revision_;
    saveTimer_.stop();
    startSave();
}

void MenuController::startSave() {
    if (readOnly_ || !saveWorker_ || saving_ || importCommitting() || revision_ == savedRevision_)
        return;
    saving_ = true;
    emit savingChanged();
    setPersistenceStatus(QStringLiteral("正在保存布局…"));
    const auto snapshot = currentSnapshot();
    const auto path = prototypeService_.configurationPath();
    const auto revision = revision_;
    const auto epoch = saveEpoch_;
    const auto diskRevision = diskRevision_;
    const QPointer<MenuController> receiver(this);
    QMetaObject::invokeMethod(
        saveWorker_,
        [receiver, snapshot, path, revision, epoch, diskRevision] {
            const auto result = LayoutStore(path).save(snapshot);
            if (result.success)
                diskRevision->store(revision, std::memory_order_release);
            if (receiver) {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, result, revision, epoch] {
                        if (receiver && receiver->saveEpoch_ == epoch)
                            receiver->completeSave(revision, result.success, result.message,
                                                   result.readOnly);
                    },
                    Qt::QueuedConnection);
            }
        },
        Qt::QueuedConnection);
}

void MenuController::completeSave(quint64 revision, bool success, const QString& message,
                                  bool blockedReadOnly) {
    saving_ = false;
    emit savingChanged();
    if (success)
        savedRevision_ = std::max(savedRevision_, revision);
    if (blockedReadOnly && !readOnly_) {
        readOnly_ = true;
        setEditMode(false);
        emit readOnlyChanged();
    }
    setPersistenceStatus(success ? QStringLiteral("布局与主题已保存。") : message, !success);
    // Retry newer edits after a completed write, but do not loop on an IO failure.
    if (success && savedRevision_ < revision_ && !readOnly_)
        saveTimer_.start();
}

bool MenuController::flushSave() {
    saveTimer_.stop();
    if (pendingImport_) {
        const auto pending = pendingImport_;
        // Fence the existing worker before capturing memory: an old snapshot must never
        // overwrite a successful import whose GUI completion is still queued.
        QMetaObject::invokeMethod(
            saveWorker_, [] {}, Qt::BlockingQueuedConnection);
        completeLayoutImport(pending);
        if (!pending->outcome.result.success)
            return false;
    }
    cancelLayoutImport();
    if (readOnly_ || !saveWorker_ || revision_ == savedRevision_)
        return !readOnly_;
    const auto snapshot = currentSnapshot();
    const auto path = prototypeService_.configurationPath();
    LayoutResult result{false, QStringLiteral("布局保存未完成。")};
    QMetaObject::invokeMethod(
        saveWorker_, [&result, snapshot, path] { result = LayoutStore(path).save(snapshot); },
        Qt::BlockingQueuedConnection);
    completeSave(revision_, result.success, result.message, result.readOnly);
    if (result.success)
        diskRevision_->store(revision_, std::memory_order_release);
    return result.success;
}

bool MenuController::layoutImportSupported() const {
    return saveWorker_ && !prototypeService_.sampleMode();
}
bool MenuController::importBusy() const { return importReading_ || importCommitting(); }
bool MenuController::importCommitting() const { return bool(pendingImport_); }
QVariantMap MenuController::importPreview() const { return importPreview_; }
QString MenuController::importError() const { return importError_; }

bool MenuController::rejectImportMutation() {
    if (!importCommitting())
        return false;
    setOperationMessage(QStringLiteral("正在替换布局，请稍候。"), true);
    return true;
}

bool MenuController::canImport() {
    if (layoutImportSupported() && !readOnly_ && !importCommitting())
        return true;
    importError_ = readOnly_ ? QStringLiteral("当前配置只读，不能导入或恢复布局。")
                   : importCommitting()
                       ? QStringLiteral("正在替换布局，请稍候。")
                       : QStringLiteral("当前为内存样例模式，不能导入或恢复布局。");
    emit importStateChanged();
    return false;
}

bool MenuController::stageLayoutImport(const QUrl& source) {
    if (!canImport())
        return false;
    if (!source.isLocalFile()) {
        cancelLayoutImport();
        importError_ = QStringLiteral("请选择本地 JSON 布局文件。");
        emit importStateChanged();
        return false;
    }
    return stageLayoutImportPath(source.toLocalFile(), false);
}

bool MenuController::stageBackupRestore() {
    if (!canImport())
        return false;
    return stageLayoutImportPath(prototypeService_.configurationPath() + QStringLiteral(".bak"),
                                 true);
}

bool MenuController::stageLayoutImportPath(const QString& path, bool backup) {
    cancelLayoutImport();
    importReading_ = true;
    const auto generation = importGeneration_;
    const auto revision = revision_;
    const QPointer<MenuController> receiver(this);
    emit importStateChanged();
    QMetaObject::invokeMethod(
        saveWorker_,
        [receiver, path, backup, generation, revision] {
            const auto preview = readLayoutImport(path);
            if (!receiver)
                return;
            QMetaObject::invokeMethod(
                receiver,
                [receiver, preview, path, backup, generation, revision] {
                    if (!receiver || receiver->importGeneration_ != generation)
                        return;
                    receiver->importReading_ = false;
                    if (receiver->readOnly_ || receiver->revision_ != revision) {
                        receiver->importError_ = QStringLiteral("当前布局已更改，请重新读取预览。");
                    } else if (!preview.success) {
                        receiver->importError_ = preview.message;
                    } else {
                        receiver->importSnapshot_ = preview.snapshot;
                        receiver->importPreviewRevision_ = revision;
                        receiver->importPreview_ = {
                            {QStringLiteral("source"), path},
                            {QStringLiteral("fileName"), QFileInfo(path).fileName()},
                            {QStringLiteral("directory"), QFileInfo(path).path()},
                            {QStringLiteral("backup"), backup},
                            {QStringLiteral("sourceVersion"), preview.sourceVersion},
                            {QStringLiteral("currentVersion"), LayoutStore::schemaVersion},
                            {QStringLiteral("groups"), preview.groupCount},
                            {QStringLiteral("tiles"), preview.tileCount},
                            {QStringLiteral("targets"), preview.targetCount},
                            {QStringLiteral("theme"),
                             preview.snapshot.value(QStringLiteral("preferences"))
                                 .toObject()
                                 .value(QStringLiteral("theme"))
                                 .toString()}};
                    }
                    emit receiver->importStateChanged();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return true;
}

void MenuController::cancelLayoutImport() {
    if (importCommitting())
        return;
    ++importGeneration_;
    importReading_ = false;
    importSnapshot_ = {};
    importPreview_ = {};
    importError_.clear();
    emit importStateChanged();
}

bool MenuController::confirmLayoutImport() {
    if (!canImport())
        return false;
    if (importReading_ || importSnapshot_.isEmpty() || importPreviewRevision_ != revision_) {
        importError_ = QStringLiteral("预览已失效，请重新选择布局文件。");
        emit importStateChanged();
        return false;
    }
    saveTimer_.stop();
    ++saveEpoch_; // Old autosave callbacks cannot restart a timer or change import status.
    const auto pending = std::make_shared<PendingLayoutImport>();
    pending->replacement = importSnapshot_;
    pending->revision = revision_;
    const auto current = currentSnapshot();
    const auto path = prototypeService_.configurationPath();
    const auto diskRevision = diskRevision_;
    pendingImport_ = pending;
    importSnapshot_ = {};
    importPreview_ = {};
    importError_.clear();
    saving_ = true;
    emit savingChanged();
    emit importStateChanged();
    setPersistenceStatus(QStringLiteral("正在保存当前编辑并替换布局…"));
    const QPointer<MenuController> receiver(this);
    QMetaObject::invokeMethod(
        saveWorker_,
        [receiver, pending, current, path, diskRevision] {
            const bool dirty = pending->revision > diskRevision->load(std::memory_order_acquire);
            pending->outcome = commitLayoutImport(
                current, pending->replacement,
                [path](const QJsonObject& snapshot) { return LayoutStore(path).save(snapshot); },
                dirty);
            if (pending->outcome.currentSaved || pending->outcome.result.success)
                diskRevision->store(pending->revision, std::memory_order_release);
            pending->done.store(true, std::memory_order_release);
            if (receiver)
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, pending] {
                        if (receiver)
                            receiver->completeLayoutImport(pending);
                    },
                    Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return true;
}

void MenuController::completeLayoutImport(const std::shared_ptr<PendingLayoutImport>& pending) {
    if (pendingImport_ != pending || !pending->done.load(std::memory_order_acquire))
        return;
    pendingImport_.reset();
    saving_ = false;
    emit savingChanged();
    const auto& outcome = pending->outcome;
    savedRevision_ = std::max(savedRevision_, diskRevision_->load(std::memory_order_acquire));
    if (outcome.result.success) {
        // This immutable snapshot was strictly validated before either disk write.
        const auto restored = prototypeService_.restoreSnapshot(pending->replacement);
        Q_ASSERT(restored.success);
        darkTheme_ = pending->replacement.value(QStringLiteral("preferences"))
                         .toObject()
                         .value(QStringLiteral("theme"))
                         .toString() != QLatin1String("light");
        ++revision_;
        savedRevision_ = revision_;
        diskRevision_->store(revision_, std::memory_order_release);
        setEditMode(true);
        setSearchQuery({});
        refreshGroups();
        emit darkThemeChanged();
        setPersistenceStatus(QStringLiteral("布局已替换并保存。"));
        emit importStateChanged();
        emit layoutImportSucceeded();
        return;
    }
    importError_ = outcome.result.message;
    if (outcome.result.readOnly && !readOnly_) {
        readOnly_ = true;
        setEditMode(false);
        emit readOnlyChanged();
    }
    setPersistenceStatus(outcome.currentSaved
                             ? QStringLiteral("此前编辑已保存，导入失败；当前布局保持不变。")
                             : QStringLiteral("导入失败，当前布局保持不变。"),
                         true);
    emit importStateChanged();
}

} // namespace xdstarter
