#include "services/application_catalog.h"
#include "services/prototype_service.h"

#include <QCoreApplication>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <memory>
#include <utility>

namespace {

using namespace xdstarter;

LaunchTarget shortcut(const QString& name) {
    const auto path = QStringLiteral("C:\\Applications\\%1.lnk").arg(name);
    const auto id = applicationTargetId(QStringLiteral("shortcut"), path);
    return {id, QStringLiteral("shortcut"), name, name.first(1), path, {}, id};
}

struct FakeState {
    QMutex mutex;
    CatalogSnapshot snapshot;
    QHash<QString, LaunchResult> availability;
    LaunchResult defaultAvailability{true, {}, {}};
    LaunchResult launchResult{true, {}, {}};
    int iconEdge = 96;
    QRgb iconColor = qRgb(180, 30, 40);
    bool nullIcons = false;
    std::atomic<int> scans{0};
    std::atomic<int> iconLoads{0};
    std::atomic<int> checks{0};
    std::atomic<int> launches{0};
    std::atomic<int> guiThreadCalls{0};
};

class FakeApplicationPlatform final : public ApplicationPlatform {
  public:
    explicit FakeApplicationPlatform(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}

    CatalogSnapshot scanApplications() override {
        recordThread();
        ++state_->scans;
        QMutexLocker lock(&state_->mutex);
        return state_->snapshot;
    }

    QImage loadIcon(const LaunchTarget&, int size) override {
        recordThread();
        ++state_->iconLoads;
        QMutexLocker lock(&state_->mutex);
        if (state_->nullIcons || size <= 0) {
            return {};
        }
        QImage image(state_->iconEdge, state_->iconEdge, QImage::Format_ARGB32_Premultiplied);
        image.fill(state_->iconColor);
        return image;
    }

    LaunchResult launch(const LaunchTarget&) override {
        recordThread();
        ++state_->launches;
        QMutexLocker lock(&state_->mutex);
        return state_->launchResult;
    }

    LaunchResult checkAvailability(const LaunchTarget& target) override {
        recordThread();
        ++state_->checks;
        QMutexLocker lock(&state_->mutex);
        return state_->availability.value(target.id, state_->defaultAvailability);
    }

  private:
    void recordThread() {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            ++state_->guiThreadCalls;
        }
    }

    std::shared_ptr<FakeState> state_;
};

std::unique_ptr<ApplicationPlatform> fakePlatform(const std::shared_ptr<FakeState>& state) {
    return std::make_unique<FakeApplicationPlatform>(state);
}

int rowFor(QAbstractItemModel* model, const QString& id) {
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row, 0), ApplicationListModel::AppId).toString() == id) {
            return row;
        }
    }
    return -1;
}

class ApplicationCatalogTests final : public QObject {
    Q_OBJECT

  private slots:
    void scanNormalizesAndRunsInBackground();
    void failedRefreshRetainsPreviousEntries();
    void iconRequestsAreDeduplicatedAndCacheIsBounded();
    void refreshReloadsIconsAndMetadata();
    void launchChecksAvailabilityAndReportsCompletion();
    void registeredMissingTargetsPreserveLayout();
    void targetJsonRoundTripKeepsLaunchKindsSeparate();
};

