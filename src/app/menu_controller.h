#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

namespace xdstarter {

class PlatformIntegration;
class MotionPreference;
class ContrastPreference;
class PrototypeService;
class ApplicationCatalog;
class DesktopShellService;
class SearchService;
struct LayoutResult;
struct LayoutImportPreview;
struct PendingLayoutImport;

class IconVersionState final : public QObject {
    Q_OBJECT
    Q_PROPERTY(quint64 version READ version NOTIFY versionChanged)

  public:
    explicit IconVersionState(quint64 version, QObject* parent = nullptr)
        : QObject(parent), version_(version) {}
    [[nodiscard]] quint64 version() const { return version_; }
    void setVersion(quint64 version) {
        if (version_ == version)
            return;
        version_ = version;
        emit versionChanged();
    }

  signals:
    void versionChanged();

  private:
    quint64 version_;
};

class MenuController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MenuController is supplied by the application")

    Q_PROPERTY(QVariantList tiles READ tiles NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(QVariantMap tileTransitions READ tileTransitions NOTIFY groupsChanged)
    Q_PROPERTY(QVariantMap visualTokens READ visualTokens CONSTANT)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString platformStatus READ platformStatus CONSTANT)
    Q_PROPERTY(bool simulatedPlatform READ simulatedPlatform CONSTANT)
    Q_PROPERTY(int previewWidth READ previewWidth NOTIFY groupsChanged)
    Q_PROPERTY(int previewHeight READ previewHeight NOTIFY groupsChanged)
    Q_PROPERTY(int gridPitch READ gridPitch CONSTANT)
    Q_PROPERTY(int gridGap READ gridGap CONSTANT)
    Q_PROPERTY(int minimumGridWidth READ minimumGridWidth CONSTANT)
    Q_PROPERTY(int defaultGridWidth READ defaultGridWidth CONSTANT)
    Q_PROPERTY(int gridColumns READ gridColumns NOTIFY groupsChanged)
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(bool keyboardNavigation READ keyboardNavigation NOTIFY keyboardNavigationChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    Q_PROPERTY(QString themePreference READ themePreference WRITE setThemePreference NOTIFY themePreferenceChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY animationsEnabledChanged)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY highContrastChanged)
    Q_PROPERTY(QVariantMap highContrastColors READ highContrastColors NOTIFY highContrastChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(bool layoutImportSupported READ layoutImportSupported CONSTANT)
    Q_PROPERTY(bool importBusy READ importBusy NOTIFY importStateChanged)
    Q_PROPERTY(bool importCommitting READ importCommitting NOTIFY importStateChanged)
    Q_PROPERTY(QVariantMap importPreview READ importPreview NOTIFY importStateChanged)
    Q_PROPERTY(QString importError READ importError NOTIFY importStateChanged)
    Q_PROPERTY(QString operationMessage READ operationMessage NOTIFY operationMessageChanged)
    Q_PROPERTY(bool operationError READ operationError NOTIFY operationMessageChanged)
    Q_PROPERTY(QString persistenceStatus READ persistenceStatus NOTIFY persistenceStatusChanged)
    Q_PROPERTY(bool persistenceError READ persistenceError NOTIFY persistenceStatusChanged)
    Q_PROPERTY(QAbstractItemModel* applications READ applications CONSTANT)
    Q_PROPERTY(bool catalogBusy READ catalogBusy NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogStatus READ catalogStatus NOTIFY catalogChanged)
    Q_PROPERTY(bool catalogError READ catalogError NOTIFY catalogChanged)
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
    Q_PROPERTY(int maximumTileDisplayNameLength READ maximumTileDisplayNameLength CONSTANT)
    Q_PROPERTY(bool desktopActionBusy READ desktopActionBusy NOTIFY desktopActionStateChanged)
    Q_PROPERTY(bool desktopActionError READ desktopActionError NOTIFY desktopActionStateChanged)
    Q_PROPERTY(
        QString desktopActionStatus READ desktopActionStatus NOTIFY desktopActionStateChanged)

  public:
    MenuController(PrototypeService& prototypeService, const PlatformIntegration& platform,
                   QVariantMap visualTokens, QObject* parent = nullptr,
                   ApplicationCatalog* catalog = nullptr,
                   DesktopShellService* desktopShell = nullptr,
                   MotionPreference* motionPreference = nullptr,
                   ContrastPreference* contrastPreference = nullptr);
    ~MenuController() override;

    [[nodiscard]] QVariantList tiles() const;
    [[nodiscard]] QVariantList groups() const;
    [[nodiscard]] QVariantMap tileTransitions() const { return tileTransitions_; }
    [[nodiscard]] QVariantMap visualTokens() const;
    [[nodiscard]] QString platformName() const;
    [[nodiscard]] QString platformStatus() const;
    [[nodiscard]] bool simulatedPlatform() const;
    [[nodiscard]] int previewWidth() const;
    [[nodiscard]] int previewHeight() const;
    Q_INVOKABLE QString neighborTileId(const QString& tileId, const QString& direction) const;
    [[nodiscard]] int gridPitch() const;
    [[nodiscard]] int gridGap() const;
    [[nodiscard]] int minimumGridWidth() const;
    [[nodiscard]] int defaultGridWidth() const;
    [[nodiscard]] int gridColumns() const;
    Q_INVOKABLE bool setGridColumnsForWidth(qreal availableWidth);
    [[nodiscard]] bool editMode() const;
    [[nodiscard]] bool keyboardNavigation() const { return keyboardNavigation_; }
    void setEditMode(bool enabled);
    [[nodiscard]] bool darkTheme() const;
    void setDarkTheme(bool enabled);
    [[nodiscard]] QString themePreference() const;
    void setThemePreference(const QString& preference);
    [[nodiscard]] bool animationsEnabled() const { return animationsEnabled_; }
    [[nodiscard]] bool highContrast() const { return highContrast_; }
    [[nodiscard]] QVariantMap highContrastColors() const { return highContrastColors_; }
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] bool saving() const;
    [[nodiscard]] QString operationMessage() const;
    [[nodiscard]] bool operationError() const;
    [[nodiscard]] QString persistenceStatus() const;
    [[nodiscard]] bool persistenceError() const;
    Q_INVOKABLE QVariantMap previewPlacement(const QString& tileId, const QString& groupId,
                                             int column, int row) const;
    Q_INVOKABLE bool moveTile(const QString& tileId, const QString& groupId, int column, int row);
    Q_INVOKABLE bool moveTileBy(const QString& tileId, int dx, int dy);
    Q_INVOKABLE bool resizeTile(const QString& tileId, const QString& size);
    Q_INVOKABLE void requestSave();
    bool flushSave();
    [[nodiscard]] bool layoutImportSupported() const;
    [[nodiscard]] bool importBusy() const;
    [[nodiscard]] bool importCommitting() const;
    [[nodiscard]] QVariantMap importPreview() const;
    [[nodiscard]] QString importError() const;
    Q_INVOKABLE bool stageLayoutImport(const QUrl& source);
    Q_INVOKABLE bool stageBackupRestore();
    Q_INVOKABLE bool confirmLayoutImport();
    Q_INVOKABLE void cancelLayoutImport();
    [[nodiscard]] QAbstractItemModel* applications() const;
    [[nodiscard]] bool catalogBusy() const;
    [[nodiscard]] QString catalogStatus() const;
    [[nodiscard]] bool catalogError() const;
    [[nodiscard]] bool launchBusy() const;
    [[nodiscard]] int iconRevision() const;
    [[nodiscard]] QVariantMap iconVersions() const;
    Q_INVOKABLE QObject* iconVersionState(const QString& id) const;
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
    Q_INVOKABLE bool isApplicationPinned(const QString& appId) const;
    Q_INVOKABLE QVariantMap previewApplicationPlacement(const QString& appId,
                                                        const QString& groupId, int column,
                                                        int row) const;
    Q_INVOKABLE bool pinApplicationAt(const QString& appId, const QString& groupId, int column,
                                      int row);
    Q_INVOKABLE bool unpinApplication(const QString& appId);
    Q_INVOKABLE bool unpinTile(const QString& tileId);
    Q_INVOKABLE bool renameTile(const QString& tileId, const QString& displayName);
    Q_INVOKABLE bool resetTileDisplayName(const QString& tileId);
    [[nodiscard]] int maximumTileDisplayNameLength() const;
    [[nodiscard]] int maximumGroups() const;
    [[nodiscard]] int maximumGroupNameLength() const;
    Q_INVOKABLE QString createGroup(const QString& name);
    Q_INVOKABLE bool renameGroup(const QString& groupId, const QString& name);
    Q_INVOKABLE bool moveGroupBy(const QString& groupId, int delta);
    Q_INVOKABLE bool removeGroup(const QString& groupId);
    [[nodiscard]] bool desktopActionBusy() const;
    [[nodiscard]] QString desktopActionStatus() const;
    [[nodiscard]] bool desktopActionError() const;
    Q_INVOKABLE bool requestDesktopAction(const QString& actionId);
    Q_INVOKABLE void requestExit();

