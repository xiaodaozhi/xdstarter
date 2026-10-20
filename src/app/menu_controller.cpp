#include "menu_controller.h"

#include "platform/platform_integration.h"
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/layout_store.h"
#include "services/prototype_service.h"
#include "xdstarter/grid_tokens.h"

#include <QPointer>

#include <algorithm>
#include <optional>
#include <utility>

namespace xdstarter {
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
      simulatedPlatform_(platform.isSimulated()), catalog_(catalog), desktopShell_(desktopShell) {
    if (catalog_)
        catalog_->refreshApplications();
    readOnly_ = prototypeService.isReadOnly();
    darkTheme_ = prototypeService.snapshot()
                     .value(QStringLiteral("preferences"))
                     .toObject()
                     .value(QStringLiteral("theme"))
                     .toString() != QLatin1String("light");
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
            emit catalogChanged();
        });
        connect(catalog_, &ApplicationCatalog::availabilityChanged, this,
                &MenuController::refreshGroups);
        connect(catalog_, &ApplicationCatalog::iconReady, this, [this](const QString& id) {
            if (!pinnedTargetIds_.contains(id))
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
            setOperationMessage(message);
            emit launchFailed(message);
        });
    }
    if (desktopShell_) {
        connect(desktopShell_, &DesktopShellService::stateChanged, this, [this] {
            setOperationMessage(desktopActionStatus());
            emit desktopActionStateChanged();
        });
        connect(desktopShell_, &DesktopShellService::desktopActionSucceeded, this,
                [this](const QString&) { emit desktopActionSucceeded(); });
        connect(desktopShell_, &DesktopShellService::desktopActionFailed, this,
                [this](const QString&, const QString& message) {
                    setOperationMessage(message);
                    emit desktopActionFailed(message);
                });
        connect(desktopShell_, &QObject::destroyed, this,
                &MenuController::desktopActionStateChanged);
    }
}

MenuController::~MenuController() {
    flushSave();
    saveThread_.quit();
    saveThread_.wait();
}