void ApplicationCatalogTests::scanNormalizesAndRunsInBackground() {
    auto state = std::make_shared<FakeState>();
    const auto alpha = shortcut(QStringLiteral("Alpha"));
    const auto beta = shortcut(QStringLiteral("Beta"));
    auto invalid = alpha;
    invalid.id = QStringLiteral("mismatched-id");
    state->snapshot = {{beta, alpha, alpha, invalid}, {}, true};
    ApplicationCatalog catalog(fakePlatform(state));
    QSignalSpy changed(&catalog, &ApplicationCatalog::catalogChanged);

    catalog.refreshApplications();
    QVERIFY(catalog.busy());
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QCOMPARE(state->scans.load(), 1);
    QCOMPARE(state->guiThreadCalls.load(), 0);
    QVERIFY(changed.count() >= 2);
    auto* model = catalog.applicationsModel();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->data(model->index(0, 0), ApplicationListModel::DisplayName).toString(),
             alpha.name);
    QCOMPARE(model->data(model->index(1, 0), ApplicationListModel::AppId).toString(), beta.id);
    QCOMPARE(model->rowCount(model->index(0, 0)), 0);
    QVERIFY(catalog.containsApplication(alpha.id));
    QVERIFY(!catalog.containsApplication(invalid.id));
    QVERIFY(catalog.availability(alpha.id).success);
    catalog.setPinnedIds({alpha.id});
    QVERIFY(model->data(model->index(0, 0), ApplicationListModel::Pinned).toBool());
    QVERIFY(!model->data(model->index(1, 0), ApplicationListModel::Pinned).toBool());
    QVERIFY(!model->data(QModelIndex{}, ApplicationListModel::AppId).isValid());
}

void ApplicationCatalogTests::failedRefreshRetainsPreviousEntries() {
    auto state = std::make_shared<FakeState>();
    const auto application = shortcut(QStringLiteral("Existing"));
    state->snapshot = {{application}, {}, true};
    ApplicationCatalog catalog(fakePlatform(state));
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    {
        QMutexLocker lock(&state->mutex);
        state->snapshot = {{}, {QStringLiteral("fixture scan failure")}, false};
    }
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QCOMPARE(catalog.applicationsModel()->rowCount(), 1);
    QCOMPARE(catalog.target(application.id).name, application.name);
    QVERIFY(catalog.containsApplication(application.id));
    QVERIFY(catalog.status().contains(QStringLiteral("fixture scan failure")));
    QCOMPARE(state->scans.load(), 2);
    QCOMPARE(state->guiThreadCalls.load(), 0);
    {
        QMutexLocker lock(&state->mutex);
        state->snapshot = {{}, {}, true};
    }
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QCOMPARE(catalog.applicationsModel()->rowCount(), 0);
    QVERIFY(!catalog.containsApplication(application.id));
    QVERIFY(catalog.target(application.id).id.isEmpty());
}

void ApplicationCatalogTests::iconRequestsAreDeduplicatedAndCacheIsBounded() {
    auto state = std::make_shared<FakeState>();
    QVector<LaunchTarget> entries;
    for (int index = 0; index < 10; ++index) {
        entries.append(shortcut(QStringLiteral("Icon-%1").arg(index)));
    }
    state->snapshot = {entries, {}, true};
    state->iconEdge = 512;
    ApplicationCatalog catalog(fakePlatform(state));
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QSignalSpy ready(&catalog, &ApplicationCatalog::iconReady);
    for (int index = 0; index < 10; ++index) {
        catalog.requestIcon(entries.first().id);
        catalog.queueIconRequest(entries.first().id);
    }
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
    QCOMPARE(state->iconLoads.load(), 1);
    QVERIFY(!catalog.cachedIcon(entries.first().id).isNull());
    catalog.requestIcon(entries.first().id);
    QCoreApplication::processEvents();
    QCOMPARE(state->iconLoads.load(), 1);
    for (qsizetype index = 1; index < entries.size(); ++index) {
        catalog.requestIcon(entries[index].id);
    }
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), entries.size(), 5000);
    qsizetype cachedBytes = 0;
    for (const auto& entry : entries) {
        cachedBytes += catalog.cachedIcon(entry.id).sizeInBytes();
    }
    QVERIFY(cachedBytes > 0);
    QVERIFY(cachedBytes <= 8 * 1024 * 1024);
    QVERIFY(!catalog.cachedIcon(entries.last().id).isNull());
    QCOMPARE(state->guiThreadCalls.load(), 0);

    auto nullState = std::make_shared<FakeState>();
    nullState->snapshot = {{entries.first()}, {}, true};
    nullState->nullIcons = true;
    ApplicationCatalog nullCatalog(fakePlatform(nullState));
    nullCatalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!nullCatalog.busy(), 3000);
    QSignalSpy nullReady(&nullCatalog, &ApplicationCatalog::iconReady);
    nullCatalog.requestIcon(entries.first().id);
    QTRY_COMPARE_WITH_TIMEOUT(nullReady.count(), 1, 3000);
    nullCatalog.requestIcon(entries.first().id);
    QCoreApplication::processEvents();
    QCOMPARE(nullState->iconLoads.load(), 1);
}