  signals:
    void keyboardNavigationChanged();
    void groupsChanged();
    void editModeChanged();
    void darkThemeChanged();
    void themePreferenceChanged();
    void animationsEnabledChanged();
    void highContrastChanged();
    void readOnlyChanged();
    void savingChanged();
    void importStateChanged();
    void layoutImportSucceeded();
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

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void applyThemePreference(const QString& preference);
    void refreshGroups();
    void advanceIconVersion(const QString& id);
    void retireIconVersionState(const QString& id);
    void setOperationMessage(QString message, bool error = false);
    void setPersistenceStatus(QString status, bool error = false);
    void scheduleSave();
    void startSave();
    void completeSave(quint64 revision, bool success, const QString& message, bool readOnly);
    bool stageLayoutImportPath(const QString& path, bool backup);
    void completeLayoutImport(const std::shared_ptr<PendingLayoutImport>& pending);
    bool rejectImportMutation();
    bool canImport();
    [[nodiscard]] QJsonObject currentSnapshot() const;
    [[nodiscard]] QVariantMap findTile(const QString& tileId) const;
    void rebuildSearchEntries();
    void refreshSearchResults();
    bool canEditGroups();
    bool applyTileNameResult(const LayoutResult& result, const QJsonObject& before,
                             const QString& successMessage);
    bool applyGroupResult(const LayoutResult& result, const QJsonObject& before,
                          const QString& successMessage);
    PrototypeService& prototypeService_;
    QVariantList tiles_;
    QVariantList groups_;
    QVariantMap tileTransitions_;
    QVariantMap visualTokens_;
    QString platformName_;
    QString platformStatus_;
    bool simulatedPlatform_;
    int previewWidth_ = 0;
    int previewHeight_ = 0;
    bool editMode_ = true;
    bool keyboardNavigation_ = false;
    bool darkTheme_ = true;
    bool animationsEnabled_ = true;
    bool highContrast_ = false;
    QVariantMap highContrastColors_;
    QString themePreference_ = QStringLiteral("dark");
    Qt::ColorScheme systemColorScheme_ = Qt::ColorScheme::Unknown;
    bool readOnly_ = false;
    bool saving_ = false;
    QString operationMessage_;
    bool operationError_ = false;
    bool persistenceError_ = false;
    bool desktopActionError_ = false;
    QString persistenceStatus_;
    QTimer saveTimer_;
    QThread saveThread_;
    QObject* saveWorker_ = nullptr;
    quint64 revision_ = 0;
    quint64 savedRevision_ = 0;
    quint64 saveEpoch_ = 0;
    // Disk progress is published by the one save worker before GUI callbacks run.
    std::shared_ptr<std::atomic<quint64>> diskRevision_ = std::make_shared<std::atomic<quint64>>(0);
    quint64 importGeneration_ = 0;
    quint64 importPreviewRevision_ = 0;
    bool importReading_ = false;
    QVariantMap importPreview_;
    QString importError_;
    QJsonObject importSnapshot_;
    std::shared_ptr<PendingLayoutImport> pendingImport_;
    ApplicationCatalog* catalog_ = nullptr;
    QPointer<DesktopShellService> desktopShell_;
    std::unique_ptr<SearchService> search_;
    QVariantList searchSourceState_;
    QVariantList searchResults_;
    QSet<QString> searchTargetIds_;
    int iconRevision_ = 0;
    QVariantMap iconVersions_;
    QHash<QString, IconVersionState*> iconVersionStates_;
    QSet<QString> pinnedTargetIds_;
    quint64 nextIconVersion_ = 0;
};

} // namespace xdstarter