void MenuController::refreshGroups() {
    const auto groups = prototypeService_.previewGroups();
    groups_.clear();
    tiles_.clear();
    previewWidth_ = 0;
    previewHeight_ = 0;
    groups_.reserve(groups.size());
    for (const auto& group : groups) {
        auto tiles = tileValues(group.tiles);
        for (auto& tileValue : tiles) {
            auto tile = tileValue.toMap();
            const auto target =
                prototypeService_.targetForTile(tile.value(QStringLiteral("id")).toString());
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
        if (!pinned.contains(iterator.key())) {
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
int MenuController::gridColumns() const { return grid_tokens::columns; }
bool MenuController::editMode() const { return editMode_; }
bool MenuController::darkTheme() const { return darkTheme_; }
bool MenuController::readOnly() const { return readOnly_; }
bool MenuController::saving() const { return saving_; }
QString MenuController::operationMessage() const { return operationMessage_; }
QString MenuController::persistenceStatus() const { return persistenceStatus_; }

QAbstractItemModel* MenuController::applications() const {
    return catalog_ ? catalog_->applicationsModel() : nullptr;
}
bool MenuController::catalogBusy() const { return catalog_ && catalog_->busy(); }
QString MenuController::catalogStatus() const {
    return catalog_ ? catalog_->status() : QStringLiteral("当前未连接真实应用目录。");
}
bool MenuController::launchBusy() const { return catalog_ && catalog_->launchBusy(); }
int MenuController::iconRevision() const { return iconRevision_; }
QVariantMap MenuController::iconVersions() const { return iconVersions_; }
bool MenuController::desktopActionBusy() const { return desktopShell_ && desktopShell_->busy(); }
QString MenuController::desktopActionStatus() const {
    return desktopShell_ ? desktopShell_->status() : QStringLiteral("当前未连接桌面快捷入口。");
}
bool MenuController::requestDesktopAction(const QString& actionId) {
    if (!desktopShell_) {
        const auto message = QStringLiteral("当前未连接桌面快捷入口。");
        setOperationMessage(message);
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
        setOperationMessage(QStringLiteral("当前未连接真实应用目录。"));
        return false;
    }
    setOperationMessage(QStringLiteral("正在启动应用…"));
    return catalog_->launchApplication(appId);
}

bool MenuController::launchTile(const QString& tileId) {
    if (editMode_)
        return false;
    const auto target = prototypeService_.targetForTile(tileId);
    if (target.value(QStringLiteral("type")).toString() == QLatin1String("prototype")) {
        setOperationMessage(QStringLiteral("这是样例磁贴，不会启动应用。"));
        return false;
    }
    return launchApplication(target.value(QStringLiteral("id")).toString());
}

bool MenuController::pinApplication(const QString& appId, const QString& groupId) {
    if (readOnly_ || !catalog_ || !catalog_->containsApplication(appId)) {
        setOperationMessage(readOnly_ ? QStringLiteral("当前配置只读，不能固定应用。")
                                      : QStringLiteral("应用目录中找不到该应用，请刷新后重试。"));
        return false;
    }
    const auto result =
        prototypeService_.pinApplication(applicationTargetJson(catalog_->target(appId)), groupId);
    setOperationMessage(result.success ? QStringLiteral("应用已固定到磁贴布局。") : result.message);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

bool MenuController::unpinTile(const QString& tileId) {
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能取消固定。"));
        return false;
    }
    const auto result = prototypeService_.unpinTile(tileId);
    setOperationMessage(result.success ? QStringLiteral("已取消固定，不会卸载应用。")
                                       : result.message);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

void MenuController::setEditMode(bool enabled) {
    if (enabled && readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能编辑布局。"));
        return;
    }
    if (editMode_ == enabled)
        return;
    editMode_ = enabled;
    emit editModeChanged();
}

void MenuController::setDarkTheme(bool enabled) {
    if (readOnly_ || darkTheme_ == enabled)
        return;
    darkTheme_ = enabled;
    emit darkThemeChanged();
    scheduleSave();
}

void MenuController::setOperationMessage(QString message) {
    if (operationMessage_ == message)
        return;
    operationMessage_ = std::move(message);
    emit operationMessageChanged();
}

void MenuController::setPersistenceStatus(QString status) {
    if (persistenceStatus_ == status)
        return;
    persistenceStatus_ = std::move(status);
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
    if (readOnly_)
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
    if (readOnly_) {
        setOperationMessage(QStringLiteral("当前配置只读，不能移动磁贴。"));
        return false;
    }
    const auto result = prototypeService_.moveTile(tileId, groupId, column, row);
    setOperationMessage(result.success ? QStringLiteral("磁贴已移动。") : result.message);
    if (!result.success)
        return false;
    refreshGroups();
    scheduleSave();
    return true;
}

bool MenuController::moveTileBy(const QString& tileId, int dx, int dy) {
    if (!editMode_ || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx != 0 && dy != 0)) {
        setOperationMessage(QStringLiteral("请进入编辑布局后按 Ctrl 加方向键移动一格。"));
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
    const auto size = parseSize(sizeNameValue);
    if (!size || readOnly_) {
        setOperationMessage(readOnly_ ? QStringLiteral("当前配置只读，不能调整尺寸。")
                                      : QStringLiteral("未知的磁贴尺寸。"));
        return false;
    }
    const auto result = prototypeService_.resizeTile(tileId, *size);
    setOperationMessage(result.success ? QStringLiteral("磁贴尺寸已切换。") : result.message);
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
    if (!saveWorker_) {
        setPersistenceStatus(QStringLiteral("本次布局仅在内存中保留。"));
        return;
    }
    setPersistenceStatus(QStringLiteral("布局已更改，等待保存。"));
    saveTimer_.start();
}

void MenuController::requestSave() {
    if (readOnly_)
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
    if (readOnly_ || !saveWorker_ || saving_ || revision_ == savedRevision_)
        return;
    saving_ = true;
    emit savingChanged();
    setPersistenceStatus(QStringLiteral("正在保存布局…"));
    const auto snapshot = currentSnapshot();
    const auto path = prototypeService_.configurationPath();
    const auto revision = revision_;
    const QPointer<MenuController> receiver(this);
    QMetaObject::invokeMethod(
        saveWorker_,
        [receiver, snapshot, path, revision] {
            const auto result = LayoutStore(path).save(snapshot);
            if (receiver) {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, result, revision] {
                        if (receiver)
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
    setPersistenceStatus(success ? QStringLiteral("布局与主题已保存。") : message);
    // Retry newer edits after a completed write, but do not loop on an IO failure.
    if (success && savedRevision_ < revision_ && !readOnly_)
        saveTimer_.start();
}

bool MenuController::flushSave() {
    saveTimer_.stop();
    if (readOnly_ || !saveWorker_ || revision_ == savedRevision_)
        return !readOnly_;
    const auto snapshot = currentSnapshot();
    const auto path = prototypeService_.configurationPath();
    LayoutResult result{false, QStringLiteral("布局保存未完成。")};
    QMetaObject::invokeMethod(
        saveWorker_, [&result, snapshot, path] { result = LayoutStore(path).save(snapshot); },
        Qt::BlockingQueuedConnection);
    completeSave(revision_, result.success, result.message, result.readOnly);
    return result.success;
}

} // namespace xdstarter