void ApplicationCatalogTests::refreshReloadsIconsAndMetadata() {
    auto state = std::make_shared<FakeState>();
    auto application = shortcut(QStringLiteral("Original"));
    state->snapshot = {{application}, {}, true};
    ApplicationCatalog catalog(fakePlatform(state));
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QSignalSpy ready(&catalog, &ApplicationCatalog::iconReady);
    catalog.requestIcon(application.id);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
    QCOMPARE(catalog.cachedIcon(application.id).pixel(0, 0), qRgb(180, 30, 40));
    application.name = QStringLiteral("Updated display name");
    {
        QMutexLocker lock(&state->mutex);
        state->snapshot = {{application}, {}, true};
        state->iconColor = qRgb(20, 60, 210);
    }
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QCOMPARE(catalog.target(application.id).name, application.name);
    const int row = rowFor(catalog.applicationsModel(), application.id);
    QVERIFY(row >= 0);
    QCOMPARE(
        catalog.applicationsModel()
            ->data(catalog.applicationsModel()->index(row, 0), ApplicationListModel::DisplayName)
            .toString(),
        application.name);
    catalog.requestIcon(application.id);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 3000);
    QCOMPARE(catalog.cachedIcon(application.id).pixel(0, 0), qRgb(20, 60, 210));
    QCOMPARE(state->iconLoads.load(), 2);
    {
        QMutexLocker lock(&state->mutex);
        state->iconColor = qRgb(40, 190, 70);
    }
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    catalog.requestIcon(application.id);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 3000);
    QCOMPARE(catalog.target(application.id).name, application.name);
    QCOMPARE(catalog.cachedIcon(application.id).pixel(0, 0), qRgb(40, 190, 70));
    QCOMPARE(state->iconLoads.load(), 3);
    QCOMPARE(state->guiThreadCalls.load(), 0);
}

void ApplicationCatalogTests::launchChecksAvailabilityAndReportsCompletion() {
    auto state = std::make_shared<FakeState>();
    const auto application = shortcut(QStringLiteral("Launch"));
    state->snapshot = {{application}, {}, true};
    state->defaultAvailability = {false, QStringLiteral("fixture target missing"),
                                  QStringLiteral("missing")};
    ApplicationCatalog catalog(fakePlatform(state));
    catalog.refreshApplications();
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.busy(), 3000);
    QSignalSpy succeeded(&catalog, &ApplicationCatalog::launchSucceeded);
    QSignalSpy failed(&catalog, &ApplicationCatalog::launchFailed);
    QVERIFY(catalog.launchApplication(application.id));
    QVERIFY(catalog.launchBusy());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(!catalog.launchBusy());
    QCOMPARE(state->launches.load(), 0);
    QCOMPARE(state->checks.load(), 1);
    QVERIFY(!catalog.availability(application.id).success);
    QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("fixture target missing"));
    QVERIFY(!catalog.launchApplication(QStringLiteral("unknown")));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(state->launches.load(), 0);
    {
        QMutexLocker lock(&state->mutex);
        state->defaultAvailability = {true, {}, {}};
        state->launchResult = {false, QStringLiteral("fixture launch denied"),
                               QStringLiteral("denied")};
    }
    QVERIFY(catalog.launchApplication(application.id));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 3, 3000);
    QCOMPARE(state->launches.load(), 1);
    QCOMPARE(succeeded.count(), 0);
    QVERIFY(catalog.availability(application.id).success);
    const int availableRow = rowFor(catalog.applicationsModel(), application.id);
    QVERIFY(availableRow >= 0);
    QVERIFY(catalog.applicationsModel()
                ->data(catalog.applicationsModel()->index(availableRow, 0),
                       ApplicationListModel::Available)
                .toBool());
    {
        QMutexLocker lock(&state->mutex);
        state->launchResult = {true, {}, {}};
        state->defaultAvailability = {false, QStringLiteral("fixture temporarily missing"),
                                      QStringLiteral("missing")};
    }
    QVERIFY(catalog.launchApplication(application.id));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 4, 3000);
    QVERIFY(!catalog.availability(application.id).success);
    QCOMPARE(state->launches.load(), 1);
    {
        QMutexLocker lock(&state->mutex);
        state->defaultAvailability = {true, {}, {}};
    }
    QVERIFY(catalog.launchApplication(application.id));
    QVERIFY(!catalog.launchApplication(application.id));
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 3000);
    QVERIFY(!catalog.launchBusy());
    QVERIFY(catalog.availability(application.id).success);
    QCOMPARE(state->launches.load(), 2);
    QCOMPARE(state->checks.load(), 4);
    QCOMPARE(state->guiThreadCalls.load(), 0);
}

