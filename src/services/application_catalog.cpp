#include "application_catalog.h"
#include "pinyin_data.h"

#include <algorithm>

#include <QMutexLocker>
#include <QPointer>
#include <QUrl>

#include <utility>

namespace xdstarter {

namespace {
bool sameTarget(const LaunchTarget& a, const LaunchTarget& b) {
    return a.id == b.id && a.type == b.type && a.name == b.name &&
           a.shortcutPath == b.shortcutPath && a.aumid == b.aumid && a.iconKey == b.iconKey &&
           a.parentFolderId == b.parentFolderId;
}
} // namespace

QJsonObject applicationTargetJson(const LaunchTarget& target) {
    QJsonObject object{{QStringLiteral("id"), target.id},
                       {QStringLiteral("type"), target.type},
                       {QStringLiteral("name"), target.name},
                       {QStringLiteral("symbol"), target.symbol},
                       {QStringLiteral("iconKey"), target.iconKey}};
    if (target.type == QLatin1String("shortcut"))
        object.insert(QStringLiteral("shortcutPath"), target.shortcutPath);
    if (target.type == QLatin1String("packaged"))
        object.insert(QStringLiteral("aumid"), target.aumid);
    return object;
}

LaunchTarget applicationTargetFromJson(const QJsonObject& object) {
    return {object.value(QStringLiteral("id")).toString(),
            object.value(QStringLiteral("type")).toString(),
            object.value(QStringLiteral("name")).toString(),
            object.value(QStringLiteral("symbol")).toString(),
            object.value(QStringLiteral("shortcutPath")).toString(),
            object.value(QStringLiteral("aumid")).toString(),
            object.value(QStringLiteral("iconKey")).toString()};
}

ApplicationListModel::ApplicationListModel(QObject* parent) : QAbstractListModel(parent) {}
int ApplicationListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(applications_.size());
}
QVariant ApplicationListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= applications_.size())
        return {};
    const auto& entry = applications_[index.row()];
    switch (role) {
    case Section:
        return applicationSection(entry.name);
    case ParentFolderId:
        return entry.parentFolderId;
    case AppId:
        return entry.id;
    case DisplayName:
        return entry.name;
    case Kind:
        return entry.type;
    case IconSource:
        return QStringLiteral("image://application-icons/%1?v=%2")
            .arg(entry.id)
            .arg(iconVersions_.value(entry.id));
    case Available:
        return availability_.value(entry.id, true);
    case Pinned:
        return pinnedIds_.contains(entry.id);
    default:
        return {};
    }
}
QHash<int, QByteArray> ApplicationListModel::roleNames() const {
    return {{AppId, "appId"},           {DisplayName, "displayName"},      {Kind, "kind"},
            {IconSource, "iconSource"}, {Available, "available"},          {Pinned, "pinned"},
            {Section, "sectionKey"},    {ParentFolderId, "parentFolderId"}};
}
void ApplicationListModel::replace(QVector<LaunchTarget> applications) {
    beginResetModel();
    applications_ = std::move(applications);
    availability_.clear();
    iconVersions_.clear();
    for (const auto& entry : applications_)
        iconVersions_[entry.id] = ++nextIconVersion_;
    endResetModel();
}
void ApplicationListModel::setPinnedIds(QSet<QString> ids) {
    const auto before = pinnedIds_;
    pinnedIds_ = std::move(ids);
    for (int row = 0; row < applications_.size(); ++row) {
        const auto& id = applications_[row].id;
        if (before.contains(id) != pinnedIds_.contains(id))
            emit dataChanged(index(row), index(row), {Pinned});
    }
}
void ApplicationListModel::iconReady(const QString& id) {
    iconVersions_[id] = ++nextIconVersion_;
    for (int row = 0; row < applications_.size(); ++row) {
        if (applications_[row].id == id)
            emit dataChanged(index(row), index(row), {IconSource});
    }
}
QVector<LaunchTarget> ApplicationListModel::entries() const { return applications_; }

void ApplicationListModel::setAvailability(const QString& id, bool available) {
    if (availability_.value(id, true) == available)
        return;
    availability_.insert(id, available);
    for (int row = 0; row < applications_.size(); ++row) {
        if (applications_[row].id == id)
            emit dataChanged(index(row), index(row), {Available});
    }
}

