#pragma once

#include "platform/application_platform.h"

#include <QAbstractListModel>
#include <QCache>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThread>

namespace xdstarter {

[[nodiscard]] QJsonObject applicationTargetJson(const LaunchTarget& target);
[[nodiscard]] LaunchTarget applicationTargetFromJson(const QJsonObject& object);

class ApplicationListModel final : public QAbstractListModel {
  public:
    enum Role { AppId = Qt::UserRole + 1, DisplayName, Kind, IconSource, Available, Pinned };
    explicit ApplicationListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(QVector<LaunchTarget> applications);
    void setPinnedIds(QSet<QString> ids);
    void iconReady(const QString& id);
    void setAvailability(const QString& id, bool available);
    [[nodiscard]] QVector<LaunchTarget> entries() const;

  private:
    QVector<LaunchTarget> applications_;
    QHash<QString, quint64> iconVersions_;
    quint64 nextIconVersion_ = 0;
    QSet<QString> pinnedIds_;
    QHash<QString, bool> availability_;
};

class ApplicationCatalog final : public QObject {
    Q_OBJECT

  public:
    explicit ApplicationCatalog(std::unique_ptr<ApplicationPlatform> platform,
                                QObject* parent = nullptr);
    ~ApplicationCatalog() override;
    [[nodiscard]] QAbstractItemModel* applicationsModel();
    [[nodiscard]] QVector<LaunchTarget> catalogApplications() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool launchBusy() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] LaunchTarget target(const QString& id) const;
    [[nodiscard]] bool containsApplication(const QString& id) const;
    [[nodiscard]] LaunchResult availability(const QString& id) const;
    [[nodiscard]] QImage cachedIcon(const QString& id) const;
    void refreshApplications();
    void registerTargets(const QJsonArray& targets);
    void setPinnedIds(const QSet<QString>& ids);
    void requestIcon(const QString& id);
    void queueIconRequest(const QString& id);
    bool launchApplication(const QString& id);

  signals:
    void catalogChanged();
    void availabilityChanged();
    void iconReady(const QString& id);
    void launchStateChanged();
    void launchSucceeded();
    void launchFailed(const QString& message);

  private:
    void checkRegisteredTargets();
    void pruneTargets();
    std::unique_ptr<ApplicationPlatform> platform_;
    ApplicationListModel model_;
    QThread thread_;
    QObject* worker_ = nullptr;
    QHash<QString, LaunchTarget> targets_;
    QHash<QString, LaunchResult> availability_;
    QSet<QString> catalogIds_;
    QSet<QString> persistedIds_;
    QSet<QString> pendingIcons_;
    quint64 generation_ = 0;
    quint64 iconGeneration_ = 0;
    mutable QMutex iconMutex_;
    QCache<QString, QImage> icons_{8 * 1024 * 1024};
    bool busy_ = false;
    bool launchBusy_ = false;
    QString status_ = QStringLiteral("尚未读取应用目录。");
};

} // namespace xdstarter
