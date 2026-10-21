#pragma once

#include <QAbstractItemModel>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace xdstarter {

class PlatformIntegration;
class PrototypeService;
class ApplicationCatalog;
class DesktopShellService;
class SearchService;
struct LayoutResult;

class MenuController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MenuController is supplied by the application")

    Q_PROPERTY(QVariantList tiles READ tiles NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(QVariantMap visualTokens READ visualTokens CONSTANT)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString platformStatus READ platformStatus CONSTANT)
    Q_PROPERTY(bool simulatedPlatform READ simulatedPlatform CONSTANT)
    Q_PROPERTY(int previewWidth READ previewWidth NOTIFY groupsChanged)
    Q_PROPERTY(int previewHeight READ previewHeight NOTIFY groupsChanged)
    Q_PROPERTY(int gridPitch READ gridPitch CONSTANT)
    Q_PROPERTY(int gridColumns READ gridColumns CONSTANT)
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(QString operationMessage READ operationMessage NOTIFY operationMessageChanged)
    Q_PROPERTY(QString persistenceStatus READ persistenceStatus NOTIFY persistenceStatusChanged)
    Q_PROPERTY(QAbstractItemModel* applications READ applications CONSTANT)
    Q_PROPERTY(bool catalogBusy READ catalogBusy NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogStatus READ catalogStatus NOTIFY catalogChanged)
    Q_PROPERTY(bool launchBusy READ launchBusy NOTIFY launchStateChanged)
    Q_PROPERTY(int iconRevision READ iconRevision NOTIFY iconRevisionChanged)
    Q_PROPERTY(QVariantMap iconVersions READ iconVersions NOTIFY iconVersionsChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchChanged)
    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY searchChanged)
    Q_PROPERTY(QString searchStatus READ searchStatus NOTIFY searchChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(int maximumGroups READ maximumGroups CONSTANT)
    Q_PROPERTY(int maximumGroupNameLength READ maximumGroupNameLength CONSTANT)
    Q_PROPERTY(bool desktopActionBusy READ desktopActionBusy NOTIFY desktopActionStateChanged)
    Q_PROPERTY(
        QString desktopActionStatus READ desktopActionStatus NOTIFY desktopActionStateChanged)

  public:
    MenuController(PrototypeService& prototypeService, const PlatformIntegration& platform,
                   QVariantMap visualTokens, QObject* parent = nullptr,
                   ApplicationCatalog* catalog = nullptr,
                   DesktopShellService* desktopShell = nullptr);
    ~MenuController() override;

    [[nodiscard]] QVariantList tiles() const;
    [[nodiscard]] QVariantList groups() const;
    [[nodiscard]] QVariantMap visualTokens() const;
    [[nodiscard]] QString platformName() const;
    [[nodiscard]] QString platformStatus() const;
    [[nodiscard]] bool simulatedPlatform() const;
    [[nodiscard]] int previewWidth() const;
    [[nodiscard]] int previewHeight() const;
    Q_INVOKABLE QString neighborTileId(const QString& tileId, const QString& direction) const;
    [[nodiscard]] int gridPitch() const;
    [[nodiscard]] int gridColumns() const;
    [[nodiscard]] bool editMode() const;
    void setEditMode(bool enabled);
    [[nodiscard]] bool darkTheme() const;
    void setDarkTheme(bool enabled);
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] bool saving() const;
    [[nodiscard]] QString operationMessage() const;
    [[nodiscard]] QString persistenceStatus() const;
    Q_INVOKABLE QVariantMap previewPlacement(const QString& tileId, const QString& groupId,
                                             int column, int row) const;
    Q_INVOKABLE bool moveTile(const QString& tileId, const QString& groupId, int column, int row);
    Q_INVOKABLE bool moveTileBy(const QString& tileId, int dx, int dy);
    Q_INVOKABLE bool resizeTile(const QString& tileId, const QString& size);
    Q_INVOKABLE void requestSave();
    bool flushSave();
    [[nodiscard]] QAbstractItemModel* applications() const;
    [[nodiscard]] bool catalogBusy() const;
    [[nodiscard]] QString catalogStatus() const;
    [[nodiscard]] bool launchBusy() const;
    [[nodiscard]] int iconRevision() const;
    [[nodiscard]] QVariantMap iconVersions() const;
    [[nodiscard]] QString searchQuery() const;
    void setSearchQuery(const QString& query);
    [[nodiscard]] bool searchActive() const;
    [[nodiscard]] bool searchBusy() const;
    [[nodiscard]] QString searchStatus() const;
    [[nodiscard]] QVariantList searchResults() const;
    Q_INVOKABLE bool launchSearchResult(const QString& appId);
    Q_INVOKABLE void refreshApplications();
    Q_INVOKABLE bool launchApplication(const QString& appId);
    Q_INVOKABLE bool launchTile(const QString& tileId);
    Q_INVOKABLE bool pinApplication(const QString& appId, const QString& groupId = {});
    Q_INVOKABLE bool unpinTile(const QString& tileId);
    [[nodiscard]] int maximumGroups() const;
    [[nodiscard]] int maximumGroupNameLength() const;
    Q_INVOKABLE QString createGroup(const QString& name);
    Q_INVOKABLE bool renameGroup(const QString& groupId, const QString& name);
    Q_INVOKABLE bool moveGroupBy(const QString& groupId, int delta);
    Q_INVOKABLE bool removeGroup(const QString& groupId);
    [[nodiscard]] bool desktopActionBusy() const;
    [[nodiscard]] QString desktopActionStatus() const;
    Q_INVOKABLE bool requestDesktopAction(const QString& actionId);
    Q_INVOKABLE void requestExit();

  signals:
    void groupsChanged();
    void editModeChanged();
    void darkThemeChanged();
    void readOnlyChanged();
    void savingChanged();
    void operationMessageChanged();
    void persistenceStatusChanged();
    void catalogChanged();
    void launchStateChanged();
    void iconRevisionChanged();
    void iconVersionsChanged();
    void searchQueryChanged();
    void searchChanged();
    void searchResultsAboutToChange();
    void searchResultsChanged();
    void launchSucceeded();
    void launchFailed(const QString& message);
    void desktopActionStateChanged();
    void desktopActionSucceeded();
    void desktopActionFailed(const QString& message);
    void exitRequested();

  private:
    void refreshGroups();
    void setOperationMessage(QString message);
    void setPersistenceStatus(QString status);
    void scheduleSave();
    void startSave();
    void completeSave(quint64 revision, bool success, const QString& message, bool readOnly);
    [[nodiscard]] QJsonObject currentSnapshot() const;
    [[nodiscard]] QVariantMap findTile(const QString& tileId) const;
    void rebuildSearchEntries();
    void refreshSearchResults();
    bool canEditGroups();
    bool applyGroupResult(const LayoutResult& result, const QJsonObject& before,
                          const QString& successMessage);
    PrototypeService& prototypeService_;
    QVariantList tiles_;
    QVariantList groups_;
    QVariantMap visualTokens_;
    QString platformName_;
    QString platformStatus_;
    bool simulatedPlatform_;
    int previewWidth_ = 0;
    int previewHeight_ = 0;
    bool editMode_ = false;
    bool darkTheme_ = true;
    bool readOnly_ = false;
    bool saving_ = false;
    QString operationMessage_;
    QString persistenceStatus_;
    QTimer saveTimer_;
    QThread saveThread_;
    QObject* saveWorker_ = nullptr;
    quint64 revision_ = 0;
    quint64 savedRevision_ = 0;
    ApplicationCatalog* catalog_ = nullptr;
    QPointer<DesktopShellService> desktopShell_;
    std::unique_ptr<SearchService> search_;
    QVariantList searchSourceState_;
    QVariantList searchResults_;
    QSet<QString> searchTargetIds_;
    int iconRevision_ = 0;
    QVariantMap iconVersions_;
    QSet<QString> pinnedTargetIds_;
    quint64 nextIconVersion_ = 0;
};

} // namespace xdstarter