QString applicationSection(const QString& name) {
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("🌐");
    const auto scalars = trimmed.toUcs4();
    const char32_t first = scalars.first();
    if (QChar::isDigit(first))
        return QStringLiteral("#");
    const auto normalized = trimmed.normalized(QString::NormalizationForm_D).toUpper();
    const auto character = normalized.front();
    if (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
        return QString(character);
    const auto readings = pinyinReadings(first);
    if (!readings.isEmpty())
        return readings.first().first(1).toUpper();
    return QStringLiteral("🌐");
}

ApplicationBrowserModel::ApplicationBrowserModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::modelReset, this, &ApplicationBrowserModel::sectionsChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this,
            &ApplicationBrowserModel::sectionsChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this,
            &ApplicationBrowserModel::sectionsChanged);
}
bool ApplicationBrowserModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
    return sourceModel()
               ->index(row, 0, parent)
               .data(ApplicationListModel::ParentFolderId)
               .toString() == (folders_.isEmpty() ? QString{} : folders_.last());
}
bool ApplicationBrowserModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const QString a = left.data(ApplicationListModel::Section).toString();
    const QString b = right.data(ApplicationListModel::Section).toString();
    if (a != b)
        return a == QLatin1String("#") ||
               (b != QLatin1String("#") && (b == QStringLiteral("🌐") || a < b));
    const int order =
        QString::localeAwareCompare(left.data(ApplicationListModel::DisplayName).toString(),
                                    right.data(ApplicationListModel::DisplayName).toString());
    return order ? order < 0
                 : left.data(ApplicationListModel::AppId).toString() <
                       right.data(ApplicationListModel::AppId).toString();
}
QString ApplicationBrowserModel::folderName() const { return name_; }
bool ApplicationBrowserModel::inFolder() const { return !folders_.isEmpty(); }
QStringList ApplicationBrowserModel::sections() const {
    QStringList result;
    for (int row = 0; row < rowCount(); ++row) {
        const auto section = index(row, 0).data(ApplicationListModel::Section).toString();
        if (!result.contains(section))
            result.append(section);
    }
    return result;
}
int ApplicationBrowserModel::sectionIndex(const QString& section) const {
    for (int row = 0; row < rowCount(); ++row) {
        if (index(row, 0).data(ApplicationListModel::Section).toString() == section)
            return row;
    }
    return -1;
}
bool ApplicationBrowserModel::openFolder(const QString& id) {
    if (folders_.size() >= 12)
        return false;
    for (int row = 0; row < rowCount(); ++row) {
        const auto entry = index(row, 0);
        if (entry.data(ApplicationListModel::AppId).toString() != id ||
            entry.data(ApplicationListModel::Kind).toString() != QLatin1String("folder"))
            continue;
        name_ = entry.data(ApplicationListModel::DisplayName).toString();
        folders_.append(id);
        invalidateFilter();
        emit folderChanged();
        emit sectionsChanged();
        return true;
    }
    return false;
}
void ApplicationBrowserModel::reconcile() {
    if (!folders_.isEmpty()) {
        bool found = false;
        for (int row = 0; row < sourceModel()->rowCount(); ++row) {
            const auto entry = sourceModel()->index(row, 0);
            if (entry.data(ApplicationListModel::AppId).toString() == folders_.last() &&
                entry.data(ApplicationListModel::Kind).toString() == QLatin1String("folder")) {
                name_ = entry.data(ApplicationListModel::DisplayName).toString();
                found = true;
                break;
            }
        }
        if (!found) {
            folders_.clear();
            name_.clear();
        }
    }
    invalidateFilter();
    emit folderChanged();
    emit sectionsChanged();
}
void ApplicationBrowserModel::back() {
    if (!folders_.isEmpty())
        folders_.removeLast();
    name_.clear();
    reconcile();
}
void ApplicationBrowserModel::resetFolder() {
    folders_.clear();
    name_.clear();
    reconcile();
}