void ApplicationCatalogTests::registeredMissingTargetsPreserveLayout() {
    auto state = std::make_shared<FakeState>();
    state->defaultAvailability = {false, QStringLiteral("fixture shortcut removed"),
                                  QStringLiteral("missing")};
    const auto application = shortcut(QStringLiteral("Persisted"));
    const TileGrid grid;
    PrototypeService layout(grid, {}, false);
    const auto pinned = layout.pinApplication(applicationTargetJson(application));
    QVERIFY(pinned.success);
    const auto before = layout.snapshot();
    ApplicationCatalog catalog(fakePlatform(state));
    QSignalSpy availabilityChanged(&catalog, &ApplicationCatalog::availabilityChanged);
    catalog.registerTargets(before.value(QStringLiteral("targets")).toArray());
    QTRY_VERIFY_WITH_TIMEOUT(availabilityChanged.count() > 0, 3000);
    QVERIFY(!catalog.availability(application.id).success);
    QCOMPARE(catalog.target(application.id).shortcutPath, application.shortcutPath);
    QVERIFY(!catalog.containsApplication(application.id));
    QCOMPARE(layout.snapshot(), before);
    QVERIFY(layout.isApplicationPinned(application.id));
    QVERIFY(layout.unpinTile(pinned.tileId).success);
    catalog.registerTargets(layout.snapshot().value(QStringLiteral("targets")).toArray());
    QVERIFY(catalog.target(application.id).id.isEmpty());
    QCOMPARE(catalog.applicationsModel()->rowCount(), 0);
    QCOMPARE(state->guiThreadCalls.load(), 0);
}

void ApplicationCatalogTests::targetJsonRoundTripKeepsLaunchKindsSeparate() {
    const auto application = shortcut(QStringLiteral("Round trip"));
    const auto object = applicationTargetJson(application);
    QCOMPARE(applicationTargetFromJson(object).shortcutPath, application.shortcutPath);
    QVERIFY(!object.contains(QStringLiteral("aumid")));
    LaunchTarget packaged;
    packaged.type = QStringLiteral("packaged");
    packaged.name = QStringLiteral("Packaged");
    packaged.symbol = QStringLiteral("P");
    packaged.aumid = QStringLiteral("Example.Package_123456789!App");
    packaged.id = applicationTargetId(packaged.type, packaged.aumid);
    packaged.iconKey = packaged.id;
    const auto packagedObject = applicationTargetJson(packaged);
    QCOMPARE(applicationTargetFromJson(packagedObject).aumid, packaged.aumid);
    QVERIFY(!packagedObject.contains(QStringLiteral("shortcutPath")));
    QVERIFY(isValidApplicationTarget(applicationTargetFromJson(packagedObject)));
}

} // namespace

QTEST_GUILESS_MAIN(ApplicationCatalogTests)

#include "application_catalog_tests.moc"