ApplicationCatalog::ApplicationCatalog(std::unique_ptr<ApplicationPlatform> platform,
                                       QObject* parent)
    : QObject(parent), platform_(std::move(platform)), model_(this), browser_(this) {
    browser_.setSourceModel(&model_);
    browser_.sort(0);
    worker_ = new QObject;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.start();
}
ApplicationCatalog::~ApplicationCatalog() {
    thread_.quit();
    thread_.wait();
}
QAbstractItemModel* ApplicationCatalog::applicationsModel() { return &browser_; }
QVector<LaunchTarget> ApplicationCatalog::catalogApplications() const {
    auto entries = model_.entries();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [](const auto& entry) { return entry.type == QLatin1String("folder"); }),
        entries.end());
    return entries;
}
bool ApplicationCatalog::busy() const { return busy_; }
bool ApplicationCatalog::launchBusy() const { return launchBusy_; }
QString ApplicationCatalog::status() const { return status_; }
bool ApplicationCatalog::error() const { return error_; }
LaunchTarget ApplicationCatalog::target(const QString& id) const { return targets_.value(id); }
bool ApplicationCatalog::containsApplication(const QString& id) const {
    return catalogIds_.contains(id);
}
LaunchResult ApplicationCatalog::availability(const QString& id) const {
    return availability_.value(
        id, {false, QStringLiteral("正在检查应用是否可用…"), QStringLiteral("pending")});
}
QImage ApplicationCatalog::cachedIcon(const QString& id) const {
    QMutexLocker locker(&iconMutex_);
    const auto* image = icons_.object(id);
    return image ? *image : QImage{};
}
void ApplicationCatalog::setPinnedIds(const QSet<QString>& ids) { model_.setPinnedIds(ids); }

void ApplicationCatalog::refreshApplications() {
    if (busy_ || !platform_)
        return;
    busy_ = true;
    error_ = false;
    ++generation_;
    status_ = QStringLiteral("正在读取应用目录…");
    emit catalogChanged();
    const QPointer<ApplicationCatalog> receiver(this);
    auto* platform = platform_.get();
    QMetaObject::invokeMethod(
        worker_,
        [receiver, platform] {
            const auto snapshot = platform->scanApplications();
            if (!receiver)
                return;
            QMetaObject::invokeMethod(
                receiver,
                [receiver, snapshot] {
                    if (!receiver)
                        return;
                    auto& self = *receiver;
                    self.busy_ = false;
                    self.error_ = !snapshot.success || !snapshot.warnings.isEmpty();
                    if (snapshot.success) {
                        const auto entries = normalizedApplications(snapshot.applications);
                        ++self.iconGeneration_;
                        {
                            QMutexLocker locker(&self.iconMutex_);
                            self.icons_.clear();
                        }
                        self.catalogIds_.clear();
                        for (const auto& entry : entries) {
                            if (!sameTarget(self.targets_.value(entry.id), entry)) {
                                QMutexLocker locker(&self.iconMutex_);
                                self.icons_.remove(entry.id);
                            }
                            self.targets_.insert(entry.id, entry);
                            self.catalogIds_.insert(entry.id);
                            self.availability_.insert(entry.id, {true, {}, {}});
                        }
                        self.pruneTargets();
                        self.model_.replace(entries);
                        self.browser_.reconcile();
                        self.status_ = QStringLiteral("已读取 %1 个应用。").arg(entries.size());
                    } else {
                        self.status_ = QStringLiteral("应用目录读取失败，保留已有列表。");
                    }
                    if (!snapshot.warnings.isEmpty())
                        self.status_ +=
                            QLatin1Char(' ') + snapshot.warnings.join(QStringLiteral("；"));
                    emit self.catalogChanged();
                    self.checkRegisteredTargets();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void ApplicationCatalog::registerTargets(const QJsonArray& targets) {
    bool changed = false;
    QSet<QString> ids;
    for (const auto& value : targets) {
        const auto entry = applicationTargetFromJson(value.toObject());
        if (isValidApplicationTarget(entry)) {
            ids.insert(entry.id);
            // Current catalog metadata wins; the persisted launch descriptor remains usable if
            // absent.
            if (!catalogIds_.contains(entry.id) && !sameTarget(targets_.value(entry.id), entry)) {
                changed = true;
                targets_.insert(entry.id, entry);
                availability_.remove(entry.id);
                QMutexLocker locker(&iconMutex_);
                icons_.remove(entry.id);
            }
        }
    }
    changed = changed || ids != persistedIds_;
    persistedIds_ = std::move(ids);
    pruneTargets();
    if (changed)
        ++generation_;
    if (changed && !busy_)
        checkRegisteredTargets();
}

void ApplicationCatalog::pruneTargets() {
    for (auto it = targets_.begin(); it != targets_.end();) {
        if (!catalogIds_.contains(it.key()) && !persistedIds_.contains(it.key())) {
            availability_.remove(it.key());
            QMutexLocker locker(&iconMutex_);
            icons_.remove(it.key());
            it = targets_.erase(it);
        } else {
            ++it;
        }
    }
}

void ApplicationCatalog::checkRegisteredTargets() {
    if (!platform_)
        return;
    QVector<LaunchTarget> pending;
    for (const auto& id : persistedIds_)
        if (targets_.contains(id))
            pending.append(targets_.value(id));
    if (pending.isEmpty()) {
        emit availabilityChanged();
        return;
    }
    const QPointer<ApplicationCatalog> receiver(this);
    auto* platform = platform_.get();
    const auto generation = generation_;
    QMetaObject::invokeMethod(
        worker_,
        [receiver, platform, pending, generation] {
            QHash<QString, LaunchResult> results;
            for (const auto& entry : pending)
                results.insert(entry.id, platform->checkAvailability(entry));
            if (!receiver)
                return;
            QMetaObject::invokeMethod(
                receiver,
                [receiver, results, generation] {
                    if (!receiver || receiver->generation_ != generation)
                        return;
                    for (auto it = results.cbegin(); it != results.cend(); ++it) {
                        receiver->availability_.insert(it.key(), it.value());
                        receiver->model_.setAvailability(it.key(), it.value().success);
                    }
                    emit receiver->availabilityChanged();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void ApplicationCatalog::queueIconRequest(const QString& id) {
    const QPointer<ApplicationCatalog> receiver(this);
    QMetaObject::invokeMethod(
        this,
        [receiver, id] {
            if (receiver)
                receiver->requestIcon(id);
        },
        Qt::QueuedConnection);
}
void ApplicationCatalog::requestIcon(const QString& id) {
    if (!platform_ || !targets_.contains(id) || pendingIcons_.contains(id) ||
        !cachedIcon(id).isNull())
        return;
    pendingIcons_.insert(id);
    const auto entry = targets_.value(id);
    const auto iconGeneration = iconGeneration_;
    const QPointer<ApplicationCatalog> receiver(this);
    auto* platform = platform_.get();
    QMetaObject::invokeMethod(
        worker_,
        [receiver, platform, entry, iconGeneration] {
            auto image = platform->loadIcon(entry, 96);
            if (image.isNull())
                image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            if (image.size() == QSize(1, 1))
                image.fill(Qt::transparent);
            if (!receiver)
                return;
            QMetaObject::invokeMethod(
                receiver,
                [receiver, entry, image, iconGeneration] {
                    if (!receiver)
                        return;
                    const auto& id = entry.id;
                    receiver->pendingIcons_.remove(id);
                    if (!receiver->targets_.contains(id))
                        return;
                    if (receiver->iconGeneration_ != iconGeneration ||
                        !sameTarget(receiver->targets_.value(id), entry)) {
                        receiver->requestIcon(id);
                        return;
                    }
                    {
                        QMutexLocker locker(&receiver->iconMutex_);
                        receiver->icons_.insert(id, new QImage(image), int(image.sizeInBytes()));
                    }
                    receiver->model_.iconReady(id);
                    emit receiver->iconReady(id);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

bool ApplicationCatalog::launchApplication(const QString& id) {
    if (launchBusy_ || !platform_ || !targets_.contains(id)) {
        emit launchFailed(launchBusy_ ? QStringLiteral("应用正在启动，请稍候。")
                                      : QStringLiteral("找不到应用的启动目标。"));
        return false;
    }
    const auto entry = targets_.value(id);
    launchBusy_ = true;
    emit launchStateChanged();
    const QPointer<ApplicationCatalog> receiver(this);
    auto* platform = platform_.get();
    QMetaObject::invokeMethod(
        worker_,
        [receiver, platform, entry] {
            const auto available = platform->checkAvailability(entry);
            const auto result = available.success ? platform->launch(entry) : available;
            if (!receiver)
                return;
            QMetaObject::invokeMethod(
                receiver,
                [receiver, entry, available, result] {
                    if (!receiver)
                        return;
                    receiver->launchBusy_ = false;
                    emit receiver->launchStateChanged();
                    if (sameTarget(receiver->targets_.value(entry.id), entry)) {
                        receiver->availability_.insert(entry.id, available);
                        receiver->model_.setAvailability(entry.id, available.success);
                        emit receiver->availabilityChanged();
                    }
                    if (result.success)
                        emit receiver->launchSucceeded();
                    else {
                        emit receiver->launchFailed(result.message);
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return true;
}

} // namespace xdstarter
