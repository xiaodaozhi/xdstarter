#include "native_chrome_probe.h"
#include "visual_acceptance_probe.h"
#include "ui_search_benchmark.h"

#include "app/application_icon_provider.h"
#include "app/interface_font.h"
#include "app/menu_controller.h"
#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "platform/desktop_platform.h"
#include "platform/platform_integration.h"
#include "platform/motion_preference.h"
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/prototype_service.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QLockFile>
#include <QMap>
#include <QMatrix4x4>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRawFont>
#include <QSGRendererInterface>
#include <QScreen>
#include <QStyleHints>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace {

class ManualMotionPreference final : public xdstarter::MotionPreference {
  public:
    explicit ManualMotionPreference(bool enabled) : MotionPreference(enabled) {}
    void setEnabled(bool enabled) { publishAnimationsEnabled(enabled); }
};

xdstarter::LaunchTarget catalogTarget(const QString& name, const QString& identity,
                                     const QString& type = QStringLiteral("shortcut")) {
    xdstarter::LaunchTarget target;
    target.id = xdstarter::applicationTargetId(type, identity);
    target.type = type;
    target.name = name;
    target.symbol = name.first(1);
    target.shortcutPath = type == QStringLiteral("shortcut") ? identity : QString{};
    target.aumid = type == QStringLiteral("packaged") ? identity : QString{};
    target.iconKey = target.id;
    return target;
}

struct CatalogTestState {
    QVector<xdstarter::LaunchTarget> applications{
        catalogTarget(QStringLiteral("A 测试快捷方式"), QStringLiteral("C:/Fixture/A.lnk")),
        catalogTarget(QStringLiteral("B 测试打包应用"), QStringLiteral("Fixture.Package_123!App"),
                      QStringLiteral("packaged"))};
    QHash<QString, QString> equivalentTargetIds;
    std::atomic_bool includeApplications{true};
    std::atomic_bool scanSucceeds{true};
    std::atomic_bool scanWarns{false};
    std::atomic_bool launchSucceeds{false};
    std::atomic_bool workerOnly{true};
    std::atomic_int launches{0};
    std::atomic_int scans{0};
    std::atomic_int icons{0};
};

class CatalogTestPlatform final : public xdstarter::ApplicationPlatform {
  public:
    explicit CatalogTestPlatform(std::shared_ptr<CatalogTestState> state)
        : state_(std::move(state)) {}

    xdstarter::CatalogSnapshot scanApplications() override {
        recordThread();
        ++state_->scans;
        return {
            state_->includeApplications ? state_->applications : QVector<xdstarter::LaunchTarget>{},
            state_->scanWarns ? QStringList{QStringLiteral("测试目录部分不可用")} : QStringList{},
            state_->scanSucceeds, state_->equivalentTargetIds};
    }

    QImage loadIcon(const xdstarter::LaunchTarget&, int size) override {
        recordThread();
        ++state_->icons;
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QStringLiteral("#36D6CB")));
        return image;
    }

    xdstarter::LaunchResult launch(const xdstarter::LaunchTarget&) override {
        recordThread();
        ++state_->launches;
        return state_->launchSucceeds
                   ? xdstarter::LaunchResult{true, {}, {}}
                   : xdstarter::LaunchResult{false, QStringLiteral("测试启动失败"),
                                              QStringLiteral("fixture_launch_failed")};
    }

    xdstarter::LaunchResult checkAvailability(const xdstarter::LaunchTarget&) override {
        recordThread();
        return state_->includeApplications
                   ? xdstarter::LaunchResult{true, {}, {}}
                   : xdstarter::LaunchResult{false, QStringLiteral("测试目标已移除"),
                                              QStringLiteral("fixture_missing")};
    }

  private:
    void recordThread() {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            state_->workerOnly = false;
        }
    }
    std::shared_ptr<CatalogTestState> state_;
};

struct DesktopTestState {
    std::atomic_bool succeeds{false};
    std::atomic_bool workerOnly{true};
    QMutex mutex;
    QStringList requestedActions;
};

class DesktopTestPlatform final : public xdstarter::DesktopPlatform {
  public:
    explicit DesktopTestPlatform(std::shared_ptr<DesktopTestState> state)
        : state_(std::move(state)) {}

    xdstarter::DesktopActionResult openDesktopAction(const QString& actionId) override {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            state_->workerOnly = false;
        }
        {
            QMutexLocker lock(&state_->mutex);
            state_->requestedActions.append(actionId);
        }
        return state_->succeeds
                   ? xdstarter::DesktopActionResult{true, QStringLiteral("已打开测试入口"), {}}
                   : xdstarter::DesktopActionResult{false, QStringLiteral("测试入口不可用"),
                                                    QStringLiteral("fixture_unavailable")};
    }

    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override {
        return {};
    }

  private:
    std::shared_ptr<DesktopTestState> state_;
};

QQuickItem* visualItem(QQuickItem* parent, const QString& name) {
    if (!parent) {
        return nullptr;
    }
    if (parent->objectName() == name) {
        return parent;
    }
    // Repeater delegates follow visual ownership, which need not match QObject ownership.
    for (auto* child : parent->childItems()) {
        if (auto* found = visualItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

QRectF itemGeometry(const QQuickItem* item) {
    return {item->x(), item->y(), item->width(), item->height()};
}

QList<QPointF> tileCorners(const QQuickItem* tile) {
    return {{0, 0}, {tile->width(), 0}, {tile->width(), tile->height()}, {0, tile->height()}};
}

QVector3D unprojectedPressPoint(const QMatrix4x4& matrix, const QQuickItem* tile,
                               const QPointF& point) {
    const auto clip = matrix * QVector4D(point.x(), point.y(), 0, 1);
    // The matrix translates the perspective result back to the tile center.
    // Remove that homogeneous translation before measuring its plane normal.
    return {clip.x() - float(tile->width() / 2) * clip.w(),
            clip.y() - float(tile->height() / 2) * clip.w(), clip.z()};
}

qreal tilePressAngleDegrees(const QMatrix4x4& matrix, const QQuickItem* tile) {
    const auto origin = unprojectedPressPoint(matrix, tile, {0, 0});
    const auto horizontal = unprojectedPressPoint(matrix, tile, {tile->width(), 0}) - origin;
    const auto vertical = unprojectedPressPoint(matrix, tile, {0, tile->height()}) - origin;
    const auto normal = QVector3D::crossProduct(horizontal, vertical).normalized();
    return qRadiansToDegrees(std::atan2(std::hypot(normal.x(), normal.y()), normal.z()));
}

QJsonObject rectangleJson(const QRectF& rectangle) {
    return {{QStringLiteral("x"), rectangle.x()},
            {QStringLiteral("y"), rectangle.y()},
            {QStringLiteral("width"), rectangle.width()},
            {QStringLiteral("height"), rectangle.height()}};
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software:
        return QStringLiteral("Software");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("Direct3D11");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal");
    case QSGRendererInterface::Null:
        return QStringLiteral("Null");
    default:
        return QStringLiteral("Other (%1)").arg(static_cast<int>(api));
    }
}

QString compilerName() {
#if defined(_MSC_FULL_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#elif defined(__clang__)
    return QStringLiteral("Clang ") + QString::fromLatin1(__clang_version__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC ") + QString::fromLatin1(__VERSION__);
#else
    return QStringLiteral("Unknown");
#endif
}

struct PixelRun {
    int start;
    int width;
};

qreal contrastRatio(const QColor& foreground, const QColor& background) {
    const auto luminance = [](const QColor& color) {
        const auto channel = [](qreal value) {
            return value <= 0.04045 ? value / 12.92
                                    : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
               0.0722 * channel(color.blueF());
    };
    const auto first = luminance(foreground);
    const auto second = luminance(background);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

QVector<PixelRun> scanTilePixels(const QImage& image, const QRect& gridPixels, int row,
                                const QColor& background) {
    QVector<PixelRun> runs;
    int start = -1;
    for (int x = gridPixels.left(); x < gridPixels.x() + gridPixels.width(); ++x) {
        const auto pixel = image.pixelColor(x, row);
        const int distance = std::abs(pixel.red() - background.red()) +
                             std::abs(pixel.green() - background.green()) +
                             std::abs(pixel.blue() - background.blue());
        const bool tilePixel = distance > 3;
        if (tilePixel && start < 0) {
            start = x;
        } else if (!tilePixel && start >= 0) {
            runs.append({start - gridPixels.left(), x - start});
            start = -1;
        }
    }
    if (start >= 0) {
        runs.append({start - gridPixels.left(), gridPixels.x() + gridPixels.width() - start});
    }
    return runs;
}

QVector<PixelRun> scanTilePixelsVertically(const QImage& image, const QRect& gridPixels, int column,
                                          const QColor& background) {
    QVector<PixelRun> runs;
    int start = -1;
    for (int y = gridPixels.top(); y < gridPixels.y() + gridPixels.height(); ++y) {
        const auto pixel = image.pixelColor(column, y);
        const int distance = std::abs(pixel.red() - background.red()) +
                             std::abs(pixel.green() - background.green()) +
                             std::abs(pixel.blue() - background.blue());
        const bool tilePixel = distance > 3;
        if (tilePixel && start < 0) {
            start = y;
        } else if (!tilePixel && start >= 0) {
            runs.append({start - gridPixels.top(), y - start});
            start = -1;
        }
    }
    if (start >= 0) {
        runs.append({start - gridPixels.top(), gridPixels.y() + gridPixels.height() - start});
    }
    return runs;
}

bool runsMatch(const QVector<PixelRun>& runs, const QVector<int>& expectedWidths, qreal scale) {
    if (runs.size() != expectedWidths.size()) {
        return false;
    }
    int expectedStart = 0;
    for (qsizetype index = 0; index < runs.size(); ++index) {
        const auto& run = runs[index];
        const int expectedLeft = qRound(expectedStart * scale);
        const int expectedRight = qRound((expectedStart + expectedWidths[index]) * scale);
        if (std::abs(run.start - expectedLeft) > 1 ||
            std::abs(run.width - (expectedRight - expectedLeft)) > 1) {
            return false;
        }
        if (index > 0) {
            const int gap = run.start - runs[index - 1].start - runs[index - 1].width;
            if (std::abs(gap - qRound(8 * scale)) > 1) {
                return false;
            }
        }
        expectedStart += expectedWidths[index] + 8;
    }
    return true;
}

QJsonArray runsJson(const QVector<PixelRun>& runs) {
    QJsonArray values;
    for (const auto& run : runs) {
        values.append(QJsonObject{{QStringLiteral("startPx"), run.start},
                                  {QStringLiteral("widthPx"), run.width}});
    }
    return values;
}

} // namespace

class PrototypeWindowTest : public QObject {
    Q_OBJECT

  private slots:
    void init();
    void cleanup();
    void prototypeWindowLoads();
    void interactionRefinements();
    void approvedSurfaceStates();
    void adaptiveResizeHandles();
    void catalogAlphabetAndFolderOverlay();
    void categoryGlyphsAreCentered_data();
    void categoryGlyphsAreCentered();
    void themeSwitchRetainsGeometry();
    void systemThemePreferenceTracksAndPersists();
    void layoutImportPreservesSystemTheme();
    void reducedMotionFollowsPreference();
    void reducedMotionCompletesFolderNavigation();
    void keyboardAndMouseSelection();
    void tilePressPerspective_data();
    void tilePressPerspective();
    void tilePressVisibleFeedback_data();
    void tilePressVisibleFeedback();
    void tilePressBoundaryGeometry_data();
    void tilePressBoundaryGeometry();
    void tilePressSymmetry();
    void tilePressInterruptedMotion();
    void tilePressSymbolsRemainRendered();
    void tilePressCancellation();
    void tilePressDragAndAlternateInput();
    void smallTilesExposeNames();
    void longNamesRemainBounded();
    void resizePreservesGeometryAndRevealsFocus();
    void responsiveColumnsAndPersistence();
    void contentRightEdges();
    void directionalNavigation();
    void escapeClosesWindow();
    void mouseDragWithinGroup();
    void mouseDragAcrossGroups();
    void mouseDragConflictAndEscape();
    void sizeMenuAndKeyboardEditing();
    void sizeMenuTextRemainsReadable();
    void saveAndRestoreLayout();
    void failedSaveCanBeRetried();
    void futureConfigurationIsReadOnly();
    void applicationDirectoryAndPinning();
    void equivalentShortcutsPreserveOldTilesAndSearch();
    void applicationLaunchLifecycle();
    void searchFocusCompositionAndEscape();
    void searchPreservesBrowsing();
    void searchKeyboardAndLaunchLifecycle();
    void searchRefreshAliasesAndRestart();
    void groupManagementLifecycle();
    void groupNameCompositionAndCancellation();
    void groupCatalogMoveAndRestart();
    void groupChangesPreserveSearchFocus();
    void applicationLayoutRestoresUnavailableTarget();
    void unrelatedApplicationIconPreservesPinnedTile();
    void shortcutRailAssetsAndTheme();
    void desktopShortcutActions();
    void auxiliaryMenusRespectEscape();
    void winKeyCaptureControlRequiresExplicitIntent();
    void startButtonCaptureControl();
    void framelessWindowDismissalIntent();
    void presentationSingleReveal();
    void customChromePixels();
    void shadowMarginDismissal();
    void internalPopupsGuardAndCleanup();
    void dismissalCancelsOnlyDragPreview();
    void dragOutsideReleaseClearsPreview();
    void renderedGroups();
    void highFidelityLayoutAndActions();
    void feedbackFailuresRemainVisible();
    void tileNamesAreLiteralText();
    void tileDisplayNameEditingAndRestart();
    void tileDisplayNameValidationAndCancellation();
    void tileDisplayNameSharedTargetAliases();
    void layoutImportPreviewAndRestore();
    void layoutImportCancellationAndProtection();
    void layoutImportFailureAndExitFence();
    void layoutImportFilePickerOwnership();
    void layoutImportDialogThemeAndKeyboard_data();
    void layoutImportDialogThemeAndKeyboard();
    void layoutImportMinimumPanel();
    void contextMenuShortcutsRespectFocus();
    void tileNameMouseContextFromSearchRestoresFocus();

  private:
    QQuickItem* item(const QString& name) const;
    QVariantMap tileData(const QString& id) const;
    QMap<QString, QRectF> tileGeometries() const;
    void click(QQuickItem* item);
    void openMoreMenu();
    void settleMenus();
    void createWindow(const QString& path = {}, bool sampleMode = true,
                      std::shared_ptr<CatalogTestState> catalogState = {},
                      std::shared_ptr<DesktopTestState> desktopState = {},
                      xdstarter::MotionPreference* motionPreference = nullptr);
    void destroyWindow();
    void beginMouseDrag(const QString& tileId);
    void moveMouseToGrid(const QString& groupId, const QPointF& position);
    QPoint gridPosition(const QString& groupId, const QPointF& position) const;
    QRectF viewportScene() const;
    QObject* sizeMenu() const;
    void openSizeMenu(const QString& tileId, bool keyboard);
    void chooseSize(const QString& itemName, bool keyboard = false);
    void pinFirstApplication(const std::shared_ptr<CatalogTestState>& state);
    void openGroupMenu(const QString& groupId);
    void chooseGroupAction(const QString& itemName);
    void submitGroupName(const QString& name);
    void captureLayoutImportFrame(const QString& state);
    void captureTilePressFrame(const QString& name, const QString& phase, QQuickItem* tile);
    QVariant modelValue(int row, const QByteArray& role) const;

    xdstarter::TileGrid grid_;
    std::unique_ptr<xdstarter::PrototypeService> service_;
    std::unique_ptr<xdstarter::PlatformIntegration> platform_;
    std::unique_ptr<xdstarter::ApplicationCatalog> catalog_;
    std::unique_ptr<xdstarter::DesktopShellService> desktopShell_;
    std::unique_ptr<xdstarter::MenuController> controller_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_ = nullptr;
    QStringList warnings_;
    QVariantMap visualTokens_;
};

void PrototypeWindowTest::interactionRefinements() {
    QVERIFY(controller_->editMode());
    QVERIFY(!item(QStringLiteral("editLayoutButton")));
    QVERIFY(!item(QStringLiteral("browseContextToolbar"))->isVisible());
    auto* field = item(QStringLiteral("searchField"));
    auto* tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(field && tile);
    const auto tileFont =
        visualItem(tile, QStringLiteral("tileLabel"))->property("font").value<QFont>();
    QCOMPARE(field->property("font").value<QFont>().family(), tileFont.family());
    QCOMPARE(item(QStringLiteral("groupTitle-work"))->property("font").value<QFont>().family(),
             tileFont.family());
    QCOMPARE(QFontInfo(tileFont).family(), QGuiApplication::font().family());
    const auto rawFont = QRawFont::fromFont(tileFont);
    QVERIFY(rawFont.familyName() != QStringLiteral("Agency FB"));
    QVERIFY(rawFont.supportsCharacter('A'));
#ifdef Q_OS_WIN
    const auto installedYaHei =
        QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    if (QFileInfo::exists(installedYaHei)) {
        QVERIFY(rawFont.familyName().contains(QStringLiteral("YaHei")));
        QVERIFY(rawFont.supportsCharacter(0x4e2du));
    }
#endif
    click(field);
    QVERIFY(!controller_->keyboardNavigation());
    QTest::keyClick(window_, Qt::Key_Tab);
    QVERIFY(controller_->keyboardNavigation());
    click(field);
    QVERIFY(!controller_->keyboardNavigation());
    QVERIFY(field->setProperty("text", QStringLiteral("工具")));
    auto* clear = item(QStringLiteral("clearSearchButton"));
    QVERIFY(clear && clear->isVisible());
    auto* clearBackground = clear->property("background").value<QQuickItem*>();
    QVERIFY(clearBackground);
    QCOMPARE(clearBackground->property("radius").toReal(), clear->width() / 2);
    click(clear);
    QTRY_VERIFY(!controller_->searchActive());
    auto* tip = tile->findChild<QObject*>(QStringLiteral("tileToolTip"));
    QVERIFY(tip);
    QCOMPARE(tip->property("delay").toInt(), 600);
    QCOMPARE(QFontInfo(tip->property("font").value<QFont>()).family(),
             QGuiApplication::font().family());
    QTest::mouseMove(window_,
                     tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2)).toPoint());
    QTest::qWait(150);
    QVERIFY(!tip->property("visible").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(tip->property("visible").toBool(), 1500);
    QTest::mouseMove(window_, QPoint(1, 1));
    QTRY_VERIFY(!tip->property("visible").toBool());
    openMoreMenu();
    QVERIFY(!item(QStringLiteral("restoreLayoutBackupButton")));
    QTest::keyClick(window_, Qt::Key_Tab);
    QVERIFY(controller_->keyboardNavigation());
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(!controller_->keyboardNavigation());
    openMoreMenu();
    QVERIFY(item(QStringLiteral("newGroupButton"))->isEnabled());
    auto* picker = window_->findChild<QObject*>(QStringLiteral("layoutFileDialog"));
    QVERIFY(picker);
    QVERIFY(!picker->property("title").toString().contains(QStringLiteral("JSON")));
    QTest::keyClick(window_, Qt::Key_Escape);
    settleMenus();
    QVERIFY(window_->setProperty("allowedResizeEdges", int(Qt::BottomEdge | Qt::LeftEdge)));
    for (const auto edge : {Qt::TopEdge, Qt::BottomEdge, Qt::LeftEdge, Qt::RightEdge}) {
        auto* handle = item(QStringLiteral("resizeEdge-%1").arg(int(edge)));
        QVERIFY(handle);
        // MouseArea overrides enabled; QQuickItem::isEnabled() reads the base property.
        QCOMPARE(handle->property("enabled").toBool(),
                 edge == Qt::BottomEdge || edge == Qt::LeftEdge);
    }
    QVERIFY(window_->setProperty("allowedResizeEdges", int(Qt::TopEdge | Qt::RightEdge)));
    openMoreMenu();
    const auto destination = qEnvironmentVariable("XDSTARTER_INTERACTION_CAPTURE_DIR");
    if (!destination.isEmpty()) {
        QVERIFY(QDir().mkpath(destination));
        QTest::qWait(220);
        QVERIFY(window_->grabWindow().save(
            QDir(destination).filePath(QStringLiteral("more-dark.png"))));
        controller_->setDarkTheme(false);
        QTest::qWait(100);
        QVERIFY(window_->grabWindow().save(
            QDir(destination).filePath(QStringLiteral("more-light.png"))));
    }
}

void PrototypeWindowTest::adaptiveResizeHandles() {
    const auto monitor = QRect(0, 0, 1920, 1080);
    for (const QRect bar : {QRect(0, 1040, 1920, 40), QRect(0, 0, 1920, 40), QRect(0, 0, 48, 1080),
                            QRect(1872, 0, 48, 1080)}) {
        for (const bool centered : {false, true}) {
            xdstarter::DesktopAnchorResult anchor;
            anchor.monitorPhysicalRect = monitor;
            anchor.taskbarPhysicalRect = bar;
            anchor.centeredStart = centered;
            const auto edges = xdstarter::desktopResizeEdges(anchor);
            window_->setProperty("allowedResizeEdges", static_cast<int>(edges));
            for (const auto edge : {Qt::TopEdge, Qt::BottomEdge, Qt::LeftEdge, Qt::RightEdge}) {
                auto* handle = item(QStringLiteral("resizeEdge-%1").arg(static_cast<int>(edge)));
                QVERIFY(handle);
                QCOMPARE(handle->property("enabled").toBool(), edges.testFlag(edge));
                if (edges.testFlag(edge)) {
                    const QRectF bounds = handle->mapRectToScene(handle->boundingRect());
                    QVERIFY(bounds.width() > 0 && bounds.height() > 0);
                    const qreal inset = window_->property("shadowInset").toReal();
                    if (edge == Qt::LeftEdge)
                        QCOMPARE(bounds.left(), inset);
                    if (edge == Qt::TopEdge)
                        QCOMPARE(bounds.top(), inset);
                }
            }
            openMoreMenu();
            for (const auto edge : {Qt::TopEdge, Qt::BottomEdge, Qt::LeftEdge, Qt::RightEdge})
                QVERIFY(!item(QStringLiteral("resizeEdge-%1").arg(static_cast<int>(edge)))
                             ->property("enabled")
                             .toBool());
            QTest::keyClick(window_, Qt::Key_Escape);
            settleMenus();
        }
    }
}

void PrototypeWindowTest::categoryGlyphsAreCentered_data() {
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("shortWindow");
    QTest::newRow("dark-normal") << true << false;
    QTest::newRow("light-normal") << false << false;
    QTest::newRow("dark-short") << true << true;
    QTest::newRow("light-short") << false << true;
}

void PrototypeWindowTest::categoryGlyphsAreCentered() {
    QFETCH(bool, dark);
    QFETCH(bool, shortWindow);
    const auto state = std::make_shared<CatalogTestState>();
    state->applications.append(
        catalogTarget(QStringLiteral("1 Tool"), QStringLiteral("C:/Fixture/1.lnk")));
    state->applications.append(
        catalogTarget(QStringLiteral("★ Tool"), QStringLiteral("C:/Fixture/Other.lnk")));
    destroyWindow();
    createWindow({}, true, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 4);
    controller_->setDarkTheme(dark);
    if (shortWindow)
        window_->resize(window_->width(), window_->minimumHeight());
    click(item(QStringLiteral("sectionButton-A")));
    QTRY_VERIFY(window_->property("alphabetOpen").toBool());
    QTest::mouseMove(window_, QPoint(1, 1));
    QTest::qWait(220);
    const auto frame = window_->grabWindow();
    QVERIFY(!frame.isNull());
    const auto destination = qEnvironmentVariable("XDSTARTER_CATEGORY_CAPTURE_DIR");
    if (!destination.isEmpty()) {
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(frame.save(
            QDir(destination)
                .filePath(QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".png"))));
    }
    const auto theme = visualTokens_.value(QStringLiteral("themes"))
                           .toMap()
                           .value(dark ? QStringLiteral("dark") : QStringLiteral("light"))
                           .toMap();
    const QColor surface(theme.value(QStringLiteral("surface")).toString());
    const qreal scaleX = qreal(frame.width()) / window_->width();
    const qreal scaleY = qreal(frame.height()) / window_->height();
    const auto categories =
        QStringLiteral("#ABCDEFGHIJKLMNOPQRSTUVWXYZ").split(QString{}, Qt::SkipEmptyParts) +
        QStringList{QStringLiteral("🌐")};
    int enabledCount = 0;
    int disabledCount = 0;
    for (const auto& category : categories) {
        auto* button = item(QStringLiteral("alphabet-") + category);
        QVERIFY(button && button->isVisible());
        button->isEnabled() ? ++enabledCount : ++disabledCount;
        const auto bounds = button->mapRectToScene(button->boundingRect());
        // Ignore the rounded background; measure actual visible glyph pixels,
        // including disabled items, rather than the font's line-box geometry.
        const QRect interior(
            qCeil((bounds.left() + 4) * scaleX), qCeil((bounds.top() + 4) * scaleY),
            qFloor((bounds.width() - 8) * scaleX), qFloor((bounds.height() - 8) * scaleY));
        QRect ink;
        for (int y = interior.top(); y <= interior.bottom(); ++y) {
            for (int x = interior.left(); x <= interior.right(); ++x) {
                const auto pixel = frame.pixelColor(x, y);
                if (std::max({std::abs(pixel.red() - surface.red()),
                              std::abs(pixel.green() - surface.green()),
                              std::abs(pixel.blue() - surface.blue())}) > 12)
                    ink = ink.united(QRect(x, y, 1, 1));
            }
        }
        QVERIFY2(!ink.isEmpty(), qPrintable(category));
        const QPointF inkCenter((ink.left() + ink.right() + 1) / (2 * scaleX),
                                (ink.top() + ink.bottom() + 1) / (2 * scaleY));
        const auto delta = inkCenter - bounds.center();
        QVERIFY2(std::abs(delta.x()) <= 1.0 && std::abs(delta.y()) <= 1.0,
                 qPrintable(QStringLiteral("%1 glyph offset: (%2, %3) DIP")
                                .arg(category)
                                .arg(delta.x())
                                .arg(delta.y())));
    }
    QVERIFY(enabledCount > 0 && disabledCount > 0);
}

void PrototypeWindowTest::catalogAlphabetAndFolderOverlay() {
    const auto capture = [this](const QString& name) {
        const auto destination = qEnvironmentVariable("XDSTARTER_INTERACTION_CAPTURE_DIR");
        if (destination.isEmpty())
            return;
        QVERIFY(QDir().mkpath(destination));
        QTest::qWait(220);
        QVERIFY(window_->grabWindow().save(QDir(destination).filePath(name)));
    };
    const auto state = std::make_shared<CatalogTestState>();
    xdstarter::LaunchTarget folder;
    folder.type = QStringLiteral("folder");
    folder.name = QStringLiteral("工具");
    folder.shortcutPath = QStringLiteral("C:/Fixture/Tools");
    folder.id = xdstarter::applicationTargetId(folder.type, folder.shortcutPath);
    folder.iconKey = folder.id;
    auto child =
        catalogTarget(QStringLiteral("画图"), QStringLiteral("C:/Fixture/Tools/Paint.lnk"));
    child.parentFolderId = folder.id;
    state->applications.append(folder);
    state->applications.append(child);
    auto emptyFolder = folder;
    emptyFolder.name = QStringLiteral("空目录");
    emptyFolder.shortcutPath += QStringLiteral("/Empty");
    emptyFolder.id = xdstarter::applicationTargetId(emptyFolder.type, emptyFolder.shortcutPath);
    emptyFolder.iconKey = emptyFolder.id;
    emptyFolder.parentFolderId = folder.id;
    state->applications.append(emptyFolder);
    destroyWindow();
    createWindow({}, true, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 3);
    auto* settledTile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(settledTile);
    QTRY_COMPARE(settledTile->mapRectToScene(settledTile->boundingRect()).size(),
                 settledTile->size());
    auto* first = item(QStringLiteral("applicationLaunch-") + state->applications.first().id);
    QVERIFY(first);
    QVERIFY(!item(QStringLiteral("pinButton-") + state->applications.first().id));
    QCOMPARE(first->width(),
             item(QStringLiteral("appRow-") + state->applications.first().id)->width());
    auto* section = item(QStringLiteral("sectionButton-A"));
    QVERIFY(section);
    QCOMPARE(section->size(), QSizeF(32, 32));
    click(section);
    auto* alphabet = item(QStringLiteral("alphabetSelector"));
    auto* viewport = item(QStringLiteral("catalogViewport"));
    QVERIFY(alphabet && viewport);
    QTRY_VERIFY(alphabet->isVisible());
    QCOMPARE(alphabet->size(), viewport->size());
    QVERIFY(item(QStringLiteral("alphabet-G"))->isEnabled());
    QVERIFY(!item(QStringLiteral("alphabet-Z"))->isEnabled());
    capture(QStringLiteral("alphabet-dark.png"));
    const auto initialSize = window_->size();
    window_->resize(window_->width(), window_->minimumHeight());
    auto* lastCategory = item(QStringLiteral("alphabet-🌐"));
    QVERIFY(lastCategory);
    QTRY_VERIFY(viewport->mapRectToScene(viewport->boundingRect())
                    .contains(lastCategory->mapRectToScene(lastCategory->boundingRect())));
    window_->resize(initialSize);
    click(item(QStringLiteral("alphabet-G")));
    QTRY_VERIFY(!window_->property("alphabetOpen").toBool());
    QTRY_VERIFY(!alphabet->isVisible());
    auto* applicationList = item(QStringLiteral("applicationList"));
    QVERIFY(applicationList);
    QVERIFY(QMetaObject::invokeMethod(applicationList, "positionViewAtBeginning"));
    auto* folderButton = item(QStringLiteral("applicationLaunch-") + folder.id);
    QVERIFY(folderButton);
    QTRY_VERIFY(viewport->mapRectToScene(viewport->boundingRect())
                    .contains(first->mapRectToScene(first->boundingRect())));
    QTRY_VERIFY(viewport->mapRectToScene(viewport->boundingRect())
                    .contains(folderButton->mapRectToScene(folderButton->boundingRect())));
    QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier,
                      folderButton->mapToScene(folderButton->boundingRect().center()).toPoint());
    QTRY_VERIFY(folderButton->hasActiveFocus());
    QTest::mouseMove(window_, first->mapToScene(first->boundingRect().center()).toPoint());
    auto* folderTip =
        folderButton->findChild<QObject*>(QStringLiteral("applicationToolTip-") + folder.id);
    QVERIFY(folderTip);
    QTRY_VERIFY(!folderTip->property("visible").toBool());
    auto* applicationTip = first->findChild<QObject*>(QStringLiteral("applicationToolTip-") +
                                                      state->applications.first().id);
    QVERIFY(applicationTip);
    QTRY_VERIFY(first->property("hovered").toBool());
    QVERIFY(folderButton->hasActiveFocus());
    QTest::qWait(300);
    QVERIFY(!applicationTip->property("visible").toBool());
    QTRY_VERIFY(applicationTip->property("visible").toBool());
    QVERIFY(!folderTip->property("visible").toBool());
    click(folderButton);
    QTRY_VERIFY(catalog_->applicationsModel()->property("inFolder").toBool());
    QCOMPARE(catalog_->applicationsModel()->rowCount(), 2);
    auto* overlay = item(QStringLiteral("folderOverlay"));
    QVERIFY(overlay && overlay->isVisible());
    QCOMPARE(overlay->size(), viewport->size());
    QCOMPARE(state->launches.load(), 0);
    capture(QStringLiteral("folder-dark.png"));
    QTRY_VERIFY(item(QStringLiteral("folderBackButton"))->isVisible());
    auto* emptyButton = item(QStringLiteral("applicationLaunch-") + emptyFolder.id);
    QVERIFY(emptyButton);
    click(emptyButton);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 0);
    auto* emptyHint = item(QStringLiteral("emptyCatalogHint"));
    auto* folderHeader = item(QStringLiteral("catalogFolderHeader"));
    auto* backIcon = item(QStringLiteral("folderBackIcon"));
    auto* back = item(QStringLiteral("folderBackButton"));
    QVERIFY(emptyHint && folderHeader && backIcon && back);
    QTRY_VERIFY(emptyHint->isVisible());
    QVERIFY(emptyHint->mapRectToScene(emptyHint->boundingRect()).top() >=
            folderHeader->mapRectToScene(folderHeader->boundingRect()).bottom());
    const auto iconCenter = backIcon->mapToScene(backIcon->boundingRect().center());
    const auto buttonCenter = back->mapToScene(back->boundingRect().center());
    QVERIFY(std::abs(iconCenter.x() - buttonCenter.x()) <= 1);
    QVERIFY(std::abs(iconCenter.y() - buttonCenter.y()) <= 1);
    click(back);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    click(item(QStringLiteral("folderBackButton")));
    QTRY_VERIFY(!catalog_->applicationsModel()->property("inFolder").toBool());
    QCOMPARE(catalog_->applicationsModel()->rowCount(), 3);
    QCOMPARE(state->launches.load(), 0);
}

void PrototypeWindowTest::init() {
    warnings_.clear();
    QFile tokens(QStringLiteral(":/xdstarter/tokens/visual.json"));
    QVERIFY2(tokens.open(QIODevice::ReadOnly), qPrintable(tokens.errorString()));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(tokens.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    visualTokens_ = document.object().toVariantMap();
    createWindow();
}

void PrototypeWindowTest::createWindow(const QString& path, bool sampleMode,
                                      std::shared_ptr<CatalogTestState> catalogState,
                                      std::shared_ptr<DesktopTestState> desktopState,
                                      xdstarter::MotionPreference* motionPreference) {
    service_ = std::make_unique<xdstarter::PrototypeService>(grid_, path, sampleMode);
    platform_ = xdstarter::createPlatformIntegration();
    if (catalogState) {
        catalog_ = std::make_unique<xdstarter::ApplicationCatalog>(
            std::make_unique<CatalogTestPlatform>(std::move(catalogState)));
    }
    if (desktopState) {
        desktopShell_ = std::make_unique<xdstarter::DesktopShellService>(
            std::make_unique<DesktopTestPlatform>(std::move(desktopState)));
    }
    controller_ = std::make_unique<xdstarter::MenuController>(
        *service_, *platform_, visualTokens_, nullptr, catalog_.get(), desktopShell_.get(),
        motionPreference);
    engine_ = std::make_unique<QQmlApplicationEngine>();
    if (catalog_) {
        engine_->addImageProvider(QStringLiteral("application-icons"),
                                 new xdstarter::ApplicationIconProvider(catalog_.get()));
    }
    connect(engine_.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& errors) {
        for (const auto& warning : errors) {
            warnings_.append(warning.toString());
        }
    });
    engine_->setInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(controller_.get())}});
    engine_->loadFromModule("XDStarter", "Main");
    QVERIFY2(!engine_->rootObjects().isEmpty(), qPrintable(warnings_.join(u'\n')));
    window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
    QVERIFY(window_);
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    QTRY_VERIFY(item(QStringLiteral("moreMenuButton")));
    if (sampleMode) {
        QTRY_VERIFY(item(QStringLiteral("tile-work-standard-1")));
    }
    QTest::qWait(220); // Settle the bounded initial presentation before interaction.
    QVERIFY2(warnings_.isEmpty(), qPrintable(warnings_.join(u'\n')));
}

void PrototypeWindowTest::cleanup() {
    destroyWindow();
    QVERIFY2(warnings_.isEmpty(), qPrintable(warnings_.join(u'\n')));
}

void PrototypeWindowTest::destroyWindow() {
    if (window_) {
        window_->close();
    }
    window_ = nullptr;
    engine_.reset();
    controller_.reset();
    desktopShell_.reset();
    catalog_.reset();
    platform_.reset();
    service_.reset();
}

QQuickItem* PrototypeWindowTest::item(const QString& name) const {
    if (!window_)
        return nullptr;
    if (auto* found = visualItem(window_->contentItem(), name))
        return found;
    // A closed Popup.Item is still instantiated but not attached to the overlay.
    return window_->findChild<QQuickItem*>(name);
}

QMap<QString, QRectF> PrototypeWindowTest::tileGeometries() const {
    QMap<QString, QRectF> values;
    for (const auto& groupValue : controller_->groups()) {
        for (const auto& tileValue : groupValue.toMap().value(QStringLiteral("tiles")).toList()) {
            const auto id = tileValue.toMap().value(QStringLiteral("id")).toString();
            const auto name = QStringLiteral("tile-") + id;
            if (const auto* tile = item(name)) {
                values.insert(name, itemGeometry(tile));
            }
        }
    }
    return values;
}

QVariantMap PrototypeWindowTest::tileData(const QString& id) const {
    for (const auto& groupValue : controller_->groups()) {
        for (const auto& tileValue : groupValue.toMap().value(QStringLiteral("tiles")).toList()) {
            auto tile = tileValue.toMap();
            if (tile.value(QStringLiteral("id")).toString() == id) {
                tile.insert(QStringLiteral("groupId"), groupValue.toMap().value(QStringLiteral("id")));
                return tile;
            }
        }
    }
    return {};
}

void PrototypeWindowTest::settleMenus() {
    for (const auto* name :
         {"moreMenu", "tileSizeMenu", "pinApplicationMenu", "groupMenu", "powerMenu"}) {
        auto* menu = window_->findChild<QObject*>(QLatin1String(name));
        if (menu)
            QTRY_VERIFY(menu->property("visible").toBool() == menu->property("opened").toBool());
    }
}

void PrototypeWindowTest::click(QQuickItem* target) {
    settleMenus();
    QVERIFY(target);
    const auto name = target->objectName();
    if (name == QLatin1String("themeButton") || name == QLatin1String("saveLayoutButton") ||
        name == QLatin1String("aboutButton") || name == QLatin1String("newGroupButton")) {
        openMoreMenu();
        QTRY_VERIFY(target->isVisible());
    }
    auto* targetWindow = target->window();
    QVERIFY(targetWindow);
    QTest::mouseClick(targetWindow, Qt::LeftButton, Qt::NoModifier,
                      target->mapToScene(target->boundingRect().center()).toPoint());
    if (name == QLatin1String("themeButton")) {
        auto* submenu = window_->findChild<QObject*>(QStringLiteral("appearanceMenu"));
        QVERIFY(submenu);
        QTRY_VERIFY(submenu->property("opened").toBool());
        click(item(window_->property("darkTheme").toBool() ? QStringLiteral("appearanceLightItem")
                                                           : QStringLiteral("appearanceDarkItem")));
    }
}

void PrototypeWindowTest::openMoreMenu() {
    settleMenus();
    auto* menu = window_->findChild<QObject*>(QStringLiteral("moreMenu"));
    QVERIFY(menu);
    if (!menu->property("opened").toBool()) {
        QVERIFY(QMetaObject::invokeMethod(window_, "openMoreMenu"));
    }
    QTRY_VERIFY(menu->property("opened").toBool());
}

void PrototypeWindowTest::prototypeWindowLoads() {
    QCOMPARE(window_->objectName(), QStringLiteral("prototypeWindow"));
    QVERIFY(window_->isVisible());
    QVERIFY(window_->isExposed());
    QCOMPARE(tileGeometries().size(), 15);
    for (const auto* id : {"work", "daily", "entertainment"}) {
        auto* grid = item(QStringLiteral("grid-") + QLatin1String(id));
        QVERIFY(grid);
        QCOMPARE(grid->size(), QSizeF(552, 104));
    }
    QCOMPARE(item(QStringLiteral("tile-daily-small-1"))->size(), QSizeF(48, 48));
    QCOMPARE(item(QStringLiteral("tile-work-standard-1"))->size(), QSizeF(104, 104));
    QCOMPARE(item(QStringLiteral("tile-daily-wide"))->size(), QSizeF(216, 104));
}

void PrototypeWindowTest::themeSwitchRetainsGeometry() {
    auto* themeButton = item(QStringLiteral("themeButton"));
    const auto geometry = tileGeometries();
    const auto size = window_->size();
    auto* surface = item(QStringLiteral("menuSurface"));
    QVERIFY(surface);
    const auto originalColor = surface->property("color").value<QColor>();
    QVERIFY(window_->property("darkTheme").toBool());
    click(themeButton);
    QTRY_VERIFY(!window_->property("darkTheme").toBool());
    QVERIFY(surface->property("color").value<QColor>() != originalColor);
    QCOMPARE(window_->color().alpha(), 0);
    QCOMPARE(tileGeometries(), geometry);
    QCOMPARE(window_->size(), size);

    openMoreMenu();
    themeButton->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(item(QStringLiteral("appearanceDarkItem"))->isVisible());
    item(QStringLiteral("appearanceDarkItem"))->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(window_->property("darkTheme").toBool());
    QCOMPARE(surface->property("color").value<QColor>(), originalColor);
    QCOMPARE(tileGeometries(), geometry);
    QCOMPARE(window_->size(), size);
}

void PrototypeWindowTest::systemThemePreferenceTracksAndPersists() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("theme-layout.json"));
    destroyWindow();
    createWindow(path);
    auto* hints = QGuiApplication::styleHints();
    QVERIFY(hints);
    const auto sendScheme = [hints](Qt::ColorScheme scheme) {
        return QMetaObject::invokeMethod(hints, "colorSchemeChanged", Qt::DirectConnection,
                                         Q_ARG(Qt::ColorScheme, scheme));
    };
    QVERIFY(sendScheme(Qt::ColorScheme::Light));
    QCOMPARE(controller_->themePreference(), QStringLiteral("dark"));
    QVERIFY(controller_->darkTheme()); // An explicit choice ignores system changes.
    const auto geometry = tileGeometries();
    openMoreMenu();
    auto* themeButton = item(QStringLiteral("themeButton"));
    QVERIFY(themeButton);
    themeButton->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(themeButton->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    auto* systemItem = item(QStringLiteral("appearanceSystemItem"));
    QVERIFY(systemItem);
    QTRY_VERIFY(systemItem->isVisible());
    systemItem->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(systemItem->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(controller_->themePreference(), QStringLiteral("system"));
    QVERIFY(!controller_->darkTheme());
    QCOMPARE(tileGeometries(), geometry);

    // Reopening and reselecting the current choice must keep its check mark.
    for (int attempt = 0; attempt < 2; ++attempt) {
        openMoreMenu();
        themeButton->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(themeButton->hasActiveFocus());
        QTest::keyClick(window_, Qt::Key_Return);
        QTRY_VERIFY(systemItem->isVisible());
        QVERIFY(systemItem->property("checked").toBool());
        QVERIFY(!item(QStringLiteral("appearanceLightItem"))->property("checked").toBool());
        QVERIFY(!item(QStringLiteral("appearanceDarkItem"))->property("checked").toBool());
        systemItem->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(systemItem->hasActiveFocus());
        QTest::keyClick(window_, Qt::Key_Return);
    }
    QVERIFY(controller_->flushSave());
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto beforeChanges = saved.readAll();
    saved.close();
    const auto savedObject = QJsonDocument::fromJson(beforeChanges).object();
    QCOMPARE(savedObject.value(QStringLiteral("schemaVersion")).toInt(), 5);
    QCOMPARE(savedObject.value(QStringLiteral("preferences")).toObject()
                 .value(QStringLiteral("theme")).toString(), QStringLiteral("system"));

    QVERIFY(sendScheme(Qt::ColorScheme::Dark));
    QVERIFY(controller_->darkTheme());
    QVERIFY(sendScheme(Qt::ColorScheme::Light));
    QVERIFY(!controller_->darkTheme());
    QVERIFY(sendScheme(Qt::ColorScheme::Unknown));
    QVERIFY(controller_->darkTheme()); // Unknown has the existing dark fallback.
    QCOMPARE(tileGeometries(), geometry);
    QTest::qWait(650); // More than the save debounce: system events must not write.
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), beforeChanges);
    saved.close();

    destroyWindow();
    createWindow(path);
    QCOMPARE(controller_->themePreference(), QStringLiteral("system"));
    QVERIFY(sendScheme(Qt::ColorScheme::Light));
    QVERIFY(!controller_->darkTheme());
    controller_->setThemePreference(QStringLiteral("dark"));
    QVERIFY(controller_->darkTheme());
    QVERIFY(sendScheme(Qt::ColorScheme::Light));
    QVERIFY(controller_->darkTheme());
    controller_->setThemePreference(QStringLiteral("light"));
    QVERIFY(sendScheme(Qt::ColorScheme::Dark));
    QVERIFY(!controller_->darkTheme());
    controller_->setThemePreference(QStringLiteral("unknown"));
    QCOMPARE(controller_->themePreference(), QStringLiteral("light"));
    QVERIFY(controller_->flushSave());
    destroyWindow();
    createWindow(path);
    QCOMPARE(controller_->themePreference(), QStringLiteral("light"));
    QVERIFY(!controller_->darkTheme());
    openMoreMenu();
    themeButton = item(QStringLiteral("themeButton"));
    themeButton->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(themeButton->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    auto* lightItem = item(QStringLiteral("appearanceLightItem"));
    auto* darkItem = item(QStringLiteral("appearanceDarkItem"));
    QTRY_VERIFY(lightItem->isVisible());
    QVERIFY(lightItem->property("checked").toBool());
    QVERIFY(!darkItem->property("checked").toBool());
    darkItem->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(darkItem->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(controller_->themePreference(), QStringLiteral("dark"));
    openMoreMenu();
    themeButton->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(themeButton->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(darkItem->isVisible());
    QVERIFY(darkItem->property("checked").toBool());
    QVERIFY(!lightItem->property("checked").toBool());
    QVERIFY(!item(QStringLiteral("appearanceSystemItem"))->property("checked").toBool());
}

void PrototypeWindowTest::layoutImportPreservesSystemTheme() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current.json"));
    const auto source = directory.filePath(QStringLiteral("system.json"));
    xdstarter::PrototypeService sample(grid_);
    auto replacement = sample.snapshot();
    replacement.insert(QStringLiteral("preferences"),
                       QJsonObject{{QStringLiteral("theme"), QStringLiteral("system")}});
    QVERIFY(xdstarter::LayoutStore(source).save(replacement).success);
    destroyWindow();
    createWindow(path, false);
    auto* hints = QGuiApplication::styleHints();
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Qt::DirectConnection,
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Light)));
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("layoutImportDialog"));
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto* counts = item(QStringLiteral("layoutImportCounts"));
    QVERIFY(counts);
    QVERIFY(counts->property("text").toString().contains(QStringLiteral("跟随系统")));
    const auto preview = controller_->importPreview();
    QVERIFY(!preview.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Qt::DirectConnection,
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QCOMPARE(controller_->importPreview(), preview);
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Qt::DirectConnection,
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Light)));
    QCOMPARE(controller_->importPreview(), preview);
    click(item(QStringLiteral("layoutImportConfirmButton")));
    QTRY_VERIFY(!controller_->importBusy());
    QCOMPARE(controller_->themePreference(), QStringLiteral("system"));
    QVERIFY(!controller_->darkTheme());
    QCOMPARE(service_->snapshot(), replacement);
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Qt::DirectConnection,
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QVERIFY(controller_->darkTheme());
    QVERIFY(controller_->flushSave());
    destroyWindow();
    createWindow(path, false);
    QCOMPARE(controller_->themePreference(), QStringLiteral("system"));
}

void PrototypeWindowTest::reducedMotionFollowsPreference() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("motion-layout.json"));
    ManualMotionPreference motion(false);
    destroyWindow();
    createWindow(path, true, {}, {}, &motion);
    QVERIFY(!controller_->animationsEnabled());
    QCOMPARE(window_->property("dismissAnimationDuration").toInt(), 0);
    auto* tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(tile);
    QVERIFY(!tile->property("animationsEnabled").toBool());
    const auto geometry = tileGeometries();
    controller_->setDarkTheme(false);
    QVERIFY(controller_->flushSave());
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto original = saved.readAll();
    saved.close();

    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::animationsEnabledChanged);
    motion.setEnabled(true);
    QVERIFY(controller_->animationsEnabled());
    QCOMPARE(window_->property("dismissAnimationDuration").toInt(), 120);
    QVERIFY(QMetaObject::invokeMethod(window_, "stagePresentation"));
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    motion.setEnabled(false);
    QCOMPARE(window_->property("revealProgress").toReal(), qreal(1));
    QCOMPARE(window_->property("dismissAnimationDuration").toInt(), 0);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(tileGeometries(), geometry);
    QTest::qWait(650);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), original);
    saved.close();

    tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(tile);
    const auto position = tile->mapToScene(tile->boundingRect().center()).toPoint();
    const auto unpressedColor = tile->property("fillTopColor").value<QColor>();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, position);
    QVERIFY(tile->property("pointerPressActive").toBool());
    QVERIFY(tile->property("visualPressed").toBool());
    QVERIFY(tile->property("fillTopColor").value<QColor>() != unpressedColor);
    QVERIFY(!tile->property("pressAnimating").toBool());
    QCOMPARE(tile->property("pressProgress").toReal(), qreal(0));
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, position);
    QVERIFY(!tile->property("pointerPressActive").toBool());

    motion.setEnabled(true);
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, position);
    QVERIFY(tile->property("pressAnimating").toBool());
    motion.setEnabled(false);
    QVERIFY(!tile->property("pressAnimating").toBool());
    QCOMPARE(tile->property("pressProgress").toReal(), qreal(0));
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, position);
    motion.setEnabled(true);
    QVERIFY(tile->setProperty("dragging", true));
    QTest::qWait(20);
    motion.setEnabled(false);
    QCOMPARE(tile->opacity(), qreal(0.35));
    QVERIFY(tile->setProperty("dragging", false));
    QCOMPARE(tile->opacity(), qreal(1));
    motion.setEnabled(true);
    QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 0, 2));
    QCoreApplication::processEvents();
    motion.setEnabled(false);
    QTRY_COMPARE(item(QStringLiteral("tile-work-standard-1"))->y(), qreal(112));
    destroyWindow();
}

void PrototypeWindowTest::reducedMotionCompletesFolderNavigation() {
    const auto state = std::make_shared<CatalogTestState>();
    xdstarter::LaunchTarget folder;
    folder.type = QStringLiteral("folder");
    folder.name = QStringLiteral("工具");
    folder.shortcutPath = QStringLiteral("C:/Fixture/Tools");
    folder.id = xdstarter::applicationTargetId(folder.type, folder.shortcutPath);
    folder.iconKey = folder.id;
    auto child = catalogTarget(QStringLiteral("画图"), QStringLiteral("C:/Fixture/Tools/Paint.lnk"));
    child.parentFolderId = folder.id;
    state->applications.append(folder);
    state->applications.append(child);
    ManualMotionPreference motion(true);
    destroyWindow();
    createWindow({}, true, state, {}, &motion);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 3);
    auto* browser = qobject_cast<xdstarter::ApplicationBrowserModel*>(controller_->applications());
    QVERIFY(browser);
    auto* folderButton = item(QStringLiteral("applicationLaunch-") + folder.id);
    QVERIFY(folderButton);
    click(folderButton);
    QVERIFY(!browser->inFolder()); // Animated mode initially waits for the fade-out.
    motion.setEnabled(false);
    QVERIFY(browser->inFolder()); // An in-flight transition completes immediately.
    auto* back = item(QStringLiteral("folderBackButton"));
    QVERIFY(back);
    click(back);
    QVERIFY(!browser->inFolder()); // New navigation is immediate while reduced motion is active.
    motion.setEnabled(true);
    auto* section = item(QStringLiteral("sectionButton-A"));
    QVERIFY(section);
    click(section);
    QVERIFY(window_->property("alphabetOpen").toBool());
    auto* alphabet = item(QStringLiteral("alphabetSelector"));
    QVERIFY(alphabet);
    QTest::qWait(20);
    motion.setEnabled(false);
    QCOMPARE(alphabet->opacity(), qreal(1));
    QCOMPARE(alphabet->scale(), qreal(1));
    QVERIFY(window_->setProperty("alphabetOpen", false));
    QCOMPARE(alphabet->opacity(), qreal(0));
    motion.setEnabled(true);
    QVERIFY(window_->setProperty("selectionStatus", QStringLiteral("测试错误")));
    QVERIFY(window_->setProperty("selectionStatusError", true));
    auto* feedback = item(QStringLiteral("feedbackPanel"));
    QVERIFY(feedback);
    QTest::qWait(20);
    motion.setEnabled(false);
    QCOMPARE(feedback->opacity(), qreal(1));
    QVERIFY(window_->setProperty("selectionStatus", QString{}));
    QCOMPARE(feedback->opacity(), qreal(0));
    auto* dragPreview = item(QStringLiteral("dragPlacementPreview"));
    QVERIFY(dragPreview);
    motion.setEnabled(true);
    QVERIFY(window_->setProperty("previewX", 120.0));
    QTest::qWait(20);
    motion.setEnabled(false);
    QCOMPARE(dragPreview->x(), qreal(120));
    QVERIFY(window_->setProperty("previewX", 80.0));
    QCOMPARE(dragPreview->x(), qreal(80));
    motion.setEnabled(true);
    QVERIFY(QMetaObject::invokeMethod(window_, "openMoreMenu"));
    auto* menu = window_->findChild<QObject*>(QStringLiteral("moreMenu"));
    QVERIFY(menu && menu->property("visible").toBool());
    motion.setEnabled(false);
    // A popup already entering may complete its existing bounded transition.
    QTRY_VERIFY_WITH_TIMEOUT(menu->property("opened").toBool(), 1000);
    QCOMPARE(menu->property("opacity").toReal(), qreal(1));
    QCOMPARE(menu->property("scale").toReal(), qreal(1));
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!menu->property("visible").toBool(), 1000);
    QVERIFY(QMetaObject::invokeMethod(window_, "openMoreMenu"));
    QTRY_VERIFY_WITH_TIMEOUT(menu->property("opened").toBool(), 1000);
    QCOMPARE(menu->property("opacity").toReal(), qreal(1));
    QCOMPARE(menu->property("scale").toReal(), qreal(1));
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!menu->property("visible").toBool(), 1000);
    destroyWindow();
}

void PrototypeWindowTest::keyboardAndMouseSelection() {
    auto* themeButton = item(QStringLiteral("moreMenuButton"));
    auto* first = item(QStringLiteral("tile-work-standard-1"));
    auto* second = item(QStringLiteral("tile-work-standard-long"));
    auto* status = item(QStringLiteral("selectionStatus"));
    QVERIFY(status);
    const auto firstName = tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("name")).toString();
    const auto secondName = tileData(QStringLiteral("work-standard-long")).value(QStringLiteral("name")).toString();
    QVERIFY(!firstName.isEmpty());
    QVERIFY(!secondName.isEmpty());
    themeButton->forceActiveFocus(Qt::TabFocusReason);
    // Search and catalogue controls are in the normal Tab order before the tiles.
    for (int count = 0; count < 12 && !first->hasActiveFocus(); ++count) {
        QTest::keyClick(window_, Qt::Key_Tab);
    }
    QTRY_VERIFY(first->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(status->property("text").toString().contains(firstName));
    click(second);
    QTRY_VERIFY(status->property("text").toString().contains(secondName));
    QVERIFY(window_->isVisible());
}

void PrototypeWindowTest::tilePressPerspective_data() {
    QTest::addColumn<QString>("tileId");
    QTest::addColumn<QPointF>("fraction");
    const QList<QPair<QByteArray, QPointF>> positions{
        {"top-left", {0.15, 0.15}},    {"top", {0.5, 0.15}},    {"top-right", {0.85, 0.15}},
        {"left", {0.15, 0.5}},         {"center", {0.5, 0.5}},  {"right", {0.85, 0.5}},
        {"bottom-left", {0.15, 0.85}}, {"bottom", {0.5, 0.85}}, {"bottom-right", {0.85, 0.85}}};
    for (const auto& id : {QStringLiteral("daily-small-1"), QStringLiteral("work-standard-1"),
                           QStringLiteral("daily-wide")}) {
        for (const auto& position : positions) {
            const auto name = id.toLatin1() + '-' + position.first;
            QTest::newRow(name.constData()) << id << position.second;
        }
    }
}

void PrototypeWindowTest::tilePressPerspective() {
    QFETCH(QString, tileId);
    QFETCH(QPointF, fraction);
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    tile->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(viewportScene().contains(tile->mapRectToScene(tile->boundingRect())));
    QTRY_COMPARE(tile->property("appearScale").toReal(), 1.0);
    auto* background = visualItem(tile, QStringLiteral("tilePressBackground"));
    auto* content = visualItem(tile, QStringLiteral("tilePressContent"));
    QVERIFY(background && content);
    const auto inputGeometry = tile->mapRectToScene(tile->boundingRect());
    const auto snapshot = service_->snapshot();
    const auto captureName = QString::fromLatin1(QTest::currentDataTag());
    const QPointF localPoint(tile->width() * fraction.x(), tile->height() * fraction.y());
    const auto clickPosition = tile->mapToScene(localPoint).toPoint();
    QSignalSpy activated(tile, SIGNAL(selected(QString)));
    QSignalSpy progress(tile, SIGNAL(pressProgressChanged()));
    QVERIFY(activated.isValid() && progress.isValid());
    captureTilePressFrame(captureName, QStringLiteral("neutral"), tile);
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, clickPosition);
    QVERIFY(tile->property("pointerPressActive").toBool());
    QVERIFY(tile->property("pressAnimating").toBool());
    QVERIFY(tile->property("pressProgress").toReal() < 1.0);
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    QVERIFY(progress.count() >= 2);
    QCOMPARE(activated.count(), 0);
    QCOMPARE(tile->mapRectToScene(tile->boundingRect()), inputGeometry);
    QCOMPARE(tile->scale(), 1.0);
    QCOMPARE(service_->snapshot(), snapshot);

    const auto matrix = tile->property("pressMatrix").value<QMatrix4x4>();
    QVERIFY(!matrix.isIdentity());
    const auto corners = tileCorners(tile);
    QList<QPointF> projected;
    QList<qreal> depths;
    for (const auto& corner : corners) {
        const auto point = background->mapToItem(tile, corner);
        projected.append(point);
        const auto contentPoint = content->mapToItem(tile, corner);
        QVERIFY2(QLineF(contentPoint, point).length() < 0.001,
                 qPrintable(QStringLiteral("corner=(%1,%2) background=(%3,%4 %5x%6) "
                                           "content=(%7,%8 %9x%10) mapped=(%11,%12)/(%13,%14) "
                                           "padding=%15 horizontal=%16 left=%17 right=%18")
                                .arg(corner.x())
                                .arg(corner.y())
                                .arg(background->x())
                                .arg(background->y())
                                .arg(background->width())
                                .arg(background->height())
                                .arg(content->x())
                                .arg(content->y())
                                .arg(content->width())
                                .arg(content->height())
                                .arg(point.x())
                                .arg(point.y())
                                .arg(contentPoint.x())
                                .arg(contentPoint.y())
                                .arg(tile->property("padding").toReal())
                                .arg(tile->property("horizontalPadding").toReal())
                                .arg(tile->property("leftPadding").toReal())
                                .arg(tile->property("rightPadding").toReal())));
        const auto clip = matrix * QVector4D(corner.x(), corner.y(), 0, 1);
        QVERIFY(clip.w() >= 1.0);
        depths.append(clip.z());
        QVERIFY(QLineF(point, QPointF(clip.x() / clip.w(), clip.y() / clip.w())).length() < 0.001);
    }
    // A real projective quadrilateral is not a parallelogram. A centered press
    // intentionally keeps all depths equal and only depresses the plane.
    const auto diagonalDifference = projected[0] + projected[2] - projected[1] - projected[3];
    const bool horizontalTilt = fraction.x() != 0.5;
    const bool verticalTilt = fraction.y() != 0.5;
    if (horizontalTilt || verticalTilt) {
        QVERIFY(std::hypot(diagonalDifference.x(), diagonalDifference.y()) > 0.01);
        QVERIFY(std::abs(matrix(3, 0)) + std::abs(matrix(3, 1)) > 0.000001);
    } else {
        QVERIFY(std::hypot(diagonalDifference.x(), diagonalDifference.y()) < 0.001);
        QCOMPARE(matrix(3, 0), 0.0f);
        QCOMPARE(matrix(3, 1), 0.0f);
        QVERIFY(QLineF(background->mapToItem(tile, tile->boundingRect().center()),
                       tile->boundingRect().center())
                    .length() < 0.001);
    }
    // A diagonally tilted plane may have two equal-depth corners. Its pressed
    // corner must still recede farther than the opposite corner.
    if (horizontalTilt && verticalTilt) {
        const qsizetype pressed = fraction.y() < 0.5 ? (fraction.x() < 0.5 ? 0 : 1)
                                                     : (fraction.x() < 0.5 ? 3 : 2);
        QVERIFY(depths[pressed] < depths[(pressed + 2) % 4]);
    }
    const auto left = QLineF(projected[0], projected[3]).length();
    const auto right = QLineF(projected[1], projected[2]).length();
    const auto top = QLineF(projected[0], projected[1]).length();
    const auto bottom = QLineF(projected[3], projected[2]).length();
    if (fraction.x() < 0.5)
        QVERIFY(left < right);
    if (fraction.x() > 0.5)
        QVERIFY(right < left);
    if (fraction.y() < 0.5)
        QVERIFY(top < bottom);
    if (fraction.y() > 0.5)
        QVERIFY(bottom < top);
    captureTilePressFrame(captureName, QStringLiteral("pressed"), tile);

    progress.clear();
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, clickPosition);
    QVERIFY(!tile->property("pointerPressActive").toBool());
    QVERIFY(tile->property("pressAnimating").toBool());
    QVERIFY(tile->property("pressProgress").toReal() > 0.0);
    QCOMPARE(activated.count(), 1);
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    QVERIFY(progress.count() >= 2);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QCOMPARE(tile->mapRectToScene(tile->boundingRect()), inputGeometry);
    QCOMPARE(service_->snapshot(), snapshot);
    captureTilePressFrame(captureName, QStringLiteral("restored"), tile);
}

void PrototypeWindowTest::tilePressVisibleFeedback_data() {
    QTest::addColumn<QString>("tileId");
    QTest::addColumn<QPointF>("fraction");
    const QList<QPair<QByteArray, QPointF>> positions{
        {"right", {0.85, 0.5}}, {"bottom", {0.5, 0.85}}, {"bottom-right", {0.85, 0.85}}};
    for (const auto& id : {QStringLiteral("daily-small-1"), QStringLiteral("work-standard-1"),
                           QStringLiteral("daily-wide")}) {
        for (const auto& position : positions) {
            const auto name = id.toLatin1() + '-' + position.first;
            QTest::newRow(name.constData()) << id << position.second;
        }
    }
}

void PrototypeWindowTest::tilePressVisibleFeedback() {
    QFETCH(QString, tileId);
    QFETCH(QPointF, fraction);
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    tile->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(viewportScene().contains(tile->mapRectToScene(tile->boundingRect())));
    QTRY_COMPARE(tile->property("appearScale").toReal(), 1.0);
    auto* background = visualItem(tile, QStringLiteral("tilePressBackground"));
    auto* content = visualItem(tile, QStringLiteral("tilePressContent"));
    QVERIFY(background && content);
    const auto sceneBounds = tile->mapRectToScene(tile->boundingRect());
    const auto snapshot = service_->snapshot();
    const auto minimumDimension = std::min(tile->width(), tile->height());
    // Use the real MouseArea at a normal in-tile position, rather than only
    // checking a mathematical boundary or accepting any nonzero projection.
    const auto pressPosition = tile->mapToScene({tile->width() * fraction.x(),
                                                tile->height() * fraction.y()}).toPoint();
    QSignalSpy activated(tile, SIGNAL(selected(QString)));
    QSignalSpy progress(tile, SIGNAL(pressProgressChanged()));
    QVERIFY(activated.isValid() && progress.isValid());

    struct MotionFrame {
        QImage image;
        QJsonObject metadata;
    };
    QVector<MotionFrame> motionFrames;
    motionFrames.reserve(80);
    const auto motionDirectory = qEnvironmentVariable("XDSTARTER_TILE_PRESS_MOTION_DIR");
    QString captureError;
    QString phase = QStringLiteral("neutral");
    QElapsedTimer elapsed;
    elapsed.start();
    // All captures run on the GUI thread and only retain cropped images in
    // memory. The timer is destroyed before its captured storage goes away;
    // no render-thread signal captures stack state, and PNG IO happens later.
    bool capturing = false;
    QTimer sampler;
    sampler.setTimerType(Qt::PreciseTimer);
    sampler.setInterval(16);
    const auto capture = [&] {
        if (capturing)
            return;
        capturing = true;
        const auto startedMs = elapsed.elapsed();
        const auto matrix = tile->property("pressMatrix").value<QMatrix4x4>();
        QJsonArray matrixValues;
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                matrixValues.append(matrix(row, column));
        QJsonArray corners;
        for (const auto& corner : tileCorners(tile)) {
            const auto point = background->mapToItem(tile, corner);
            corners.append(QJsonObject{{QStringLiteral("x"), point.x()},
                                       {QStringLiteral("y"), point.y()}});
        }
        QJsonObject metadata{
            {QStringLiteral("phase"), phase},
            {QStringLiteral("elapsedMs"), startedMs},
            {QStringLiteral("pressProgress"), tile->property("pressProgress").toReal()},
            {QStringLiteral("pointerPressActive"), tile->property("pointerPressActive").toBool()},
            {QStringLiteral("matrixRowMajor"), matrixValues},
            {QStringLiteral("projectedCornersDip"), corners}};
        const auto frame = window_->grabWindow();
        metadata.insert(QStringLiteral("captureFinishedMs"), elapsed.elapsed());
        if (frame.isNull()) {
            captureError = QStringLiteral("Native grabWindow returned an empty motion frame");
            sampler.stop();
        } else {
            const qreal scaleX = qreal(frame.width()) / window_->width();
            const qreal scaleY = qreal(frame.height()) / window_->height();
            const QRect crop(qFloor((sceneBounds.x() - 2) * scaleX),
                             qFloor((sceneBounds.y() - 2) * scaleY),
                             qCeil((sceneBounds.width() + 4) * scaleX),
                             qCeil((sceneBounds.height() + 4) * scaleY));
            metadata.insert(QStringLiteral("cropPixels"), rectangleJson(crop));
            motionFrames.append({frame.copy(crop), metadata});
        }
        capturing = false;
    };
    if (!motionDirectory.isEmpty()) {
        connect(&sampler, &QTimer::timeout, &sampler, capture);
        capture();
        sampler.start();
        QTest::qWait(100);
    }

    phase = QStringLiteral("press");
    const auto pressInputMs = elapsed.elapsed();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, pressPosition);
    QVERIFY(tile->property("pointerPressActive").toBool());
    QVERIFY(tile->property("pressAnimating").toBool());
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    if (motionDirectory.isEmpty())
        QVERIFY(progress.count() >= 2);
    QCOMPARE(activated.count(), 0);
    QCOMPARE(tile->mapRectToScene(tile->boundingRect()), sceneBounds);
    QCOMPARE(service_->snapshot(), snapshot);

    QList<QPointF> projected;
    for (const auto& corner : tileCorners(tile)) {
        const auto point = background->mapToItem(tile, corner);
        projected.append(point);
        QVERIFY(QLineF(point, content->mapToItem(tile, corner)).length() < 0.001);
    }
    const auto cornerMovement = QLineF(projected[2], {tile->width(), tile->height()}).length();
    const auto verticalEdgeDifference = QLineF(projected[0], projected[3]).length() -
                                        QLineF(projected[1], projected[2]).length();
    const auto horizontalEdgeDifference = QLineF(projected[0], projected[1]).length() -
                                          QLineF(projected[3], projected[2]).length();
    const bool horizontalTilt = fraction.x() != 0.5;
    const bool verticalTilt = fraction.y() != 0.5;
    qreal pressedEdgeMovement = 0;
    if (horizontalTilt && !verticalTilt) {
        pressedEdgeMovement = tile->width() -
                              background->mapToItem(tile, {tile->width(), tile->height() / 2}).x();
    } else if (verticalTilt && !horizontalTilt) {
        pressedEdgeMovement = tile->height() -
                              background->mapToItem(tile, {tile->width() / 2, tile->height()}).y();
    }
    const auto detail = QStringLiteral("case=%1, corner movement=%2 DIP, edge movement=%3 DIP, "
                                       "edge differences=%4/%5 DIP")
                            .arg(QString::fromLatin1(QTest::currentDataTag()))
                            .arg(cornerMovement).arg(pressedEdgeMovement)
                            .arg(verticalEdgeDifference).arg(horizontalEdgeDifference);
    // A direction-correct subpixel change previously passed. Require a
    // meaningful corner/edge retreat and shorter pressed edges in all sizes.
    if (horizontalTilt && verticalTilt)
        QVERIFY2(cornerMovement >= minimumDimension * 0.04, qPrintable(detail));
    else
        QVERIFY2(pressedEdgeMovement >= minimumDimension * 0.025, qPrintable(detail));
    if (horizontalTilt)
        QVERIFY2(verticalEdgeDifference >= minimumDimension * 0.025, qPrintable(detail));
    if (verticalTilt)
        QVERIFY2(horizontalEdgeDifference >= minimumDimension * 0.025, qPrintable(detail));
    const auto matrix = tile->property("pressMatrix").value<QMatrix4x4>();
    const auto pressed = matrix * QVector4D(tile->width(), tile->height(), 0, 1);
    const auto opposite = matrix * QVector4D(0, 0, 0, 1);
    QVERIFY(pressed.z() < opposite.z());

    phase = QStringLiteral("hold");
    if (!motionDirectory.isEmpty()) {
        capture();
        QTest::qWait(250);
    }
    progress.clear();
    phase = QStringLiteral("release");
    const auto releaseInputMs = elapsed.elapsed();
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, pressPosition);
    QVERIFY(!tile->property("pointerPressActive").toBool());
    QVERIFY(tile->property("pressAnimating").toBool());
    QCOMPARE(activated.count(), 1);
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    if (motionDirectory.isEmpty())
        QVERIFY(progress.count() >= 2);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QCOMPARE(tile->mapRectToScene(tile->boundingRect()), sceneBounds);
    QCOMPARE(service_->snapshot(), snapshot);
    phase = QStringLiteral("restored");
    if (motionDirectory.isEmpty())
        return;

    QTest::qWait(100);
    sampler.stop();
    capture();
    QVERIFY2(captureError.isEmpty(), qPrintable(captureError));
    const auto destination = QDir(motionDirectory).filePath(
        QString::fromLatin1(QTest::currentDataTag()));
    QVERIFY(QDir().mkpath(destination));
    QJsonArray records;
    for (qsizetype index = 0; index < motionFrames.size(); ++index) {
        auto& frame = motionFrames[index];
        const auto name = QStringLiteral("frame-%1.png").arg(index, 4, 10, QLatin1Char('0'));
        QVERIFY(frame.image.save(QDir(destination).filePath(name)));
        frame.metadata.insert(QStringLiteral("file"), name);
        records.append(frame.metadata);
    }
    const auto pressTokens = visualTokens_.value(QStringLiteral("motion")).toMap()
                                 .value(QStringLiteral("tilePress")).toMap();
    const QJsonObject record{
        {QStringLiteral("tileId"), tileId},
        {QStringLiteral("fraction"), QJsonObject{{QStringLiteral("x"), fraction.x()},
                                                {QStringLiteral("y"), fraction.y()}}},
        {QStringLiteral("qpa"), QGuiApplication::platformName()},
        {QStringLiteral("graphicsApi"), graphicsApiName(window_->rendererInterface()->graphicsApi())},
        {QStringLiteral("dpr"), window_->devicePixelRatio()},
        {QStringLiteral("inputSceneGeometryDip"), rectangleJson(sceneBounds)},
        {QStringLiteral("tokens"), QJsonObject::fromVariantMap(pressTokens)},
        {QStringLiteral("pressInputMs"), pressInputMs},
        {QStringLiteral("releaseInputMs"), releaseInputMs},
        {QStringLiteral("cornerMovementDip"), cornerMovement},
        {QStringLiteral("pressedEdgeMovementDip"), pressedEdgeMovement},
        {QStringLiteral("verticalEdgeDifferenceDip"), verticalEdgeDifference},
        {QStringLiteral("horizontalEdgeDifferenceDip"), horizontalEdgeDifference},
        {QStringLiteral("captureNote"),
         QStringLiteral("Actual GUI-thread grabWindow frames; capture can perturb animation timing. "
                        "No frame-count or unperturbed timing claim is made.")},
        {QStringLiteral("frames"), records}};
    QFile output(QDir(destination).filePath(QStringLiteral("motion.json")));
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(output.write(QJsonDocument(record).toJson()) > 0);
}

void PrototypeWindowTest::tilePressBoundaryGeometry_data() {
    QTest::addColumn<QString>("tileId");
    QTest::addColumn<QPointF>("fraction");
    const QList<QPair<QByteArray, QPointF>> positions{
        {"top-left", {0, 0}},    {"top", {0.5, 0}},    {"top-right", {1, 0}},
        {"left", {0, 0.5}},      {"center", {0.5, 0.5}}, {"right", {1, 0.5}},
        {"bottom-left", {0, 1}}, {"bottom", {0.5, 1}}, {"bottom-right", {1, 1}}};
    for (const auto& id : {QStringLiteral("daily-small-1"), QStringLiteral("work-standard-1"),
                           QStringLiteral("daily-wide")}) {
        for (const auto& position : positions) {
            const auto name = id.toLatin1() + '-' + position.first;
            QTest::newRow(name.constData()) << id << position.second;
        }
    }
}

void PrototypeWindowTest::tilePressBoundaryGeometry() {
    QFETCH(QString, tileId);
    QFETCH(QPointF, fraction);
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    QTRY_COMPARE(tile->property("appearScale").toReal(), 1.0);
    auto* background = visualItem(tile, QStringLiteral("tilePressBackground"));
    auto* content = visualItem(tile, QStringLiteral("tilePressContent"));
    QVERIFY(background && content);
    const auto geometry = tile->mapRectToScene(tile->boundingRect());
    const auto snapshot = service_->snapshot();
    QVERIFY(QMetaObject::invokeMethod(tile, "beginPointerPress",
                                     Q_ARG(double, fraction.x() * tile->width()),
                                     Q_ARG(double, fraction.y() * tile->height())));
    QTRY_VERIFY(!tile->property("pressAnimating").toBool());
    QCOMPARE(tile->property("pressProgress").toReal(), 1.0);
    const auto matrix = tile->property("pressMatrix").value<QMatrix4x4>();
    const auto pressTokens = visualTokens_.value(QStringLiteral("motion")).toMap()
                                 .value(QStringLiteral("tilePress")).toMap();
    const auto maximumTilt = pressTokens.value(QStringLiteral("maximumTiltDegrees")).toReal();
    const auto maximumDepth = pressTokens.value(QStringLiteral("maximumEdgeDepthDip")).toReal();
    const auto angle = tilePressAngleDegrees(matrix, tile);
    QVERIFY2(angle <= maximumTilt + 0.001,
             qPrintable(QStringLiteral("Combined tilt=%1 degrees; cap=%2 degrees")
                            .arg(angle).arg(maximumTilt)));
    const bool centered = fraction == QPointF(0.5, 0.5);
    if (centered)
        QVERIFY(angle < 0.001);
    else
        QVERIFY(angle > 0.1);
    int anchors = 0;
    qreal deepestRetreat = 0;
    for (const auto& corner : tileCorners(tile)) {
        const auto clip = matrix * QVector4D(corner.x(), corner.y(), 0, 1);
        const auto projected = background->mapToItem(tile, corner);
        const qreal depth = -clip.z();
        deepestRetreat = std::max(deepestRetreat, depth);
        QVERIFY2(depth >= -0.001 && depth <= maximumDepth + 0.001,
                 qPrintable(QStringLiteral("corner=(%1,%2), retreat=%3 DIP")
                                .arg(corner.x()).arg(corner.y()).arg(depth)));
        QVERIFY(clip.w() >= 1.0 - 0.000001);
        QVERIFY(QLineF(projected, content->mapToItem(tile, corner)).length() < 0.001);
        QVERIFY(QLineF(projected, {clip.x() / clip.w(), clip.y() / clip.w()}).length() < 0.001);
        // A boundary press rotates about its opposite edge/corner. Neither a
        // center-scale nor a uniform depression may displace those anchors.
        const bool oppositeX = fraction.x() == 0.5 ||
                               corner.x() == (fraction.x() < 0.5 ? tile->width() : 0);
        const bool oppositeY = fraction.y() == 0.5 ||
                               corner.y() == (fraction.y() < 0.5 ? tile->height() : 0);
        if (!centered && oppositeX && oppositeY) {
            ++anchors;
            QVERIFY(std::abs(depth) < 0.001);
            QVERIFY(QLineF(projected, corner).length() < 0.001);
        }
        if (centered)
            QVERIFY(std::abs(depth - 2) < 0.001);
    }
    QVERIFY(deepestRetreat > 0);
    QCOMPARE(anchors, centered ? 0 : (fraction.x() == 0.5 || fraction.y() == 0.5 ? 2 : 1));
    QCOMPARE(tile->mapRectToScene(tile->boundingRect()), geometry);
    QCOMPARE(service_->snapshot(), snapshot);
    QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, true)));
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
}

void PrototypeWindowTest::tilePressSymmetry() {
    for (const auto& id : {QStringLiteral("daily-small-1"), QStringLiteral("work-standard-1"),
                           QStringLiteral("daily-wide")}) {
        auto* tile = item(QStringLiteral("tile-") + id);
        QVERIFY(tile);
        QTRY_COMPARE(tile->property("appearScale").toReal(), 1.0);
        auto* background = visualItem(tile, QStringLiteral("tilePressBackground"));
        QVERIFY(background);
        const auto press = [&](const QPointF& fraction) {
            QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, true));
            return QMetaObject::invokeMethod(tile, "beginPointerPress",
                                             Q_ARG(double, fraction.x() * tile->width()),
                                             Q_ARG(double, fraction.y() * tile->height()));
        };
        for (const QPointF fraction : {QPointF(0, 0), QPointF(0.15, 0.15), QPointF(0.15, 0.3)}) {
            QVERIFY(press(fraction));
            QTRY_VERIFY(!tile->property("pressAnimating").toBool());
            const auto reference = tile->property("pressMatrix").value<QMatrix4x4>();
            QVERIFY(press({1 - fraction.x(), fraction.y()}));
            QTRY_VERIFY(!tile->property("pressAnimating").toBool());
            const auto mirroredX = tile->property("pressMatrix").value<QMatrix4x4>();
            for (const auto& corner : tileCorners(tile)) {
                const auto expected = reference * QVector4D(corner.x(), corner.y(), 0, 1);
                const auto actual = mirroredX * QVector4D(tile->width() - corner.x(), corner.y(), 0, 1);
                QVERIFY(std::abs(expected.z() - actual.z()) < 0.001);
                QVERIFY(std::abs(tile->width() - expected.x() / expected.w() - actual.x() / actual.w()) < 0.001);
                QVERIFY(std::abs(expected.y() / expected.w() - actual.y() / actual.w()) < 0.001);
            }
            QVERIFY(press({fraction.x(), 1 - fraction.y()}));
            QTRY_VERIFY(!tile->property("pressAnimating").toBool());
            const auto mirroredY = tile->property("pressMatrix").value<QMatrix4x4>();
            for (const auto& corner : tileCorners(tile)) {
                const auto expected = reference * QVector4D(corner.x(), corner.y(), 0, 1);
                const auto actual = mirroredY * QVector4D(corner.x(), tile->height() - corner.y(), 0, 1);
                QVERIFY(std::abs(expected.z() - actual.z()) < 0.001);
                QVERIFY(std::abs(expected.x() / expected.w() - actual.x() / actual.w()) < 0.001);
                QVERIFY(std::abs(tile->height() - expected.y() / expected.w() - actual.y() / actual.w()) < 0.001);
            }
            if (tile->width() == tile->height()) {
                QVERIFY(press({fraction.y(), fraction.x()}));
                QTRY_VERIFY(!tile->property("pressAnimating").toBool());
                const auto transposed = tile->property("pressMatrix").value<QMatrix4x4>();
                for (const auto& corner : tileCorners(tile)) {
                    const auto expected = reference * QVector4D(corner.x(), corner.y(), 0, 1);
                    const auto actual = transposed * QVector4D(corner.y(), corner.x(), 0, 1);
                    QVERIFY(std::abs(expected.z() - actual.z()) < 0.001);
                    QVERIFY(std::abs(expected.x() / expected.w() - actual.y() / actual.w()) < 0.001);
                    QVERIFY(std::abs(expected.y() / expected.w() - actual.x() / actual.w()) < 0.001);
                }
                if (fraction.x() == fraction.y()) {
                    const auto topRight = reference * QVector4D(tile->width(), 0, 0, 1);
                    const auto bottomLeft = reference * QVector4D(0, tile->height(), 0, 1);
                    QVERIFY(std::abs(topRight.z() - bottomLeft.z()) < 0.001);
                }
            }
        }
        QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, true)));
    }
}

void PrototypeWindowTest::tilePressInterruptedMotion() {
    auto* tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(tile);
    struct RestoreTokens {
        QQuickItem* tile;
        QVariant original;
        ~RestoreTokens() { tile->setProperty("tokens", original); }
    } restoreTokens{tile, tile->property("tokens")};
    // Give this test instance enough intermediate frames on busy CI runners.
    // JSON round-tripping owns every nested value; production tokens and the
    // other pointer tests retain their actual 90/180 ms timings.
    auto testTokens = QJsonDocument::fromVariant(visualTokens_).object().toVariantMap();
    auto motionTokens = testTokens.value(QStringLiteral("motion")).toMap();
    auto pressTokens = motionTokens.value(QStringLiteral("tilePress")).toMap();
    pressTokens.insert(QStringLiteral("pressMs"), 400);
    pressTokens.insert(QStringLiteral("releaseMs"), 600);
    motionTokens.insert(QStringLiteral("tilePress"), pressTokens);
    testTokens.insert(QStringLiteral("motion"), motionTokens);
    QVERIFY(tile->setProperty("tokens", testTokens));
    auto* background = visualItem(tile, QStringLiteral("tilePressBackground"));
    QVERIFY(background);
    QTRY_COMPARE(tile->property("appearScale").toReal(), 1.0);
    const auto snapshot = service_->snapshot();
    QSignalSpy activated(tile, SIGNAL(selected(QString)));
    QSignalSpy progress(tile, SIGNAL(pressProgressChanged()));
    QVERIFY(activated.isValid() && progress.isValid());
    const auto press = [&](const QPointF& fraction) {
        return QMetaObject::invokeMethod(tile, "beginPointerPress",
                                         Q_ARG(double, fraction.x() * tile->width()),
                                         Q_ARG(double, fraction.y() * tile->height()));
    };
    const auto projectedCorners = [&] {
        QList<QPointF> points;
        for (const auto& corner : tileCorners(tile))
            points.append(background->mapToItem(tile, corner));
        return points;
    };
    // Releasing before the press animation finishes must recover from the
    // current pose rather than completing the old press first.
    QVERIFY(press({0, 0}));
    QVERIFY(progress.wait(800));
    QVERIFY(tile->property("pressProgress").toReal() > 0);
    QVERIFY(tile->property("pressProgress").toReal() < 1);
    const auto earlyProgress = tile->property("pressProgress").toReal();
    const auto earlyPose = projectedCorners();
    QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, false)));
    QCOMPARE(tile->property("pressProgress").toReal(), earlyProgress);
    QCOMPARE(projectedCorners(), earlyPose);
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());

    QVERIFY(press({0, 0}));
    QTRY_VERIFY(!tile->property("pressAnimating").toBool());
    progress.clear();
    QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, false)));
    QVERIFY(progress.wait(800));
    QVERIFY(tile->property("pressProgress").toReal() < 1);
    QVERIFY(tile->property("pressProgress").toReal() > 0);
    const auto recoveringProgress = tile->property("pressProgress").toReal();
    const auto recoveringPose = projectedCorners();
    QVERIFY(press({1, 1}));
    QCOMPARE(tile->property("pressProgress").toReal(), recoveringProgress);
    // Recording the new pointer location cannot immediately flip the plane.
    const auto repressPose = projectedCorners();
    for (qsizetype index = 0; index < recoveringPose.size(); ++index)
        QVERIFY(QLineF(recoveringPose[index], repressPose[index]).length() < 0.001);
    QTRY_VERIFY(!tile->property("pressAnimating").toBool());
    QCOMPARE(tile->property("pressProgress").toReal(), 1.0);
    const auto resumedTarget = tile->property("pressMatrix").value<QMatrix4x4>();
    QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, true)));
    QVERIFY(press({1, 1}));
    QTRY_VERIFY(!tile->property("pressAnimating").toBool());
    const auto freshTarget = tile->property("pressMatrix").value<QMatrix4x4>();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            QVERIFY(std::abs(resumedTarget(row, column) - freshTarget(row, column)) < 0.00001);

    // Hiding during direction interpolation must cancel every motion channel;
    // showing again starts at the identity, including on a subsequent press.
    QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, false)));
    QTRY_VERIFY(tile->property("pressProgress").toReal() < 1);
    QVERIFY(press({0, 0}));
    QVERIFY(tile->property("pressAnimating").toBool());
    tile->setVisible(false);
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QCOMPARE(tile->property("pressDirectionX").toReal(), 0.0);
    QCOMPARE(tile->property("pressDirectionY").toReal(), 0.0);
    QVERIFY(!tile->property("pressAnimating").toBool());
    QVERIFY(!tile->property("pointerPressActive").toBool());
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    tile->setVisible(true);
    QTest::qWait(450);
    QCOMPARE(tile->property("pressDirectionX").toReal(), 0.0);
    QCOMPARE(tile->property("pressDirectionY").toReal(), 0.0);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QVERIFY(press({1, 1}));
    QTRY_VERIFY(!tile->property("pressAnimating").toBool());
    const auto afterHidden = tile->property("pressMatrix").value<QMatrix4x4>();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            QVERIFY(std::abs(afterHidden(row, column) - freshTarget(row, column)) < 0.00001);
    QVERIFY(QMetaObject::invokeMethod(tile, "cancelPointerPress", Q_ARG(bool, true)));
    QCOMPARE(activated.count(), 0);
    QCOMPARE(service_->snapshot(), snapshot);
}

void PrototypeWindowTest::tilePressSymbolsRemainRendered() {
    const auto whitePixels = [this](const QImage& image, QQuickItem* symbol) {
        const auto logical = symbol->mapRectToScene(symbol->boundingRect());
        const qreal sx = qreal(image.width()) / window_->width();
        const qreal sy = qreal(image.height()) / window_->height();
        const QRect pixels(qFloor(logical.left() * sx), qFloor(logical.top() * sy),
                           qCeil(logical.width() * sx), qCeil(logical.height() * sy));
        int count = 0;
        const auto region = pixels.intersected(image.rect());
        for (int y = region.top(); y <= region.bottom(); ++y)
            for (int x = region.left(); x <= region.right(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.red() > 230 && color.green() > 230 && color.blue() > 230)
                    ++count;
            }
        return count;
    };
    const auto before = service_->snapshot();
    for (const auto* id : {"daily-small-1", "work-standard-1", "daily-wide"}) {
        auto* tile = item(QStringLiteral("tile-") + QLatin1String(id));
        QVERIFY(tile);
        tile->forceActiveFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewportScene().contains(tile->mapRectToScene(tile->boundingRect())));
        auto* symbol = visualItem(tile, QStringLiteral("tileSymbol"));
        QVERIFY(symbol && symbol->isVisible());
        QTest::qWait(50);
        const auto neutral = window_->grabWindow();
        QVERIFY(!neutral.isNull());
        QVERIFY2(whitePixels(neutral, symbol) > 10, id);
        const auto position = tile->mapToScene(QPointF(12, 12)).toPoint();
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
        QTest::qWait(50);
        const auto pressed = window_->grabWindow();
        QVERIFY(!pressed.isNull());
        QVERIFY2(
            whitePixels(pressed, symbol) > 10,
            qPrintable(QStringLiteral("%1 symbol disappeared under %2 projection")
                           .arg(QLatin1String(id),
                                graphicsApiName(window_->rendererInterface()->graphicsApi()))));
        captureTilePressFrame(QLatin1String(id) + QStringLiteral("-symbol"),
                              QStringLiteral("pressed"), tile);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    }
    QCOMPARE(service_->snapshot(), before);
}

void PrototypeWindowTest::tilePressCancellation() {
    auto* tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(tile);
    const auto before = service_->snapshot();
    QSignalSpy activated(tile, SIGNAL(selected(QString)));
    QVERIFY(activated.isValid());
    const auto beginPress = [&] {
        tile->forceActiveFocus(Qt::OtherFocusReason);
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier,
                          tile->mapToScene(QPointF(18, 18)).toPoint());
    };
    beginPress();
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    // An outside release without an intervening move must not launch a tile or
    // depend on drag.threshold having already suppressed its click.
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    QCOMPARE(activated.count(), 0);

    beginPress();
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    auto* grabber = window_->mouseGrabberItem();
    QVERIFY(grabber);
    QCOMPARE(grabber->objectName(), QStringLiteral("tileEditPointer"));
    grabber->ungrabMouse();
    QTRY_VERIFY(!tile->property("pointerPressActive").toBool());
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        tile->mapToScene(QPointF(18, 18)).toPoint());
    QCOMPARE(activated.count(), 0);

    beginPress();
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    tile->setEnabled(false);
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        tile->mapToScene(QPointF(18, 18)).toPoint());
    QCOMPARE(activated.count(), 0);
    tile->setEnabled(true);

    beginPress();
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    QSignalSpy visibility(tile, SIGNAL(visibleChanged()));
    QVERIFY(visibility.isValid());
    tile->setVisible(false);
    if (tile->property("pressProgress").toReal() != 0.0) {
        qInfo().noquote() << QStringLiteral("Hidden tile: visible=%1 windowVisible=%2 "
                                            "visibleSignals=%3 active=%4 progress=%5 animation=%6 "
                                            "grabber=%7")
                                 .arg(tile->isVisible())
                                 .arg(window_->isVisible())
                                 .arg(visibility.count())
                                 .arg(tile->property("pointerPressActive").toBool())
                                 .arg(tile->property("pressProgress").toReal())
                                 .arg(tile->property("pressAnimating").toBool())
                                 .arg(window_->mouseGrabberItem()
                                          ? window_->mouseGrabberItem()->objectName()
                                          : QStringLiteral("none"));
    }
    QVERIFY(!tile->isVisible());
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QVERIFY(!tile->property("pressAnimating").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    tile->setVisible(true);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QCOMPARE(activated.count(), 0);

    beginPress();
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    QSignalSpy windowVisibility(window_, SIGNAL(visibleChanged(bool)));
    QVERIFY(windowVisibility.isValid());
    window_->hide();
    if (tile->property("pressProgress").toReal() != 0.0) {
        qInfo().noquote() << QStringLiteral("Hidden window: tileVisible=%1 windowVisible=%2 "
                                            "windowSignals=%3 componentReady=%4 active=%5 "
                                            "progress=%6 animation=%7")
                                 .arg(tile->isVisible())
                                 .arg(window_->isVisible())
                                 .arg(windowVisibility.count())
                                 .arg(tile->property("pressComponentReady").toBool())
                                 .arg(tile->property("pointerPressActive").toBool())
                                 .arg(tile->property("pressProgress").toReal())
                                 .arg(tile->property("pressAnimating").toBool());
    }
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QVERIFY(!tile->property("pressAnimating").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QCOMPARE(activated.count(), 0);
    QCOMPARE(service_->snapshot(), before);
}

void PrototypeWindowTest::tilePressDragAndAlternateInput() {
    const auto id = QStringLiteral("work-standard-1");
    auto* tile = item(QStringLiteral("tile-") + id);
    QVERIFY(tile);
    const auto before = service_->snapshot();
    const auto inputGeometry = tile->mapRectToScene(tile->boundingRect());
    const QPointF localPress(19, 23);
    const auto position = tile->mapToScene(localPress).toPoint();
    QSignalSpy activated(tile, SIGNAL(selected(QString)));
    QSignalSpy context(tile, SIGNAL(contextRequested(QString, QString, double, double)));
    QVERIFY(activated.isValid() && context.isValid());
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, position);
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
    QTest::mouseMove(window_, position + QPoint(24, 0));
    QTRY_VERIFY(window_->property("dragActive").toBool());
    QCOMPARE(window_->property("dragOffsetX").toReal(), localPress.x());
    QCOMPARE(window_->property("dragOffsetY").toReal(), localPress.y());
    QVERIFY(!tile->property("pointerPressActive").toBool());
    QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QCOMPARE(tile->mapRectToScene(tile->boundingRect()), inputGeometry);
    QCOMPARE(service_->snapshot(), before);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, position + QPoint(24, 0));
    QCOMPARE(activated.count(), 0);
    QCOMPARE(service_->snapshot(), before);

    QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier, position);
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    QCOMPARE(context.count(), 1);
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QVERIFY(tile->property("pressMatrix").value<QMatrix4x4>().isIdentity());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyPress(window_, Qt::Key_Space);
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QTest::keyRelease(window_, Qt::Key_Space);
    QTRY_COMPARE(activated.count(), 1);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(activated.count(), 2);
    QTest::keyClick(window_, Qt::Key_Menu);
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    QCOMPARE(tile->property("pressProgress").toReal(), 0.0);
    QCOMPARE(activated.count(), 2);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
    // A non-editing or read-only tile still has pointer feedback and activates,
    // while movements beyond the editable drag threshold cannot change layout.
    for (const bool readOnly : {false, true}) {
        QVERIFY(tile->setProperty("editMode", readOnly));
        QVERIFY(tile->setProperty("readOnly", readOnly));
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(tile->property("pressProgress").toReal(), 1.0);
        QTest::mouseMove(window_, position + QPoint(24, 0));
        QVERIFY(!window_->property("dragActive").toBool());
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, position + QPoint(24, 0));
        QTRY_COMPARE(tile->property("pressProgress").toReal(), 0.0);
    }
    QCOMPARE(activated.count(), 4);
    QCOMPARE(service_->snapshot(), before);
}

void PrototypeWindowTest::captureTilePressFrame(const QString& name, const QString& phase,
                                                QQuickItem* tile) {
    const auto directory = qEnvironmentVariable("XDSTARTER_TILE_PRESS_CAPTURE_DIR");
    if (directory.isEmpty())
        return;
    QVERIFY(QDir().mkpath(directory));
    std::atomic_int frames{0};
    const auto connection = connect(
        window_, &QQuickWindow::afterRendering, window_, [&frames] { ++frames; },
        Qt::DirectConnection);
    QElapsedTimer elapsed;
    elapsed.start();
    while (frames.load() < 2 && elapsed.elapsed() < 3000) {
        window_->requestUpdate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    disconnect(connection);
    QVERIFY2(frames.load() >= 2, "No fresh rendered frames for tile press capture");
    const auto frame = window_->grabWindow();
    QVERIFY(!frame.isNull());
    const auto prefix = QDir(directory).filePath(name + '-' + phase);
    QVERIFY(frame.save(prefix + QStringLiteral("-window.png")));
    const auto sceneBounds = tile->mapRectToScene(tile->boundingRect());
    const auto scaleX = static_cast<qreal>(frame.width()) / window_->width();
    const auto scaleY = static_cast<qreal>(frame.height()) / window_->height();
    const QRect crop(qFloor(sceneBounds.x() * scaleX) - 2, qFloor(sceneBounds.y() * scaleY) - 2,
                     qCeil(sceneBounds.width() * scaleX) + 4,
                     qCeil(sceneBounds.height() * scaleY) + 4);
    QVERIFY(frame.copy(crop).save(prefix + QStringLiteral("-tile.png")));
    auto* background = visualItem(tile, QStringLiteral("tilePressBackground"));
    QVERIFY(background);
    const auto matrix = tile->property("pressMatrix").value<QMatrix4x4>();
    QJsonArray corners;
    for (const auto& point : {QPointF(0, 0), QPointF(tile->width(), 0),
                              QPointF(tile->width(), tile->height()), QPointF(0, tile->height())}) {
        const auto projected = background->mapToItem(tile, point);
        const auto clip = matrix * QVector4D(point.x(), point.y(), 0, 1);
        corners.append(QJsonObject{{QStringLiteral("x"), projected.x()},
                                   {QStringLiteral("y"), projected.y()},
                                   {QStringLiteral("depth"), clip.z()},
                                   {QStringLiteral("homogeneousW"), clip.w()}});
    }
    QJsonArray values;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            values.append(matrix(row, column));
    const QJsonObject record{
        {QStringLiteral("phase"), phase},
        {QStringLiteral("tileId"),
         tile->property("tile").toMap().value(QStringLiteral("id")).toString()},
        {QStringLiteral("qpa"), QGuiApplication::platformName()},
        {QStringLiteral("graphicsApi"),
         graphicsApiName(window_->rendererInterface()->graphicsApi())},
        {QStringLiteral("freshFrames"), frames.load()},
        {QStringLiteral("dpr"), window_->devicePixelRatio()},
        {QStringLiteral("inputSceneGeometryDip"), rectangleJson(sceneBounds)},
        {QStringLiteral("projectedCornersDip"), corners},
        {QStringLiteral("matrixRowMajor"), values}};
    QFile output(prefix + QStringLiteral(".json"));
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(output.write(QJsonDocument(record).toJson()) > 0);
}

void PrototypeWindowTest::smallTilesExposeNames() {
    auto* small = item(QStringLiteral("tile-daily-small-1"));
    QVERIFY(small);
    auto* label = visualItem(small, QStringLiteral("tileLabel"));
    auto* symbol = visualItem(small, QStringLiteral("tileSymbol"));
    auto* tooltip = small->findChild<QObject*>(QStringLiteral("tileToolTip"));
    QVERIFY(label);
    QVERIFY(symbol);
    QVERIFY(tooltip);
    QVERIFY(!label->isVisible());
    QVERIFY(symbol->isVisible());
    QVERIFY(!symbol->property("text").toString().isEmpty());
    const auto name = tileData(QStringLiteral("daily-small-1")).value(QStringLiteral("name")).toString();
    QVERIFY(!name.isEmpty());
    auto* accessible = QAccessible::queryAccessibleInterface(small);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), name);
    QVERIFY(tooltip->property("text").toString().contains(name));
    QTest::mouseMove(window_, QPoint(1, 1));
    item(QStringLiteral("moreMenuButton"))->forceActiveFocus(Qt::OtherFocusReason);
    for (int count = 0; count < 64 && !small->hasActiveFocus(); ++count)
        QTest::keyClick(window_, Qt::Key_Tab);
    QTRY_VERIFY(small->hasActiveFocus());
    QVERIFY(controller_->keyboardNavigation());
    QTRY_VERIFY_WITH_TIMEOUT(tooltip->property("visible").toBool(), 1500);
    QVERIFY(small->property("visualFocus").toBool());
    QTest::keyClick(window_, Qt::Key_Tab);
    QTRY_VERIFY(!small->hasActiveFocus());
    QTRY_VERIFY(!tooltip->property("visible").toBool());
}

void PrototypeWindowTest::longNamesRemainBounded() {
    for (const auto* id : {"work-standard-long", "work-wide-long"}) {
        auto* tile = item(QStringLiteral("tile-") + QLatin1String(id));
        QVERIFY(tile);
        auto* label = visualItem(tile, QStringLiteral("tileLabel"));
        QVERIFY(label);
        QVERIFY(label->isVisible());
        QTRY_VERIFY(label->property("truncated").toBool());
        QVERIFY(label->width() < tile->width());
        const auto tileRectangle = itemGeometry(tile);
        const auto mappedLabel = label->mapRectToItem(tile, label->boundingRect());
        QVERIFY(tile->boundingRect().contains(mappedLabel));
        auto* accessible = QAccessible::queryAccessibleInterface(tile);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), label->property("text").toString());
        window_->setProperty("darkTheme", false);
        QCOMPARE(itemGeometry(tile), tileRectangle);
    }
}

void PrototypeWindowTest::resizePreservesGeometryAndRevealsFocus() {
    const auto geometry = tileGeometries();
    auto* flickable = item(QStringLiteral("groupsFlickable"));
    auto* last = item(QStringLiteral("tile-entertainment-standard-3"));
    QVERIFY(flickable);
    QVERIFY(last);
    const int shadow = qRound(window_->property("shadowInset").toReal());
    window_->resize(480 + 2 * shadow, 480 + 2 * shadow);
    QTRY_COMPARE(window_->size(), QSize(480 + 2 * shadow, 480 + 2 * shadow));
    QTRY_VERIFY(controller_->gridColumns() < 10);
    last = item(QStringLiteral("tile-entertainment-standard-3"));
    QVERIFY(last);
    last->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(last->hasActiveFocus());
    QTRY_COMPARE(flickable->property("contentX").toReal(), 0.0);
    QTRY_VERIFY(flickable->property("contentY").toReal() > 0);
    QTRY_VERIFY(flickable->boundingRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(
        last->mapRectToItem(flickable, last->boundingRect())));
    const auto resized = tileGeometries();
    QCOMPARE(resized.keys(), geometry.keys());
    for (auto tile = geometry.cbegin(); tile != geometry.cend(); ++tile)
        QCOMPARE(resized.value(tile.key()).size(), tile.value().size());
}

void PrototypeWindowTest::responsiveColumnsAndPersistence() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("responsive.json"));
    destroyWindow();
    createWindow(path, true);
    const auto targets = service_->snapshot().value(QStringLiteral("targets"));
    const int shadow = qRound(window_->property("shadowInset").toReal());
    window_->resize(1280 + 2 * shadow, 680 + 2 * shadow);
    QTRY_VERIFY(controller_->gridColumns() > 10);
    const int wideColumns = controller_->gridColumns();
    QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"),
                                  wideColumns - 2, 4));
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("column")).toInt(),
             wideColumns - 2);
    window_->resize(480 + 2 * shadow, 480 + 2 * shadow);
    QTRY_VERIFY(controller_->gridColumns() < 10);
    QVERIFY(xdstarter::LayoutStore::validate(service_->snapshot()).success);
    QCOMPARE(service_->snapshot().value(QStringLiteral("targets")), targets);
    for (const auto& group : service_->previewGroups())
        for (const auto& tile : group.tiles)
            QVERIFY(tile.rectangle.x + tile.rectangle.width <= group.width);
    QVERIFY(controller_->flushSave());
    const xdstarter::PrototypeService restarted(grid_, path, true);
    auto expected = service_->snapshot();
    auto preferences = expected.value(QStringLiteral("preferences")).toObject();
    preferences.insert(QStringLiteral("theme"),
                       controller_->darkTheme() ? QStringLiteral("dark") : QStringLiteral("light"));
    expected.insert(QStringLiteral("preferences"), preferences);
    QCOMPARE(restarted.snapshot(), expected);
    QCOMPARE(restarted.gridColumns(), controller_->gridColumns());
}

void PrototypeWindowTest::directionalNavigation() {
    auto* first = item(QStringLiteral("tile-work-standard-1"));
    auto* second = item(QStringLiteral("tile-work-standard-long"));
    auto* nextGroup = item(QStringLiteral("tile-daily-wide"));
    first->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Right);
    QTRY_VERIFY(second->hasActiveFocus());
    first->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Down);
    QTRY_VERIFY(nextGroup->hasActiveFocus());
    auto* smallTop = item(QStringLiteral("tile-daily-small-1"));
    auto* smallBottom = item(QStringLiteral("tile-daily-small-3"));
    smallTop->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Down);
    QTRY_VERIFY(smallBottom->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Up);
    QTRY_VERIFY(smallTop->hasActiveFocus());
}

void PrototypeWindowTest::escapeClosesWindow() {
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->isVisible());
}

QRectF PrototypeWindowTest::viewportScene() const {
    const auto* flickable = item(QStringLiteral("groupsFlickable"));
    return flickable ? flickable->mapRectToScene(flickable->boundingRect()) : QRectF{};
}

QPoint PrototypeWindowTest::gridPosition(const QString& groupId, const QPointF& position) const {
    const auto* grid = item(QStringLiteral("grid-") + groupId);
    return grid ? grid->mapToScene(position).toPoint() : QPoint{};
}

void PrototypeWindowTest::beginMouseDrag(const QString& tileId) {
    settleMenus();
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    const auto target = tileData(tileId);
    QTRY_COMPARE(tile->x(), target.value(QStringLiteral("x")).toReal());
    QTRY_COMPARE(tile->y(), target.value(QStringLiteral("y")).toReal());
    QTRY_COMPARE(tile->width(), target.value(QStringLiteral("width")).toReal());
    QTRY_COMPARE(tile->height(), target.value(QStringLiteral("height")).toReal());
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(viewportScene().adjusted(-1, -1, 1, 1).contains(
        tile->mapRectToScene(tile->boundingRect())));
    const auto start = tile->mapToScene(tile->boundingRect().center()).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window_, start + QPoint(24, 0), 20);
    QTRY_VERIFY(window_->property("dragActive").toBool());
    QCOMPARE(window_->property("draggedTileId").toString(), tileId);
}

void PrototypeWindowTest::moveMouseToGrid(const QString& groupId, const QPointF& position) {
    QVERIFY(item(QStringLiteral("grid-") + groupId));
    // Exercise the production edge-scroll Timer while the real MouseArea owns the grab.
    if (!viewportScene().adjusted(0, 48, 0, -48).contains(gridPosition(groupId, position))) {
        const auto viewport = viewportScene();
        const auto target = gridPosition(groupId, position);
        const auto edge = target.y() > viewport.center().y()
                              ? QPoint(qRound(viewport.left() + 80), qRound(viewport.bottom() - 8))
                              : QPoint(qRound(viewport.left() + 80), qRound(viewport.top() + 8));
        QTest::mouseMove(window_, edge, 20);
        QTRY_VERIFY_WITH_TIMEOUT(viewportScene().adjusted(0, 48, 0, -48).contains(
                                     gridPosition(groupId, position)),
                                 5000);
    }
    QTest::mouseMove(window_, gridPosition(groupId, position), 20);
    QTRY_COMPARE(window_->property("dragTargetGroupId").toString(), groupId);
}

void PrototypeWindowTest::mouseDragWithinGroup() {
    controller_->setEditMode(true);
    QVERIFY(controller_->editMode());
    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::groupsChanged);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    moveMouseToGrid(QStringLiteral("work"), QPointF(52, 164));
    QVERIFY(window_->property("dragValid").toBool());
    QCOMPARE(window_->property("dragColumn").toInt(), 0);
    QCOMPARE(window_->property("dragRow").toInt(), 2);
    auto* preview = item(QStringLiteral("dragPlacementPreview"));
    QVERIFY(preview && preview->isVisible());
    QCOMPARE(preview->size(), QSizeF(104, 104));
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("row")).toInt(), 0);
    QCOMPARE(changed.count(), 0);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("work"), QPointF(52, 164)));
    QTRY_COMPARE(changed.count(), 1);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("row")).toInt(), 2);
    QTRY_VERIFY(item(QStringLiteral("tile-work-standard-1"))->hasActiveFocus());
}

void PrototypeWindowTest::mouseDragAcrossGroups() {
    controller_->setEditMode(true);
    // The compact browsing chrome can leave the second group inside the default
    // viewport. Keep this case an actual edge-scroll scenario on every font kit.
    window_->resize(window_->width(), window_->minimumHeight());
    QTRY_COMPARE(item(QStringLiteral("grid-work"))->height(), 104.0);
    QTRY_VERIFY(!viewportScene().adjusted(0, 48, 0, -48).contains(
        gridPosition(QStringLiteral("daily"), QPointF(52, 164))));
    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::groupsChanged);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    moveMouseToGrid(QStringLiteral("daily"), QPointF(52, 164));
    QVERIFY(window_->property("dragValid").toBool());
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("groupId")).toString(),
             QStringLiteral("work"));
    QVERIFY(item(QStringLiteral("groupsFlickable"))->property("contentY").toReal() > 0);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("daily"), QPointF(52, 164)));
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("groupId")).toString(),
             QStringLiteral("daily"));
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("row")).toInt(), 2);
    QCOMPARE(tileGeometries().size(), 15);
    QTRY_VERIFY(item(QStringLiteral("tile-work-standard-1"))->hasActiveFocus());
}

void PrototypeWindowTest::mouseDragConflictAndEscape() {
    const auto before = service_->snapshot();
    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::groupsChanged);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    moveMouseToGrid(QStringLiteral("work"), QPointF(164, 52));
    QVERIFY(window_->property("dragValid").toBool());
    QCOMPARE(service_->snapshot(), before);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("work"), QPointF(164, 52)));
    QCOMPARE(service_->snapshot(), before);
    QCOMPARE(changed.count(), 0);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    moveMouseToGrid(QStringLiteral("work"), QPointF(164, 52));
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("work"), QPointF(164, 52)));
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(xdstarter::LayoutStore::validate(service_->snapshot()).success);
    QVERIFY(service_->snapshot() != before);
    QVERIFY(controller_->editMode());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->isVisible());
}

QObject* PrototypeWindowTest::sizeMenu() const {
    return window_->findChild<QObject*>(QStringLiteral("tileSizeMenu"));
}

void PrototypeWindowTest::openSizeMenu(const QString& tileId, bool keyboard) {
    settleMenus();
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(tile->hasActiveFocus());
    if (keyboard) {
        QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    } else {
        QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier,
                          tile->mapToScene(tile->boundingRect().center()).toPoint());
    }
    QTRY_VERIFY(sizeMenu());
    QTRY_VERIFY(sizeMenu()->property("visible").toBool());
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
}

void PrototypeWindowTest::chooseSize(const QString& itemName, bool keyboard) {
    QTRY_VERIFY(item(itemName));
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    auto* submenu = window_->findChild<QObject*>(QStringLiteral("tileDimensionsMenu"));
    QVERIFY(submenu);
    if (itemName.startsWith(QLatin1String("size")) && !submenu->property("opened").toBool()) {
        auto* dimensions = item(QStringLiteral("tileDimensionsMenuItem"));
        QVERIFY(dimensions);
        if (keyboard) {
            dimensions->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window_, Qt::Key_Right);
        } else {
            click(dimensions);
        }
        QTRY_VERIFY(submenu->property("opened").toBool());
    }
    QTRY_VERIFY(item(itemName)->isVisible());
    QTRY_VERIFY(item(itemName)->isEnabled());
    if (keyboard) {
        auto* targetWindow = item(itemName)->window();
        QVERIFY(targetWindow);
        item(itemName)->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(item(itemName)->hasActiveFocus());
        QTest::keyClick(targetWindow, Qt::Key_Return);
    } else {
        click(item(itemName));
    }
    QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
}

void PrototypeWindowTest::sizeMenuAndKeyboardEditing() {
    const auto id = QStringLiteral("work-standard-1");
    openSizeMenu(id, false);
    chooseSize(QStringLiteral("sizeSmallItem"));
    QTRY_COMPARE(tileData(id).value(QStringLiteral("size")).toString(), QStringLiteral("small"));
    QTRY_VERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());

    openSizeMenu(id, true);
    chooseSize(QStringLiteral("sizeStandardItem"), true);
    QTRY_COMPARE(tileData(id).value(QStringLiteral("size")).toString(), QStringLiteral("standard"));
    openSizeMenu(id, true);
    chooseSize(QStringLiteral("sizeWideItem"));
    QCOMPARE(tileData(id).value(QStringLiteral("size")).toString(), QStringLiteral("wide"));
    QVERIFY(xdstarter::LayoutStore::validate(service_->snapshot()).success);

    controller_->setEditMode(true);
    QTRY_VERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(tileData(id).value(QStringLiteral("column")).toInt(), 1);
    QVERIFY(!controller_->operationMessage().isEmpty());
    QVERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());
    // Two single-cell moves reach the complete empty row while retaining the stable tile focus.
    QTRY_VERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Down, Qt::ControlModifier);
    QTRY_COMPARE(tileData(id).value(QStringLiteral("row")).toInt(), 1);
    QTRY_VERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Down, Qt::ControlModifier);
    QTRY_COMPARE(tileData(id).value(QStringLiteral("row")).toInt(), 2);
    QTRY_VERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());
    openSizeMenu(id, true);
    chooseSize(QStringLiteral("sizeWideItem"), true);
    QTRY_COMPARE(tileData(id).value(QStringLiteral("size")).toString(), QStringLiteral("wide"));
    QTRY_COMPARE(item(QStringLiteral("tile-") + id)->size(), QSizeF(216, 104));
    openSizeMenu(id, true);
    chooseSize(QStringLiteral("sizeSmallItem"));
    QTRY_COMPARE(tileData(id).value(QStringLiteral("size")).toString(), QStringLiteral("small"));
    openSizeMenu(id, true);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
    QVERIFY(controller_->editMode());
    QVERIFY(window_->isVisible());
}

void PrototypeWindowTest::sizeMenuTextRemainsReadable() {
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        const auto theme = visualTokens_.value(QStringLiteral("themes")).toMap()
                               .value(dark ? QStringLiteral("dark") : QStringLiteral("light")).toMap();
        const QColor foreground(theme.value(QStringLiteral("foreground")).toString());
        const auto surfaceTheme =
            visualTokens_.value(QStringLiteral("interactionSurface"))
                .toMap()
                .value(QStringLiteral("themes"))
                .toMap()
                .value(dark ? QStringLiteral("dark") : QStringLiteral("light"))
                .toMap();
        const QColor hover(surfaceTheme.value(QStringLiteral("hover")).toString());
        const QColor pressed(surfaceTheme.value(QStringLiteral("pressed")).toString());
        openSizeMenu(QStringLiteral("work-standard-1"), true);
        for (const auto* name : {"sizeSmallItem", "sizeStandardItem", "sizeWideItem"}) {
            auto* option = item(QLatin1String(name));
            QVERIFY(option);
            auto* background = option->property("background").value<QQuickItem*>();
            auto* label = option->property("contentItem").value<QQuickItem*>();
            auto* indicator = option->property("indicator").value<QQuickItem*>();
            QVERIFY(background);
            QVERIFY(label);
            QVERIFY(indicator);
            option->setProperty("highlighted", true);
            QTRY_COMPARE(background->property("color").value<QColor>(), hover);
            QTRY_COMPARE(label->property("color").value<QColor>(), foreground);
            QCOMPARE(indicator->property("color").value<QColor>(), foreground);
            QVERIFY(contrastRatio(label->property("color").value<QColor>(),
                                  background->property("color").value<QColor>()) >= 4.5);
            option->setProperty("down", true);
            QTRY_COMPARE(background->property("color").value<QColor>(), pressed);
            QCOMPARE(label->property("color").value<QColor>(), foreground);
            QVERIFY(contrastRatio(label->property("color").value<QColor>(),
                                  background->property("color").value<QColor>()) >= 4.5);
            option->setProperty("down", false);
            option->setProperty("highlighted", false);
            QTRY_COMPARE(label->property("color").value<QColor>(), foreground);
        }
        QTest::keyClick(window_, Qt::Key_Escape);
        QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
    }
}

void PrototypeWindowTest::saveAndRestoreLayout() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    destroyWindow();
    createWindow(path);
    QSignalSpy saving(controller_.get(), &xdstarter::MenuController::savingChanged);
    QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 2));
    QVERIFY(controller_->resizeTile(QStringLiteral("work-standard-1"), QStringLiteral("wide")));
    controller_->setDarkTheme(false);
    QVERIFY(!QFile::exists(path));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path) && !controller_->saving(), 5000);
    QVERIFY(!controller_->persistenceError());
    QCOMPARE(saving.count(), 2);
    QFile primary(path);
    QFile backup(path + QStringLiteral(".bak"));
    QVERIFY(primary.open(QIODevice::ReadOnly));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    const auto firstSnapshotBytes = primary.readAll();
    QCOMPARE(backup.readAll(), firstSnapshotBytes);
    primary.close();
    backup.close();
    const auto firstSnapshot = QJsonDocument::fromJson(firstSnapshotBytes);
    QVERIFY(firstSnapshot.isObject());
    QCOMPARE(firstSnapshot.object().value(QStringLiteral("preferences")).toObject()
                 .value(QStringLiteral("theme")).toString(),
             QStringLiteral("light"));
    QJsonObject savedTile;
    for (const auto& group : firstSnapshot.object().value(QStringLiteral("groups")).toArray()) {
        if (group.toObject().value(QStringLiteral("id")).toString() != QLatin1String("daily")) {
            continue;
        }
        for (const auto& tile : group.toObject().value(QStringLiteral("tiles")).toArray()) {
            if (tile.toObject().value(QStringLiteral("id")).toString() ==
                QLatin1String("work-standard-1")) {
                savedTile = tile.toObject();
            }
        }
    }
    QCOMPARE(savedTile.value(QStringLiteral("column")).toInt(-1), 0);
    QCOMPARE(savedTile.value(QStringLiteral("row")).toInt(-1), 2);
    QCOMPARE(savedTile.value(QStringLiteral("size")).toString(), QStringLiteral("wide"));
    QVERIFY(controller_->persistenceStatus().contains(QStringLiteral("保存")));

    destroyWindow();
    createWindow(path);
    const auto restored = tileData(QStringLiteral("work-standard-1"));
    QCOMPARE(restored.value(QStringLiteral("groupId")).toString(), QStringLiteral("daily"));
    QCOMPARE(restored.value(QStringLiteral("row")).toInt(), 2);
    QCOMPARE(restored.value(QStringLiteral("size")).toString(), QStringLiteral("wide"));
    QVERIFY(!controller_->darkTheme());
    QVERIFY(!window_->property("darkTheme").toBool());
    QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0, 3));
    QVERIFY(controller_->flushSave());
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), firstSnapshotBytes);
    backup.close();
    destroyWindow();
    createWindow(path);
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("row")).toInt(), 3);
    QVERIFY(!controller_->darkTheme());
    // Tear down before the temporary directory removes the live configuration path.
    destroyWindow();
}

void PrototypeWindowTest::failedSaveCanBeRetried() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto parentPath = directory.filePath(QStringLiteral("blocked"));
    QFile parent(parentPath);
    QVERIFY(parent.open(QIODevice::WriteOnly));
    QCOMPARE(parent.write("occupied"), qint64(8));
    parent.close();
    const auto path = parentPath + QStringLiteral("/layout.json");
    destroyWindow();
    createWindow(path);
    QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 0, 2));
    controller_->requestSave();
    QTRY_VERIFY(!controller_->saving());
    QVERIFY(controller_->persistenceStatus().contains(QStringLiteral("无法")));
    QVERIFY(controller_->persistenceError());
    auto* failureTimer = window_->findChild<QObject*>(QStringLiteral("persistenceFeedbackTimer"));
    QVERIFY(failureTimer);
    QVERIFY(QMetaObject::invokeMethod(failureTimer, "stop"));
    QTRY_VERIFY(item(QStringLiteral("persistenceStatus"))->isVisible());
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("row")).toInt(), 2);
    QCOMPARE(item(QStringLiteral("persistenceStatus"))->property("text").toString(),
             controller_->persistenceStatus());
    QVERIFY(QFile::remove(parentPath));
    click(item(QStringLiteral("saveLayoutButton")));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path) && !controller_->saving(), 5000);
    destroyWindow();
    createWindow(path);
    QCOMPARE(tileData(QStringLiteral("work-standard-1")).value(QStringLiteral("row")).toInt(), 2);
    destroyWindow();
}

void PrototypeWindowTest::futureConfigurationIsReadOnly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    const QByteArray original = "{\"schemaVersion\":999,\"futureData\":\"keep\"}";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), qint64(original.size()));
    file.close();
    destroyWindow();
    createWindow(path);
    QVERIFY(controller_->readOnly());
    const auto themeBefore = controller_->themePreference();
    controller_->setThemePreference(QStringLiteral("system"));
    QCOMPARE(controller_->themePreference(), themeBefore);
    QVERIFY(controller_->persistenceError());
    QTRY_VERIFY(item(QStringLiteral("persistenceStatus"))->isVisible());
    QVERIFY(!item(QStringLiteral("editLayoutButton")));
    QVERIFY(!item(QStringLiteral("newGroupButton"))->isVisible());
    QVERIFY(!item(QStringLiteral("newGroupButton"))->isEnabled());
    QVERIFY(controller_->createGroup(QStringLiteral("不可写")).isEmpty());
    QVERIFY(!controller_->renameGroup(QStringLiteral("work"), QStringLiteral("不可写")));
    QVERIFY(!controller_->moveGroupBy(QStringLiteral("work"), 1));
    QVERIFY(!controller_->removeGroup(QStringLiteral("work")));
    QVERIFY(!controller_->renameTile(QStringLiteral("work-standard-1"), QStringLiteral("拒绝")));
    QVERIFY(!controller_->resetTileDisplayName(QStringLiteral("work-standard-1")));
    QVERIFY(!item(QStringLiteral("saveLayoutButton"))->isEnabled());
    controller_->setEditMode(true);
    QVERIFY(!controller_->editMode());
    QVERIFY(!controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 0, 2));
    openSizeMenu(QStringLiteral("work-standard-1"), true);
    QVERIFY(!item(QStringLiteral("sizeSmallItem"))->isEnabled());
    QVERIFY(!item(QStringLiteral("renameTileItem"))->isEnabled());
    QVERIFY(!item(QStringLiteral("resetTileNameItem"))->isEnabled());
    QTest::keyClick(window_, Qt::Key_Escape);
    settleMenus();
    const int shadow = qRound(window_->property("shadowInset").toReal());
    window_->resize(480 + 2 * shadow, 480 + 2 * shadow);
    auto* groupsViewport = item(QStringLiteral("groupsFlickable"));
    auto* close = item(QStringLiteral("closeMenuButton"));
    QVERIFY(groupsViewport && close);
    QTRY_COMPARE(groupsViewport->width(), qreal(368));
    QTRY_VERIFY(qAbs(groupsViewport->mapToScene(QPointF(groupsViewport->width(), 0)).x() -
                     close->mapToScene(QPointF(close->width(), 0)).x()) < 0.01);
    QVERIFY(groupsViewport->property("contentWidth").toReal() > groupsViewport->width());
    QCOMPARE(controller_->gridColumns(), 10);
    QVERIFY(!controller_->flushSave());
    destroyWindow();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
    file.close();

    // A newer file can appear after startup. The controller then becomes read-only while
    // the service still retains its initially writable load result.
    QVERIFY(QFile::remove(path));
    const auto state = std::make_shared<CatalogTestState>();
    createWindow(path, false, state);
    QVERIFY(!controller_->readOnly());
    QVERIFY(!service_->isReadOnly());
    pinFirstApplication(state);
    QVERIFY(controller_->flushSave());
    const auto writableSnapshot = service_->snapshot();
    QFile backup(path + QStringLiteral(".bak"));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    const auto originalBackup = backup.readAll();
    backup.close();
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(original), qint64(original.size()));
    file.close();
    controller_->requestSave();
    QTRY_VERIFY(controller_->readOnly());
    QTRY_VERIFY(!controller_->saving());
    QVERIFY(!service_->isReadOnly());
    const auto pinnedId = state->applications.first().id;
    const auto otherId = state->applications.last().id;
    QSignalSpy groupsChanged(controller_.get(), &xdstarter::MenuController::groupsChanged);
    QVERIFY(!controller_->unpinApplication(pinnedId));
    QVERIFY(!controller_->pinApplication(otherId));
    QVERIFY(!controller_->pinApplicationAt(otherId, QStringLiteral("default"), 0, 0));
    QVERIFY(!controller_->previewApplicationPlacement(otherId, QStringLiteral("default"), 0, 0)
                 .value(QStringLiteral("valid"))
                 .toBool());
    QVERIFY(!controller_->setGridColumnsForWidth(216));
    QCOMPARE(service_->snapshot(), writableSnapshot);
    QCOMPARE(groupsChanged.count(), 0);
    QVERIFY(controller_->isApplicationPinned(pinnedId));
    QVERIFY(!controller_->flushSave());
    destroyWindow();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), originalBackup);
}

QVariant PrototypeWindowTest::modelValue(int row, const QByteArray& role) const {
    auto* model = catalog_ ? catalog_->applicationsModel() : nullptr;
    if (!model || row < 0 || row >= model->rowCount()) {
        return {};
    }
    const auto roles = model->roleNames();
    for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
        if (iterator.value() == role) {
            return model->data(model->index(row, 0), iterator.key());
        }
    }
    return {};
}

void PrototypeWindowTest::pinFirstApplication(const std::shared_ptr<CatalogTestState>& state) {
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), state->applications.size());
    const auto applicationId = state->applications.first().id;
    const auto buttonName = QStringLiteral("applicationLaunch-") + applicationId;
    QTRY_VERIFY(item(buttonName));
    QVERIFY(item(buttonName)->isEnabled());
    QTest::mouseClick(
        window_, Qt::RightButton, Qt::NoModifier,
        item(buttonName)->mapToScene(item(buttonName)->boundingRect().center()).toPoint());
    auto* menu = window_->findChild<QObject*>(QStringLiteral("pinApplicationMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("visible").toBool());
    const auto groupItem = QStringLiteral("applicationPinAction");
    QTRY_VERIFY(item(groupItem));
    QTRY_VERIFY(menu->property("opened").toBool());
    item(groupItem)->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(item(groupItem)->hasActiveFocus());
    QVERIFY(item(groupItem)->window());
    QTest::keyClick(item(groupItem)->window(), Qt::Key_Return);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QTRY_COMPARE(tileGeometries().size(), 1);
    QVERIFY(item(buttonName)->isEnabled());
    QVERIFY(controller_->isApplicationPinned(applicationId));
    QTRY_VERIFY(item(buttonName)->hasActiveFocus());
}

void PrototypeWindowTest::equivalentShortcutsPreserveOldTilesAndSearch() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    auto state = std::make_shared<CatalogTestState>();
    const auto user =
        catalogTarget(QStringLiteral("Bitwarden"), QStringLiteral("C:/User/Bitwarden.lnk"));
    const auto common =
        catalogTarget(QStringLiteral("Bitwarden"), QStringLiteral("C:/Common/Bitwarden.lnk"));
    const auto other = state->applications.last();
    state->applications = {user, other};
    state->equivalentTargetIds.insert(common.id, user.id);
    xdstarter::PrototypeService restored(grid_, {}, false);
    const auto oldPin = restored.pinApplication(xdstarter::applicationTargetJson(common));
    QVERIFY(oldPin.success);
    QVERIFY(restored.renameTile(oldPin.tileId, QStringLiteral("我的密码库")).success);
    QVERIFY(restored.pinApplication(xdstarter::applicationTargetJson(user)).success);
    QVERIFY(restored.pinApplication(xdstarter::applicationTargetJson(other)).success);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto bytes = QJsonDocument(restored.snapshot()).toJson();
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    destroyWindow();
    createWindow(path, false, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QVERIFY(controller_->isApplicationPinned(user.id));
    int userRow = -1;
    for (int row = 0; row < catalog_->applicationsModel()->rowCount(); ++row)
        if (modelValue(row, "appId").toString() == user.id)
            userRow = row;
    QVERIFY(userRow >= 0);
    QVERIFY(modelValue(userRow, "pinned").toBool());
    const auto original = service_->snapshot();
    QVERIFY(controller_->pinApplication(user.id));
    QVERIFY(!controller_->previewApplicationPlacement(user.id, QStringLiteral("default"), 8, 8)
                 .value(QStringLiteral("valid"))
                 .toBool());
    QVERIFY(!controller_->pinApplicationAt(user.id, QStringLiteral("default"), 8, 8));
    QCOMPARE(service_->snapshot(), original);
    QCOMPARE(
        service_->targetForTile(oldPin.tileId).value(QStringLiteral("shortcutPath")).toString(),
        common.shortcutPath);
    controller_->setSearchQuery(QStringLiteral("Bitwarden"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("appId")).toString(),
             user.id);
    QVERIFY(controller_->searchResults().first().toMap().value(QStringLiteral("pinned")).toBool());
    controller_->setSearchQuery({});
    QTRY_COMPARE(controller_->searchResults().size(), 0);
    controller_->setSearchQuery(QStringLiteral("我的密码库"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("appId")).toString(),
             user.id);
    QVERIFY(controller_->unpinApplication(user.id));
    QVERIFY(!controller_->isApplicationPinned(common.id));
    QVERIFY(!controller_->isApplicationPinned(user.id));
    QVERIFY(controller_->isApplicationPinned(other.id));
    QCOMPARE(tileGeometries().size(), 1);
    // Removing the representative invalidates the old alias on refresh.
    controller_->setSearchQuery({});
    state->includeApplications = false;
    catalog_->refreshApplications();
    QTRY_VERIFY(!catalog_->busy());
    QCOMPARE(catalog_->canonicalApplicationId(common.id), common.id);
}

void PrototypeWindowTest::applicationDirectoryAndPinning() {
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, false, state);
    QCOMPARE(tileGeometries().size(), 0);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QCOMPARE(modelValue(0, "displayName").toString(), state->applications.first().name);
    QCOMPARE(modelValue(0, "kind").toString(), QStringLiteral("shortcut"));
    QCOMPARE(modelValue(1, "kind").toString(), QStringLiteral("packaged"));
    QTRY_COMPARE(state->icons.load(), 2);
    QVERIFY(state->workerOnly);
    const auto applicationId = state->applications.first().id;
    const auto launchButton = QStringLiteral("applicationLaunch-") + applicationId;
    QTRY_VERIFY(item(launchButton));
    auto* accessible = QAccessible::queryAccessibleInterface(item(launchButton));
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), state->applications.first().name);
    auto* icon = visualItem(item(QStringLiteral("appRow-") + applicationId),
                            QStringLiteral("applicationIcon"));
    QVERIFY(icon);
    QTRY_COMPARE(icon->property("status").toInt(), 1); // QML Image.Ready.
    QVERIFY(icon->property("source").toUrl().toString().startsWith(
        QStringLiteral("image://application-icons/")));
    const auto iconShowsLoadedPixels = [this, icon] {
        const auto image = window_->grabWindow();
        if (image.isNull()) {
            return false;
        }
        const auto capture = qEnvironmentVariable("XDSTARTER_INTERACTION_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            image.save(QDir(capture).filePath(QStringLiteral("catalog-icon.png")));
        }
        const auto center = icon->mapToScene(icon->boundingRect().center());
        const auto pixel = QPoint(qRound(center.x() * image.width() / window_->width()),
                                   qRound(center.y() * image.height() / window_->height()));
        if (!image.rect().contains(pixel))
            return false;
        const auto actual = image.pixelColor(pixel);
        const QColor expected(QStringLiteral("#36D6CB"));
        // Software opacity composition may round an RGB channel by one unit.
        return std::abs(actual.red() - expected.red()) <= 1 &&
               std::abs(actual.green() - expected.green()) <= 1 &&
               std::abs(actual.blue() - expected.blue()) <= 1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(iconShowsLoadedPixels(), 5000);

    pinFirstApplication(state);
    QCOMPARE(tileGeometries().size(), 1);
    const auto tileId = service_->previewGroups().first().tiles.first().id;
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    QCOMPARE(tileData(tileId).value(QStringLiteral("name")).toString(),
             state->applications.first().name);
    auto* tileIcon = visualItem(tile, QStringLiteral("tileIcon"));
    QVERIFY(tileIcon);
    QTRY_COMPARE(tileIcon->property("status").toInt(), 1);
    openSizeMenu(tileId, true);
    chooseSize(QStringLiteral("unpinTileItem"), true);
    QTRY_COMPARE(tileGeometries().size(), 0);
    QVERIFY(!item(QStringLiteral("pinButton-") + applicationId));
    QVERIFY(!controller_->isApplicationPinned(applicationId));
    QCOMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QCOMPARE(state->launches.load(), 0);
    QTRY_VERIFY(item(QStringLiteral("moreMenuButton"))->hasActiveFocus());

    // Drag the actual catalogue delegate through its threshold and release over the grid.
    auto* catalogueButton = item(launchButton);
    QVERIFY(catalogueButton);
    const auto cataloguePress =
        catalogueButton->mapToScene(catalogueButton->boundingRect().center()).toPoint();
    const auto beforeCatalogueDrag = service_->snapshot();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, cataloguePress);
    QTest::mouseMove(window_, cataloguePress + QPoint(24, 0), 20);
    QTRY_COMPARE(window_->property("draggedApplicationId").toString(), applicationId);
    QCOMPARE(service_->snapshot(), beforeCatalogueDrag);
    moveMouseToGrid(QStringLiteral("default"), QPointF(52, 52));
    QVERIFY(window_->property("dragValid").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("default"), QPointF(52, 52)));
    QTRY_COMPARE(tileGeometries().size(), 1);
    QVERIFY(controller_->isApplicationPinned(applicationId));
    QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier, cataloguePress);
    auto* applicationMenu = window_->findChild<QObject*>(QStringLiteral("pinApplicationMenu"));
    QVERIFY(applicationMenu);
    QTRY_VERIFY(applicationMenu->property("opened").toBool());
    QCOMPARE(item(QStringLiteral("applicationPinAction"))->property("text").toString(),
             QStringLiteral("取消固定磁贴"));
    click(item(QStringLiteral("applicationPinAction")));
    QTRY_COMPARE(tileGeometries().size(), 0);
    QTRY_VERIFY(!applicationMenu->property("visible").toBool());
    QTRY_VERIFY(item(launchButton)->hasActiveFocus());
    QVERIFY(!controller_->isApplicationPinned(applicationId));
    QCOMPARE(state->launches.load(), 0);
    const auto beforeCancelledCatalogueDrag = service_->snapshot();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, cataloguePress);
    QTest::mouseMove(window_, cataloguePress + QPoint(24, 0), 20);
    QTRY_VERIFY(window_->property("dragActive").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    QCOMPARE(service_->snapshot(), beforeCancelledCatalogueDrag);

    // Compact browsing changes panes while a debounced resize adjusts real columns.
    const int shadow = qRound(window_->property("shadowInset").toReal());
    window_->resize(480 + 2 * shadow, 480 + 2 * shadow);
    QTRY_VERIFY(item(QStringLiteral("catalogViewButton"))->isVisible());
    QVERIFY(item(QStringLiteral("groupsScroll"))->isVisible());
    click(item(QStringLiteral("catalogViewButton")));
    QTRY_VERIFY(item(QStringLiteral("catalogPanel"))->isVisible());
    QVERIFY(!item(QStringLiteral("groupsScroll"))->isVisible());
    QTest::qWait(600); // Settle the deliberate width-to-grid debounce before starting a drag.
    catalogueButton = item(launchButton);
    QVERIFY(catalogueButton);
    const auto compactPress =
        catalogueButton->mapToScene(catalogueButton->boundingRect().center()).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, compactPress);
    QTest::mouseMove(window_, compactPress + QPoint(24, 0), 20);
    QTRY_COMPARE(window_->property("draggedApplicationId").toString(), applicationId);
    QVERIFY(item(QStringLiteral("catalogPanel"))->isVisible());
    QVERIFY(item(QStringLiteral("groupsScroll"))->isVisible());
    moveMouseToGrid(QStringLiteral("default"), QPointF(52, 52));
    QVERIFY(window_->property("dragValid").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("default"), QPointF(52, 52)));
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QTRY_VERIFY(controller_->isApplicationPinned(applicationId));
    QTRY_VERIFY(item(QStringLiteral("groupsScroll"))->isVisible());
    QVERIFY(!item(QStringLiteral("catalogPanel"))->isVisible());
    QVERIFY(controller_->unpinApplication(applicationId));
    QTRY_VERIFY(controller_->gridColumns() < 10);
    QCOMPARE(item(QStringLiteral("grid-default"))->width(),
             qreal(controller_->gridColumns() * controller_->gridPitch() - controller_->gridGap()));

    window_->resize(980 + 2 * shadow, 680 + 2 * shadow);
    QTRY_VERIFY(item(QStringLiteral("catalogPanel"))->isVisible());
    QTRY_COMPARE(item(QStringLiteral("catalogPanel"))->width(),
                 visualTokens_.value(QStringLiteral("layout")).toMap().value(QStringLiteral("catalogWidth")).toReal());
    QTRY_VERIFY(item(QStringLiteral("refreshApplicationsButton"))->isVisible());
    QTRY_VERIFY(item(QStringLiteral("refreshApplicationsButton"))->isEnabled());
    const auto scans = state->scans.load();
    click(item(QStringLiteral("refreshApplicationsButton")));
    QTRY_VERIFY(state->scans.load() > scans);
    QTRY_VERIFY(!controller_->catalogBusy());
    QCOMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QVERIFY(state->workerOnly);

    // A refresh may remove the source while its context menu is open. Restore an
    // actual fallback control rather than leaving focus in the destroyed delegate.
    auto* refreshedApplication = item(launchButton);
    QVERIFY(refreshedApplication);
    QTest::mouseClick(
        window_, Qt::RightButton, Qt::NoModifier,
        refreshedApplication->mapToScene(refreshedApplication->boundingRect().center()).toPoint());
    QTRY_VERIFY(applicationMenu->property("opened").toBool());
    state->includeApplications = false;
    controller_->refreshApplications();
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 0);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!applicationMenu->property("visible").toBool());
    QTRY_VERIFY(item(QStringLiteral("moreMenuButton"))->hasActiveFocus());
}

void PrototypeWindowTest::applicationLaunchLifecycle() {
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, false, state);
    pinFirstApplication(state);
    QCOMPARE(tileGeometries().size(), 1);
    const auto applicationId = state->applications.first().id;
    const auto buttonName = QStringLiteral("applicationLaunch-") + applicationId;
    QSignalSpy failed(controller_.get(), &xdstarter::MenuController::launchFailed);
    QSignalSpy succeeded(controller_.get(), &xdstarter::MenuController::launchSucceeded);
    click(item(buttonName));
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(window_->isVisible());
    QCOMPARE(tileGeometries().size(), 1);
    QTRY_VERIFY(item(QStringLiteral("selectionStatus"))->property("text").toString().contains(
        QStringLiteral("测试启动失败")));
    QTRY_VERIFY(!controller_->launchBusy());
    QCOMPARE(state->launches.load(), 1);

    state->launchSucceeds = true;
    QTRY_VERIFY(item(buttonName)->isEnabled());
    item(buttonName)->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(item(buttonName)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(succeeded.count(), 1);
    QTRY_VERIFY(!window_->isVisible());
    QCOMPARE(state->launches.load(), 2);
    const auto tileId = service_->previewGroups().first().tiles.first().id;
    QVERIFY(tileData(tileId).value(QStringLiteral("available")).toBool());

    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    // Default editability does not consume an ordinary tile click as a preview.
    QVERIFY(controller_->editMode());
    click(item(QStringLiteral("tile-") + tileId));
    QTRY_COMPARE(succeeded.count(), 2);
    QTRY_VERIFY(!window_->isVisible());
    QCOMPARE(state->launches.load(), 3);
    QVERIFY(state->workerOnly);
}

void PrototypeWindowTest::searchFocusCompositionAndEscape() {
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, true, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    auto* field = item(QStringLiteral("searchField"));
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    auto* accessible = QAccessible::queryAccessibleInterface(field);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("搜索应用"));

    item(QStringLiteral("moreMenuButton"))->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(field->hasActiveFocus());
    controller_->setEditMode(true);
    QVERIFY(window_->setProperty("dismissalManaged", true));
    QSignalSpy dismissRequested(window_, SIGNAL(dismissRequested()));

    // A preedit is not a committed query, regardless of its visible candidate text.
    QInputMethodEvent preedit(QStringLiteral("ceshi"), {});
    QCoreApplication::sendEvent(field, &preedit);
    QTRY_VERIFY(field->property("inputMethodComposing").toBool());
    QVERIFY(controller_->searchQuery().isEmpty());
    QVERIFY(!controller_->searchActive());
    QCOMPARE(state->launches.load(), 0);
    QInputMethodEvent committed;
    committed.setCommitString(QStringLiteral("测试"));
    QCoreApplication::sendEvent(field, &committed);
    QTRY_VERIFY(!field->property("inputMethodComposing").toBool());
    QTRY_COMPARE(controller_->searchQuery(), QStringLiteral("测试"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 2);
    QVERIFY(item(QStringLiteral("searchPanel"))->isVisible());
    QVERIFY(!item(QStringLiteral("browsePanel"))->isVisible());

    // Popup handling precedes clearing the query; clearing precedes leaving edit mode.
    click(item(QStringLiteral("shortcut-power")));
    auto* menu = window_->findChild<QObject*>(QStringLiteral("powerMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QCOMPARE(controller_->searchQuery(), QStringLiteral("测试"));
    QVERIFY(controller_->editMode());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(controller_->searchQuery().isEmpty());
    QTRY_VERIFY(!controller_->searchActive());
    QTRY_VERIFY(item(QStringLiteral("browsePanel"))->isVisible());
    QTRY_VERIFY(field->hasActiveFocus());
    QVERIFY(controller_->editMode());
    QCOMPARE(dismissRequested.count(), 0);
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(controller_->editMode());
    QTRY_COMPARE(dismissRequested.count(), 1);
    QCOMPARE(state->launches.load(), 0);
}

void PrototypeWindowTest::searchPreservesBrowsing() {
    const auto state = std::make_shared<CatalogTestState>();
    state->applications.clear();
    for (int index = 0; index < 40; ++index) {
        state->applications.append(
            catalogTarget(QStringLiteral("App %1").arg(index, 2, 10, QLatin1Char('0')),
                          QStringLiteral("C:/Fixture/App-%1.lnk").arg(index)));
    }
    destroyWindow();
    createWindow({}, true, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 40);
    auto* field = item(QStringLiteral("searchField"));
    auto* applications = item(QStringLiteral("applicationList"));
    auto* groups = item(QStringLiteral("groupsFlickable"));
    QVERIFY(field);
    QVERIFY(applications);
    QVERIFY(groups);
    QTRY_VERIFY(!window_->property("compactLayout").toBool());
    QVERIFY(applications->setProperty("contentY", 120.0));
    QVERIFY(groups->setProperty("contentY", 140.0));
    QTRY_COMPARE(applications->property("contentY").toReal(), qreal(120));
    QTRY_COMPARE(groups->property("contentY").toReal(), qreal(140));
    const auto before = controller_->groups();
    const auto geometry = tileGeometries();

    QVERIFY(field->setProperty("text", QStringLiteral("App")));
    QTRY_VERIFY(controller_->searchActive());
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 40);
    QVERIFY(!item(QStringLiteral("browsePanel"))->isVisible());
    click(item(QStringLiteral("clearSearchButton")));
    QTRY_VERIFY(!controller_->searchActive());
    QTRY_VERIFY(item(QStringLiteral("browsePanel"))->isVisible());
    QTRY_COMPARE(applications->property("contentY").toReal(), qreal(120));
    QTRY_COMPARE(groups->property("contentY").toReal(), qreal(140));
    QCOMPARE(controller_->groups(), before);
    QCOMPARE(tileGeometries(), geometry);

    QVERIFY(field->setProperty("text", QStringLiteral("no-such-application")));
    QTRY_VERIFY(controller_->searchActive());
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_VERIFY(controller_->searchResults().isEmpty());
    QTRY_VERIFY(item(QStringLiteral("emptySearchHint"))->isVisible());
    QVERIFY(item(QStringLiteral("emptySearchHint"))
                ->property("text")
                .toString()
                .contains(QStringLiteral("no-such-application")));
    click(item(QStringLiteral("clearSearchButton")));
    QTRY_VERIFY(!controller_->searchActive());

    const int shadow = qRound(window_->property("shadowInset").toReal());
    window_->resize(480 + 2 * shadow, 480 + 2 * shadow);
    QTRY_VERIFY(item(QStringLiteral("catalogViewButton"))->isVisible());
    click(item(QStringLiteral("catalogViewButton")));
    QTRY_VERIFY(window_->property("showApplications").toBool());
    QTRY_VERIFY(item(QStringLiteral("catalogPanel"))->isVisible());
    QTRY_VERIFY(controller_->gridColumns() < 10);
    const auto compactBefore = controller_->groups();
    QVERIFY(applications->setProperty("contentY", 180.0));
    QTRY_COMPARE(applications->property("contentY").toReal(), qreal(180));
    QVERIFY(field->setProperty("text", QStringLiteral("App 01")));
    QTRY_VERIFY(controller_->searchActive());
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    click(item(QStringLiteral("clearSearchButton")));
    QTRY_VERIFY(!controller_->searchActive());
    QTRY_VERIFY(item(QStringLiteral("catalogPanel"))->isVisible());
    QVERIFY(window_->property("showApplications").toBool());
    QVERIFY(!item(QStringLiteral("groupsScroll"))->isVisible());
    QTRY_COMPARE(applications->property("contentY").toReal(), qreal(180));
    QCOMPARE(controller_->groups(), compactBefore);
    QCOMPARE(item(QStringLiteral("grid-work"))->width(),
             qreal(controller_->gridColumns() * controller_->gridPitch() - controller_->gridGap()));
    QCOMPARE(state->launches.load(), 0);
}

void PrototypeWindowTest::searchKeyboardAndLaunchLifecycle() {
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, false, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    auto* field = item(QStringLiteral("searchField"));
    QVERIFY(field);
    QInputMethodEvent committed;
    committed.setCommitString(QStringLiteral("测试"));
    QCoreApplication::sendEvent(field, &committed);
    QTRY_VERIFY(controller_->searchActive());
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 2);
    const auto firstId = state->applications[0].id;
    const auto secondId = state->applications[1].id;
    const auto firstName = QStringLiteral("searchResult-") + firstId;
    const auto secondName = QStringLiteral("searchResult-") + secondId;
    QTRY_VERIFY(item(firstName));
    QTRY_VERIFY(item(secondName));
    auto* accessible = QAccessible::queryAccessibleInterface(item(firstName));
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), state->applications[0].name);
    QTest::keyClick(window_, Qt::Key_Down);
    QTRY_VERIFY(item(firstName) && item(firstName)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Down);
    QTRY_VERIFY(item(secondName) && item(secondName)->hasActiveFocus());
    const auto beforeRefresh = controller_->searchResults();
    QSignalSpy resultsChanged(controller_.get(), &xdstarter::MenuController::searchResultsChanged);
    emit catalog_->iconReady(firstId);
    emit catalog_->catalogChanged();
    QCoreApplication::processEvents();
    QCOMPARE(controller_->searchQuery(), QStringLiteral("测试"));
    QCOMPARE(controller_->searchResults(), beforeRefresh);
    QCOMPARE(resultsChanged.count(), 0);
    QVERIFY(item(secondName) && item(secondName)->hasActiveFocus());
    QCOMPARE(item(QStringLiteral("searchResultsList"))->property("currentIndex").toInt(), 1);
    QVERIFY(controller_->pinApplication(firstId));
    QTRY_VERIFY(resultsChanged.count() > 0);
    QTRY_VERIFY(
        controller_->searchResults().first().toMap().value(QStringLiteral("pinned")).toBool());
    QTRY_VERIFY(item(secondName) && item(secondName)->hasActiveFocus());
    QCOMPARE(item(QStringLiteral("searchResultsList"))->property("currentIndex").toInt(), 1);
    QCOMPARE(controller_->searchQuery(), QStringLiteral("测试"));
    QTest::keyClick(window_, Qt::Key_Up);
    QTRY_VERIFY(item(firstName) && item(firstName)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Up);
    QTRY_VERIFY(field->hasActiveFocus());

    // Explicit export uses only fixture names/icons and the QQuickWindow's own pixels.
    const auto artifactDirectory = qEnvironmentVariable("XDSTARTER_SEARCH_VALIDATION_DIR");
    if (!artifactDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(artifactDirectory));
        QJsonArray frames;
        for (const bool dark : {true, false}) {
            controller_->setDarkTheme(dark);
            field->forceActiveFocus(Qt::OtherFocusReason);
            QTest::mouseMove(window_, QPoint(1, 1));
            window_->requestUpdate();
            QTest::qWait(80);
            const auto image = window_->grabWindow();
            QVERIFY(!image.isNull());
            const auto name =
                dark ? QStringLiteral("search-dark.png") : QStringLiteral("search-light.png");
            QVERIFY(image.save(QDir(artifactDirectory).filePath(name)));
            frames.append(
                QJsonObject{{QStringLiteral("file"), name},
                            {QStringLiteral("widthPx"), image.width()},
                            {QStringLiteral("heightPx"), image.height()},
                            {QStringLiteral("devicePixelRatio"), window_->devicePixelRatio()}});
        }
        const QJsonObject evidence{
            {QStringLiteral("fixtureOnly"), true},
            {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
            {QStringLiteral("platformName"), QGuiApplication::platformName()},
            {QStringLiteral("query"), controller_->searchQuery()},
            {QStringLiteral("resultCount"), controller_->searchResults().size()},
            {QStringLiteral("frames"), frames}};
        QFile output(QDir(artifactDirectory).filePath(QStringLiteral("search-render.json")));
        QVERIFY2(output.open(QIODevice::WriteOnly), qPrintable(output.errorString()));
        QVERIFY(output.write(QJsonDocument(evidence).toJson()) > 0);
    }

    QSignalSpy failed(controller_.get(), &xdstarter::MenuController::launchFailed);
    QSignalSpy succeeded(controller_.get(), &xdstarter::MenuController::launchSucceeded);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(failed.count(), 1);
    QTRY_VERIFY(!controller_->launchBusy());
    QCOMPARE(state->launches.load(), 1);
    QVERIFY(window_->isVisible());
    QVERIFY(item(QStringLiteral("selectionStatus"))
                ->property("text")
                .toString()
                .contains(QStringLiteral("测试启动失败")));

    state->launchSucceeds = true;
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_VERIFY(item(secondName));
    click(item(secondName));
    QTRY_COMPARE(succeeded.count(), 1);
    QTRY_VERIFY(!window_->isVisible());
    QCOMPARE(state->launches.load(), 2);
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    QTRY_VERIFY(field->hasActiveFocus());
    QCOMPARE(controller_->searchQuery(), QStringLiteral("测试"));
    QVERIFY(field->setProperty("text", state->applications[0].name));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(succeeded.count(), 2);
    QTRY_VERIFY(!window_->isVisible());
    QCOMPARE(state->launches.load(), 3);
    QVERIFY(state->workerOnly);
}

void PrototypeWindowTest::searchRefreshAliasesAndRestart() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("search-layout.json"));
    const auto state = std::make_shared<CatalogTestState>();
    const auto firstId = state->applications[0].id;
    const auto secondId = state->applications[1].id;
    destroyWindow();
    createWindow(path, false, state);
    pinFirstApplication(state);
    controller_->setSearchQuery(QStringLiteral("测试"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 2);
    QVERIFY(controller_->flushSave());
    destroyWindow();

    // A persisted display name may differ from current catalogue metadata. Both
    // names must match the same launch identity, without duplicating its result.
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    auto document = QJsonDocument::fromJson(saved.readAll()).object();
    saved.close();
    auto targets = document.value(QStringLiteral("targets")).toArray();
    QCOMPARE(targets.size(), 1);
    auto target = targets[0].toObject();
    QCOMPARE(target.value(QStringLiteral("id")).toString(), firstId);
    target.insert(QStringLiteral("name"), QStringLiteral("自定义测试别名"));
    targets.replace(0, target);
    document.insert(QStringLiteral("targets"), targets);
    QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(saved.write(QJsonDocument(document).toJson()) > 0);
    saved.close();

    createWindow(path, false, state);
    QVERIFY(controller_->searchQuery().isEmpty());
    QVERIFY(!controller_->searchActive());
    QCOMPARE(item(QStringLiteral("searchField"))->property("text").toString(), QString{});
    QTRY_VERIFY(!controller_->catalogBusy());
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    const auto tileId = service_->previewGroups().first().tiles.first().id;
    QCOMPARE(tileData(tileId).value(QStringLiteral("name")).toString(),
             QStringLiteral("自定义测试别名"));
    controller_->setSearchQuery(QStringLiteral("测试"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 2);
    int firstMatches = 0;
    for (const auto& value : controller_->searchResults())
        firstMatches += value.toMap().value(QStringLiteral("appId")).toString() == firstId;
    QCOMPARE(firstMatches, 1);
    controller_->setSearchQuery(QStringLiteral("自定义"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("appId")).toString(),
             firstId);
    QCOMPARE(controller_->searchResults()
                 .first()
                 .toMap()
                 .value(QStringLiteral("displayName"))
                 .toString(),
             state->applications[0].name);

    // Changing the query invalidates the previous result before the worker returns.
    controller_->setSearchQuery(state->applications[1].name);
    QVERIFY(controller_->searchBusy());
    QVERIFY(!controller_->launchSearchResult(firstId));
    QVERIFY(!controller_->launchSearchResult(QStringLiteral("missing-search-id")));
    QCOMPARE(state->launches.load(), 0);
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("appId")).toString(),
             secondId);
    QVERIFY(!controller_->launchSearchResult(firstId));

    // Removing the catalogue preserves a pinned target as unavailable, while an
    // unpinned old result disappears. Neither identity can be launched from search.
    state->includeApplications = false;
    controller_->refreshApplications();
    QTRY_VERIFY(!controller_->catalogBusy());
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 0);
    QTRY_VERIFY(!tileData(tileId).value(QStringLiteral("available")).toBool());
    QTRY_VERIFY(tileData(tileId)
                    .value(QStringLiteral("unavailableReason"))
                    .toString()
                    .contains(QStringLiteral("测试目标已移除")));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_VERIFY(controller_->searchResults().isEmpty());
    QVERIFY(!controller_->launchSearchResult(secondId));
    controller_->setSearchQuery(QStringLiteral("自定义"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    const auto unavailable = controller_->searchResults().first().toMap();
    QCOMPARE(unavailable.value(QStringLiteral("appId")).toString(), firstId);
    QVERIFY(!unavailable.value(QStringLiteral("available")).toBool());
    QVERIFY(unavailable.value(QStringLiteral("unavailableReason"))
                .toString()
                .contains(QStringLiteral("测试目标已移除")));
    QVERIFY(!controller_->launchSearchResult(firstId));
    QCOMPARE(state->launches.load(), 0);
    QVERIFY(window_->isVisible());
    QVERIFY(state->workerOnly);
}

void PrototypeWindowTest::openGroupMenu(const QString& groupId) {
    settleMenus();
    const auto buttonName = QStringLiteral("groupActions-") + groupId;
    QTRY_VERIFY(item(buttonName));
    QVERIFY(item(buttonName)->isVisible());
    item(buttonName)->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(item(buttonName)->hasActiveFocus());
    QTRY_VERIFY(viewportScene().adjusted(-1, -1, 1, 1).contains(
        item(buttonName)->mapRectToScene(item(buttonName)->boundingRect())));
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    auto* menu = window_->findChild<QObject*>(QStringLiteral("groupMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    QTRY_COMPARE(window_->property("managedGroupId").toString(), groupId);
    QVERIFY(window_->property("internalPopupVisible").toBool());
    QVERIFY(window_->property("dismissalBlocked").toBool());
}

void PrototypeWindowTest::chooseGroupAction(const QString& itemName) {
    auto* action = item(itemName);
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    action->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(action->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
}

void PrototypeWindowTest::submitGroupName(const QString& name) {
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("groupNameDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto* field = item(QStringLiteral("groupNameField"));
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    QVERIFY(field->setProperty("text", name));
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(!dialog->property("visible").toBool());
}

void PrototypeWindowTest::groupManagementLifecycle() {
    const auto geometry = tileGeometries();
    QVERIFY(!item(QStringLiteral("newGroupButton"))->isVisible());
    controller_->setEditMode(true);
    QVERIFY(controller_->editMode());
    openMoreMenu();
    auto* create = item(QStringLiteral("newGroupButton"));
    QVERIFY(create && create->isVisible() && create->isEnabled());
    create->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    submitGroupName(QStringLiteral("  我的工具 Tools  "));
    QTRY_COMPARE(controller_->groups().size(), 4);
    const auto groupId = controller_->groups().last().toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(controller_->groups().last().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("我的工具 Tools"));
    QCOMPARE(tileGeometries(), geometry);
    QTRY_VERIFY2(
        item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus(),
        qPrintable(QStringLiteral("focus=%1 pending=%2 popup=%3 group=%4")
                       .arg(window_->activeFocusItem() ? window_->activeFocusItem()->objectName()
                                                       : QStringLiteral("none"))
                       .arg(window_->property("groupFocusPending").toBool())
                       .arg(window_->property("internalPopupVisible").toBool())
                       .arg(window_->property("focusedGroupId").toString())));

    openGroupMenu(groupId);
    QVERIFY(!item(QStringLiteral("groupMoveDownAction"))->isEnabled());
    QVERIFY(item(QStringLiteral("groupDeleteAction"))->isEnabled());
    chooseGroupAction(QStringLiteral("groupRenameAction"));
    const auto longName = QStringLiteral("<b>中文 Tools & 长名称</b> ") + QString(90, QChar(u'组'));
    QVERIFY(longName.size() <= controller_->maximumGroupNameLength());
    submitGroupName(longName);
    auto* label = item(QStringLiteral("groupTitle-") + groupId);
    QVERIFY(label);
    QCOMPARE(label->property("text").toString(), longName);
    QCOMPARE(label->property("textFormat").toInt(), 0); // Text.PlainText.
    QTRY_VERIFY(label->property("truncated").toBool());
    auto* group = item(QStringLiteral("group-") + groupId);
    auto* groupActions = item(QStringLiteral("groupActions-") + groupId);
    QVERIFY(group && groupActions);
    QVERIFY(group->boundingRect().contains(label->mapRectToItem(group, label->boundingRect())));
    QCOMPARE(label->mapToItem(group, QPointF(label->width(), 0)).x(), groupActions->x() - 8);
    auto* accessible = QAccessible::queryAccessibleInterface(
        item(QStringLiteral("groupActions-") + groupId));
    QVERIFY(accessible);
    QVERIFY(accessible->text(QAccessible::Name).contains(longName));
    QSignalSpy groupsChanged(controller_.get(), &xdstarter::MenuController::groupsChanged);
    QVERIFY(controller_->renameGroup(groupId, longName));
    QCOMPARE(groupsChanged.count(), 0);

    openGroupMenu(groupId);
    chooseGroupAction(QStringLiteral("groupMoveUpAction"));
    QTRY_COMPARE(controller_->groups()[2].toMap().value(QStringLiteral("id")).toString(), groupId);
    const auto focusDescription = [this] {
        const auto* focused = window_->activeFocusItem();
        return focused ? focused->objectName() + QStringLiteral(" (") +
                             QString::fromLatin1(focused->metaObject()->className()) + u')'
                       : QStringLiteral("no active focus item");
    };
    QTRY_VERIFY2(item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus(),
                 qPrintable(focusDescription()));
    openGroupMenu(groupId);
    chooseGroupAction(QStringLiteral("groupMoveDownAction"));
    QTRY_COMPARE(controller_->groups().last().toMap().value(QStringLiteral("id")).toString(), groupId);
    // Moving closes the menu and queues focus restoration. Wait for it as for
    // move-up above before directing Shift+F10 at another group's button.
    QTRY_VERIFY(!window_->property("groupFocusPending").toBool());
    QTRY_VERIFY2(item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus(),
                 qPrintable(focusDescription()));
    QCOMPARE(tileGeometries(), geometry);

    openGroupMenu(QStringLiteral("work"));
    QVERIFY(!item(QStringLiteral("groupDeleteAction"))->isEnabled());
    QVERIFY(item(QStringLiteral("groupDeleteHint"))->property("text").toString().contains(
        QStringLiteral("移至其他分组")));
    QVERIFY(!item(QStringLiteral("groupMoveUpAction"))->isEnabled());
    QTest::keyClick(window_, Qt::Key_Escape);
    openGroupMenu(groupId);
    chooseGroupAction(QStringLiteral("groupDeleteAction"));
    QTRY_COMPARE(controller_->groups().size(), 3);
    QTRY_VERIFY(!item(QStringLiteral("groupActions-") + groupId));
    QTRY_VERIFY(item(QStringLiteral("groupActions-entertainment"))->hasActiveFocus());
    QCOMPARE(tileGeometries(), geometry);
    QVERIFY(controller_->editMode());
}

void PrototypeWindowTest::groupNameCompositionAndCancellation() {
    destroyWindow();
    createWindow({}, false);
    controller_->setEditMode(true);
    openGroupMenu(QStringLiteral("default"));
    QVERIFY(!item(QStringLiteral("groupDeleteAction"))->isEnabled());
    QVERIFY(item(QStringLiteral("groupDeleteHint"))->property("text").toString().contains(
        QStringLiteral("至少保留")));
    QTest::keyClick(window_, Qt::Key_Escape);
    click(item(QStringLiteral("newGroupButton")));
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("groupNameDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto* field = item(QStringLiteral("groupNameField"));
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    QInputMethodEvent preedit(QStringLiteral("fenzu"), {});
    QCoreApplication::sendEvent(field, &preedit);
    QTRY_VERIFY(field->property("inputMethodComposing").toBool());
    QVERIFY(!item(QStringLiteral("groupNameSaveButton"))->isEnabled());
    QTest::keyClick(window_, Qt::Key_Return);
    QCOMPARE(controller_->groups().size(), 1);
    QVERIFY(dialog->property("visible").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(controller_->editMode());
    QInputMethodEvent endComposition;
    QCoreApplication::sendEvent(field, &endComposition);
    QTRY_VERIFY(!field->property("inputMethodComposing").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(controller_->groups().size(), 1);
    QVERIFY(controller_->editMode());

    click(item(QStringLiteral("newGroupButton")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("  ")));
    QTest::keyClick(window_, Qt::Key_Return);
    QVERIFY(dialog->property("visible").toBool());
    QTRY_VERIFY(item(QStringLiteral("groupNameError"))->isVisible());
    QCOMPARE(controller_->groups().size(), 1);
    QVERIFY(field->setProperty("text", QString(QChar(0x0085)) + QStringLiteral("不接受控制字符")));
    QTest::keyClick(window_, Qt::Key_Return);
    QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(controller_->groups().size(), 1);
    QVERIFY(field->setProperty("text", QStringLiteral("未保存的分组")));
    QTest::keyClick(window_, Qt::Key_Tab);
    QTRY_VERIFY(item(QStringLiteral("groupNameCancelButton"))->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(controller_->groups().size(), 1);

    click(item(QStringLiteral("newGroupButton")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("失焦不提交")));
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(controller_->groups().size(), 1);
    QVERIFY(field->property("text").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    QTRY_VERIFY2(
        item(QStringLiteral("searchField"))->hasActiveFocus(),
        qPrintable(QStringLiteral("focus=%1 suspended=%2 popup=%3 pending=%4 staged=%5")
                       .arg(window_->activeFocusItem() ? window_->activeFocusItem()->objectName()
                                                       : QStringLiteral("none"))
                       .arg(window_->property("presentationSuspended").toBool())
                       .arg(window_->property("internalPopupVisible").toBool())
                       .arg(window_->property("presentationFocusPending").toBool())
                       .arg(window_->property("presentationStaged").toBool())));
    QVERIFY(controller_->editMode());

    while (controller_->groups().size() < controller_->maximumGroups()) {
        QVERIFY(!controller_->createGroup(QStringLiteral("同名分组")).isEmpty());
    }
    QTRY_VERIFY(!item(QStringLiteral("newGroupButton"))->isEnabled());
    QCOMPARE(controller_->groups().size(), 32);
}

void PrototypeWindowTest::groupCatalogMoveAndRestart() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow(path, false, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    controller_->setEditMode(true);
    click(item(QStringLiteral("newGroupButton")));
    submitGroupName(QStringLiteral("工具 Tools"));
    const auto newId = controller_->groups().last().toMap().value(QStringLiteral("id")).toString();
    const auto applicationId = state->applications.first().id;
    auto* applicationRow = item(QStringLiteral("applicationLaunch-") + applicationId);
    QVERIFY(applicationRow);
    QTest::mouseClick(
        window_, Qt::RightButton, Qt::NoModifier,
        applicationRow->mapToScene(applicationRow->boundingRect().center()).toPoint());
    auto* pinMenu = window_->findChild<QObject*>(QStringLiteral("pinApplicationMenu"));
    QVERIFY(pinMenu);
    QTRY_VERIFY(pinMenu->property("opened").toBool());
    auto* pinToNewGroup = item(QStringLiteral("applicationPinAction"));
    QVERIFY(pinToNewGroup && pinToNewGroup->isEnabled());
    QCOMPARE(pinToNewGroup->property("text").toString(), QStringLiteral("固定为磁贴"));
    pinToNewGroup->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(tileGeometries().size(), 1);
    const auto tileId = service_->previewGroups().last().tiles.first().id;
    QCOMPARE(tileData(tileId).value(QStringLiteral("groupId")).toString(), newId);

    beginMouseDrag(tileId);
    moveMouseToGrid(QStringLiteral("default"), QPointF(52, 52));
    QVERIFY(window_->property("dragValid").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                       gridPosition(QStringLiteral("default"), QPointF(52, 52)));
    QTRY_COMPARE(tileData(tileId).value(QStringLiteral("groupId")).toString(),
                 QStringLiteral("default"));
    openGroupMenu(newId);
    chooseGroupAction(QStringLiteral("groupDeleteAction"));
    QTRY_COMPARE(controller_->groups().size(), 1);
    openGroupMenu(QStringLiteral("default"));
    chooseGroupAction(QStringLiteral("groupRenameAction"));
    submitGroupName(QStringLiteral("日常应用 Daily"));
    click(item(QStringLiteral("newGroupButton")));
    submitGroupName(QStringLiteral("学习与资料 Learning"));
    const auto archiveId = controller_->groups().last().toMap().value(QStringLiteral("id")).toString();
    openGroupMenu(archiveId);
    chooseGroupAction(QStringLiteral("groupMoveUpAction"));
    QTRY_COMPARE(controller_->groups().first().toMap().value(QStringLiteral("id")).toString(),
                 archiveId);
    const auto beforeRestart = controller_->groups();
    const auto savedTile = tileData(tileId);
    QCOMPARE(savedTile.value(QStringLiteral("targetId")).toString(), applicationId);
    QCOMPARE(savedTile.value(QStringLiteral("column")).toInt(), 0);
    QCOMPARE(savedTile.value(QStringLiteral("row")).toInt(), 0);

    const auto artifactDirectory = qEnvironmentVariable("XDSTARTER_GROUP_VALIDATION_DIR");
    if (!artifactDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(artifactDirectory));
        QJsonArray frames;
        for (const bool dark : {true, false}) {
            controller_->setDarkTheme(dark);
            item(QStringLiteral("moreMenuButton"))->forceActiveFocus(Qt::TabFocusReason);
            QTest::mouseMove(window_, QPoint(1, 1));
            window_->requestUpdate();
            QTest::qWait(80);
            const auto image = window_->grabWindow();
            QVERIFY(!image.isNull());
            const auto name = dark ? QStringLiteral("groups-dark.png")
                                   : QStringLiteral("groups-light.png");
            QVERIFY(image.save(QDir(artifactDirectory).filePath(name)));
            frames.append(QJsonObject{{QStringLiteral("file"), name},
                                      {QStringLiteral("widthPx"), image.width()},
                                      {QStringLiteral("heightPx"), image.height()},
                                      {QStringLiteral("devicePixelRatio"), window_->devicePixelRatio()}});
        }
        const QJsonObject evidence{
            {QStringLiteral("fixtureOnly"), true},
            {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
            {QStringLiteral("platformName"), QGuiApplication::platformName()},
            {QStringLiteral("groupCount"), controller_->groups().size()},
            {QStringLiteral("tileCount"), tileGeometries().size()},
            {QStringLiteral("frames"), frames}};
        QFile output(QDir(artifactDirectory).filePath(QStringLiteral("groups-render.json")));
        QVERIFY2(output.open(QIODevice::WriteOnly), qPrintable(output.errorString()));
        QVERIFY(output.write(QJsonDocument(evidence).toJson()) > 0);
    }

    QVERIFY(controller_->flushSave());
    const auto savedTheme = controller_->darkTheme();
    destroyWindow();
    createWindow(path, false, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QTRY_COMPARE(controller_->groups(), beforeRestart);
    QCOMPARE(controller_->darkTheme(), savedTheme);
    QVERIFY(controller_->searchQuery().isEmpty());
    QVERIFY(controller_->editMode());
    QCOMPARE(tileData(tileId), savedTile);
    QCOMPARE(state->launches.load(), 0);
    destroyWindow();
}

void PrototypeWindowTest::groupChangesPreserveSearchFocus() {
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, false, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    controller_->setEditMode(true);
    const auto groupId = controller_->createGroup(QStringLiteral("另一组"));
    QVERIFY(!groupId.isEmpty());
    QVERIFY(controller_->pinApplication(state->applications.first().id, QStringLiteral("default")));
    QTRY_COMPARE(tileGeometries().size(), 1);
    const auto pinnedTileId = service_->previewGroups().first().tiles.first().id;
    QTRY_VERIFY(tileData(pinnedTileId).value(QStringLiteral("available")).toBool());
    openGroupMenu(groupId);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus());
    QSignalSpy availabilityChanged(catalog_.get(), &xdstarter::ApplicationCatalog::availabilityChanged);
    QSignalSpy groupsChanged(controller_.get(), &xdstarter::MenuController::groupsChanged);
    const auto scansBefore = state->scans.load();
    state->includeApplications = false;
    controller_->refreshApplications();
    QTRY_VERIFY(state->scans.load() > scansBefore);
    QTRY_VERIFY(!controller_->catalogBusy());
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 0);
    QTRY_VERIFY(availabilityChanged.count() > 0);
    QTRY_VERIFY(groupsChanged.count() > 0);
    QTRY_VERIFY(!tileData(pinnedTileId).value(QStringLiteral("available")).toBool());
    QTRY_VERIFY(item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus());
    const auto availabilityEvents = availabilityChanged.count();
    state->includeApplications = true;
    controller_->refreshApplications();
    QTRY_VERIFY(!controller_->catalogBusy());
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QTRY_VERIFY(availabilityChanged.count() > availabilityEvents);
    QTRY_VERIFY(tileData(pinnedTileId).value(QStringLiteral("available")).toBool());
    QTRY_VERIFY(item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus());
    auto* themeButton = item(QStringLiteral("moreMenuButton"));
    themeButton->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(controller_->renameGroup(groupId, QStringLiteral("后台更新")));
    QCoreApplication::processEvents();
    QVERIFY(themeButton->hasActiveFocus());
    auto* launchButton = item(QStringLiteral("applicationLaunch-") + state->applications.first().id);
    QVERIFY(launchButton);
    launchButton->forceActiveFocus(Qt::TabFocusReason);
    QVERIFY(controller_->moveGroupBy(groupId, -1));
    QCoreApplication::processEvents();
    QVERIFY(launchButton->hasActiveFocus());
    controller_->setSearchQuery(QStringLiteral("测试"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 2);
    auto* field = item(QStringLiteral("searchField"));
    field->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(field->hasActiveFocus());
    const auto before = controller_->searchResults();
    QSignalSpy resultsChanged(controller_.get(), &xdstarter::MenuController::searchResultsChanged);
    bool becameBusy = false;
    QObject busyObserver;
    connect(controller_.get(), &xdstarter::MenuController::searchChanged, &busyObserver,
            [this, &becameBusy] { becameBusy = becameBusy || controller_->searchBusy(); });
    QVERIFY(controller_->renameGroup(groupId, QStringLiteral("已改名")));
    QVERIFY(controller_->moveGroupBy(groupId, 1));
    QVERIFY(controller_->removeGroup(groupId));
    QCoreApplication::processEvents();
    QVERIFY(!becameBusy);
    QCOMPARE(resultsChanged.count(), 0);
    QCOMPARE(controller_->searchResults(), before);
    QVERIFY(field->hasActiveFocus());
    QCOMPARE(controller_->searchQuery(), QStringLiteral("测试"));
}

void PrototypeWindowTest::applicationLayoutRestoresUnavailableTarget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("applications-layout.json"));
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow(path, false, state);
    pinFirstApplication(state);
    QCOMPARE(tileGeometries().size(), 1);
    const auto tileId = service_->previewGroups().first().tiles.first().id;
    QVERIFY(controller_->moveTile(tileId, QStringLiteral("default"), 2, 2));
    QVERIFY(controller_->resizeTile(tileId, QStringLiteral("wide")));
    controller_->setDarkTheme(false);
    QVERIFY(controller_->flushSave());
    QVERIFY(QFile::exists(path));
    destroyWindow();

    state->includeApplications = false;
    createWindow(path, false, state);
    QCOMPARE(tileGeometries().size(), 1);
    QCOMPARE(tileData(tileId).value(QStringLiteral("name")).toString(),
             state->applications.first().name);
    QCOMPARE(tileData(tileId).value(QStringLiteral("column")).toInt(), 2);
    QCOMPARE(tileData(tileId).value(QStringLiteral("row")).toInt(), 2);
    QCOMPARE(tileData(tileId).value(QStringLiteral("size")).toString(), QStringLiteral("wide"));
    QVERIFY(!controller_->darkTheme());
    QTRY_VERIFY(!controller_->catalogBusy());
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 0);
    QTRY_VERIFY(!tileData(tileId).value(QStringLiteral("available")).toBool());
    // Pending availability is also false; wait for the worker's terminal result before reading UI.
    QTRY_VERIFY(tileData(tileId).value(QStringLiteral("unavailableReason")).toString().contains(
        QStringLiteral("测试目标已移除")));
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    auto* marker = visualItem(tile, QStringLiteral("unavailableMarker"));
    QVERIFY(marker);
    QTRY_VERIFY(marker->isVisible());
    auto* tooltip = tile->findChild<QObject*>(QStringLiteral("tileToolTip"));
    QVERIFY(tooltip);
    QTRY_VERIFY(tooltip->property("text").toString().contains(QStringLiteral("测试目标已移除")));
    QSignalSpy failed(controller_.get(), &xdstarter::MenuController::launchFailed);
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(window_->isVisible());
    QCOMPARE(tileGeometries().size(), 1);
    openSizeMenu(tileId, false);
    chooseSize(QStringLiteral("unpinTileItem"));
    QTRY_COMPARE(tileGeometries().size(), 0);
    QVERIFY(controller_->flushSave());
    destroyWindow();
    createWindow(path, false, state);
    QCOMPARE(tileGeometries().size(), 0);
    QVERIFY(!controller_->darkTheme());
    QVERIFY(state->workerOnly);
    destroyWindow();
}

void PrototypeWindowTest::unrelatedApplicationIconPreservesPinnedTile() {
    const auto state = std::make_shared<CatalogTestState>();
    for (int index = 0; index < 40; ++index) {
        state->applications.append(catalogTarget(
            QStringLiteral("M %1").arg(index, 2, 10, QLatin1Char('0')),
            QStringLiteral("C:/Fixture/M%1.lnk").arg(index)));
    }
    const auto unrelated = catalogTarget(QStringLiteral("Z 暂未显示的应用"),
                                         QStringLiteral("C:/Fixture/Z.lnk"));
    state->applications.append(unrelated);
    destroyWindow();
    createWindow({}, false, state);
    pinFirstApplication(state);
    QCOMPARE(tileGeometries().size(), 1);
    const auto applicationId = state->applications.first().id;
    QTRY_VERIFY(!catalog_->cachedIcon(applicationId).isNull());
    QVERIFY(catalog_->cachedIcon(unrelated.id).isNull());
    const auto tileId = service_->previewGroups().first().tiles.first().id;
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    auto* icon = visualItem(tile, QStringLiteral("tileIcon"));
    QVERIFY(icon);
    QTRY_COMPARE(icon->property("status").toInt(), 1);
    const auto before = icon->property("source").toUrl();
    QSignalSpy ready(catalog_.get(), &xdstarter::ApplicationCatalog::iconReady);
    catalog_->requestIcon(unrelated.id);
    const auto unrelatedReady = [&ready, &unrelated] {
        return std::any_of(ready.cbegin(), ready.cend(), [&unrelated](const QList<QVariant>& event) {
            return event.first().toString() == unrelated.id;
        });
    };
    QTRY_VERIFY(unrelatedReady());
    QVERIFY(!catalog_->cachedIcon(unrelated.id).isNull());
    QCOMPARE(item(QStringLiteral("tile-") + tileId), tile);
    QCOMPARE(icon->property("source").toUrl(), before);
    QVERIFY(!controller_->iconVersions().contains(unrelated.id));
    QCOMPARE(controller_->iconVersions().size(), 1);
    QVERIFY(controller_->unpinTile(tileId));
    QCOMPARE(tileGeometries().size(), 0);
    QVERIFY(controller_->iconVersions().isEmpty());
    QVERIFY(state->workerOnly);
}

void PrototypeWindowTest::shortcutRailAssetsAndTheme() {
    auto* rail = item(QStringLiteral("shortcutRail"));
    QVERIFY(rail);
    QCOMPARE(rail->width(), qreal(64));
    const auto geometry = tileGeometries();
    auto* avatar = item(QStringLiteral("shortcutAvatar"));
    QVERIFY(avatar);
    QCOMPARE(avatar->size(), QSizeF(32, 32));
    QVERIFY(!item(QStringLiteral("brandMark")));
    const QImage mark(QStringLiteral(":/xdstarter/app/xdstarter-mark.svg"));
    QVERIFY(!mark.isNull());
    QVERIFY(mark.hasAlphaChannel());

    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        const auto theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
        QQuickItem* previousShortcut = nullptr;
        for (const auto* action : {"account", "documents", "downloads", "settings", "power"}) {
            const auto id = QLatin1String(action);
            auto* button = item(QStringLiteral("shortcut-") + id);
            QVERIFY(button);
            QCOMPARE(button->size(), QSizeF(48, 48));
            if (previousShortcut)
                QCOMPARE(button->y(), previousShortcut->y() + previousShortcut->height());
            previousShortcut = id == QLatin1String("account") ? nullptr : button;
            auto* accessible = QAccessible::queryAccessibleInterface(button);
            QVERIFY(accessible);
            QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
            const bool isAvatar = id == QLatin1String("account");
            auto* icon = isAvatar ? avatar : item(QStringLiteral("shortcutIcon-") + id);
            QVERIFY(icon);
            QCOMPARE(icon->size(), isAvatar ? QSizeF(32, 32) : QSizeF(24, 24));
            const auto expected = QStringLiteral("qrc:/xdstarter/ui/") + theme + u'/' +
                                  (isAvatar ? QStringLiteral("avatar") : QString(id)) +
                                  QStringLiteral(".svg");
            QTRY_COMPARE(icon->property("source").toUrl(), QUrl(expected));
            QTRY_COMPARE(icon->property("status").toInt(), 1);
        }
        // Decode the exact bundled SVG, including its mask. A radius alone on an
        // enclosing Rectangle would leave the avatar image's corners opaque.
        const QImage avatarImage(QStringLiteral(":/xdstarter/ui/") + theme +
                                 QStringLiteral("/avatar.svg"));
        QVERIFY(!avatarImage.isNull());
        QVERIFY(avatarImage.hasAlphaChannel());
        const auto lastX = avatarImage.width() - 1;
        const auto lastY = avatarImage.height() - 1;
        QCOMPARE(avatarImage.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(avatarImage.pixelColor(lastX, 0).alpha(), 0);
        QCOMPARE(avatarImage.pixelColor(0, lastY).alpha(), 0);
        QCOMPARE(avatarImage.pixelColor(lastX, lastY).alpha(), 0);
        QVERIFY(avatarImage.pixelColor(avatarImage.width() / 2,
                                       avatarImage.height() / 2).alpha() > 0);
        QCOMPARE(tileGeometries(), geometry);
    }
}

void PrototypeWindowTest::desktopShortcutActions() {
    const auto state = std::make_shared<DesktopTestState>();
    destroyWindow();
    createWindow({}, true, {}, state);
    QSignalSpy failed(controller_.get(), &xdstarter::MenuController::desktopActionFailed);
    QSignalSpy succeeded(controller_.get(), &xdstarter::MenuController::desktopActionSucceeded);
    const QStringList expected{QStringLiteral("documents"), QStringLiteral("downloads"),
                               QStringLiteral("settings"), QStringLiteral("account")};
    for (qsizetype index = 0; index < expected.size(); ++index) {
        auto* button = item(QStringLiteral("shortcut-") + expected[index]);
        QVERIFY(button);
        if (index % 2 == 0) {
            click(button);
        } else {
            button->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(button->hasActiveFocus());
            QTest::keyClick(window_, Qt::Key_Return);
        }
        QTRY_COMPARE(failed.count(), index + 1);
        QTRY_VERIFY(!controller_->desktopActionBusy());
        QVERIFY(window_->isVisible());
        QVERIFY(item(QStringLiteral("desktopActionStatus"))->property("text")
                    .toString().contains(QStringLiteral("测试入口不可用")));
    }
    {
        QMutexLocker lock(&state->mutex);
        QCOMPARE(state->requestedActions, expected);
    }
    QCOMPARE(succeeded.count(), 0);
    state->succeeds = true;
    click(item(QStringLiteral("shortcut-documents")));
    QTRY_COMPARE(succeeded.count(), 1);
    QTRY_VERIFY(!window_->isVisible());
    QVERIFY(state->workerOnly);
}

void PrototypeWindowTest::auxiliaryMenusRespectEscape() {
    auto* power = item(QStringLiteral("shortcut-power"));
    auto* menu = window_->findChild<QObject*>(QStringLiteral("powerMenu"));
    auto* about = window_->findChild<QObject*>(QStringLiteral("aboutPopup"));
    QVERIFY(power);
    QVERIFY(menu);
    QVERIFY(about);
    QSignalSpy exiting(controller_.get(), &xdstarter::MenuController::exitRequested);
    controller_->setEditMode(true);
    click(power);
    QTRY_VERIFY(menu->property("opened").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(window_->isVisible());
    QVERIFY(controller_->editMode());
    QCOMPARE(exiting.count(), 0);

    click(item(QStringLiteral("aboutButton")));
    QTRY_VERIFY(about->property("opened").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!about->property("visible").toBool());
    QVERIFY(window_->isVisible());
    QVERIFY(controller_->editMode());
    controller_->setEditMode(false);

    power->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto* exitItem = item(QStringLiteral("exitApplicationItem"));
    QVERIFY(exitItem);
    exitItem->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(exitItem->hasActiveFocus());
    QTest::keyClick(exitItem->window(), Qt::Key_Return);
    QTRY_COMPARE(exiting.count(), 1);
    // The host owns process shutdown. Tests observe intent without quitting the runner.
    QVERIFY(window_->isVisible());
}

void PrototypeWindowTest::startButtonCaptureControl() {
    auto* menu = window_->findChild<QObject*>(QStringLiteral("powerMenu"));
    QVERIFY(menu);
    QSignalSpy requested(window_, SIGNAL(startButtonCaptureToggleRequested()));
    QVERIFY(requested.isValid());
    click(item(QStringLiteral("shortcut-power")));
    QTRY_VERIFY(menu->property("opened").toBool());
    auto* capture = item(QStringLiteral("startButtonCaptureItem"));
    QVERIFY(capture);
    QVERIFY(!capture->isEnabled());
    QCOMPARE(requested.count(), 0);
    window_->setProperty("startButtonCaptureAvailable", true);
    QTRY_VERIFY(capture->isEnabled());
    capture->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(requested.count(), 1);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(!window_->property("startButtonCaptureActive").toBool());
    window_->setProperty("startButtonCaptureActive", true);
    click(item(QStringLiteral("shortcut-power")));
    QTRY_VERIFY(menu->property("opened").toBool());
    QVERIFY(capture->property("text").toString().contains(QStringLiteral("暂停")));
    const auto directory = qEnvironmentVariable("XDSTARTER_START_CAPTURE_VALIDATION_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        for (const bool dark : {true, false}) {
            controller_->setDarkTheme(dark);
            QTest::qWait(220);
            const auto file =
                dark ? QStringLiteral("power-dark.png") : QStringLiteral("power-light.png");
            QVERIFY(window_->grabWindow().save(QDir(directory).filePath(file)));
        }
    }
    capture->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Enter);
    QTRY_COMPARE(requested.count(), 2);
    QTRY_VERIFY(!menu->property("visible").toBool());
}

void PrototypeWindowTest::winKeyCaptureControlRequiresExplicitIntent() {
    const auto artifactDirectory = qEnvironmentVariable("XDSTARTER_WIN_KEY_VALIDATION_DIR");
    if (!artifactDirectory.isEmpty()) {
        // These optional screenshots are complete native frames of the sample fixture.
        // They do not instantiate input capture or enumerate the user's applications.
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
        QVERIFY(QDir().mkpath(artifactDirectory));
        auto* screen = window_->screen();
        QVERIFY(screen);
        const auto available = screen->availableGeometry();
        const int shadow = qRound(window_->property("shadowInset").toReal());
        const int width =
            std::min(980 + 2 * shadow, std::max(480 + 2 * shadow, available.width() - 64));
        const int height =
            std::min(680 + 2 * shadow, std::max(480 + 2 * shadow, available.height() - 96));
        window_->resize(width, height);
        window_->setPosition(available.center() - QPoint(width / 2, height / 2));
        QTRY_COMPARE(window_->size(), QSize(width, height));
        // Resizing updates QML geometry on the next frame, after the size property changes.
        QSignalSpy placed(window_, &QQuickWindow::frameSwapped);
        window_->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window_));
        window_->requestUpdate();
        QTRY_VERIFY(!placed.isEmpty());
    }
    auto* menu = window_->findChild<QObject*>(QStringLiteral("powerMenu"));
    QVERIFY(menu);
    QSignalSpy requested(window_, SIGNAL(winKeyCaptureToggleRequested()));
    QVERIFY(requested.isValid());
    click(item(QStringLiteral("shortcut-power")));
    QTRY_VERIFY(menu->property("opened").toBool());
    auto* captureItem = item(QStringLiteral("winKeyCaptureItem"));
    QVERIFY(captureItem);
    QVERIFY(!captureItem->isEnabled());
    QCOMPARE(requested.count(), 0);
    QVERIFY(window_->setProperty("winKeyCaptureAvailable", true));
    QTRY_VERIFY(captureItem->isEnabled());
    QVERIFY(captureItem->property("text").toString().contains(QStringLiteral("启用")));
    if (!artifactDirectory.isEmpty()) {
        QSignalSpy rendered(window_, &QQuickWindow::frameSwapped);
        window_->requestUpdate();
        QTRY_VERIFY(!rendered.isEmpty());
        const auto image = window_->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(image.size(), QSize(qRound(window_->width() * window_->devicePixelRatio()),
                                     qRound(window_->height() * window_->devicePixelRatio())));
        QVERIFY(image.save(QDir(artifactDirectory).filePath(
            QStringLiteral("win-key-enable-sample-native.png"))));
    }
    captureItem->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(requested.count(), 1);
    QTRY_VERIFY(!menu->property("visible").toBool());
    // Emitting intent never claims that native installation has succeeded.
    QVERIFY(!window_->property("winKeyCaptureActive").toBool());
    QVERIFY(window_->setProperty("winKeyCaptureBusy", true));
    click(item(QStringLiteral("shortcut-power")));
    QTRY_VERIFY(menu->property("opened").toBool());
    QTRY_VERIFY(!captureItem->isEnabled());
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(window_->setProperty("winKeyCaptureBusy", false));
    QVERIFY(window_->setProperty("winKeyCaptureActive", true));
    click(item(QStringLiteral("shortcut-power")));
    QTRY_VERIFY(menu->property("opened").toBool());
    QTRY_VERIFY(captureItem->property("text").toString().contains(QStringLiteral("暂停")));
    if (!artifactDirectory.isEmpty()) {
        QSignalSpy rendered(window_, &QQuickWindow::frameSwapped);
        window_->requestUpdate();
        QTRY_VERIFY(!rendered.isEmpty());
        const auto image = window_->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(image.size(), QSize(qRound(window_->width() * window_->devicePixelRatio()),
                                     qRound(window_->height() * window_->devicePixelRatio())));
        QVERIFY(image.save(QDir(artifactDirectory).filePath(
            QStringLiteral("win-key-pause-sample-native.png"))));
    }
    click(captureItem);
    QTRY_COMPARE(requested.count(), 2);
    QVERIFY(window_->isVisible());
}

void PrototypeWindowTest::framelessWindowDismissalIntent() {
    QVERIFY(window_->flags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(window_->flags().testFlag(Qt::NoDropShadowWindowHint));
    QCOMPARE(window_->flags() & Qt::WindowType_Mask, Qt::WindowFlags(Qt::Tool));
    QVERIFY(window_->flags().testFlag(Qt::WindowStaysOnTopHint));
    QCOMPARE(window_->color().alpha(), 0);
    auto* surface = item(QStringLiteral("menuSurface"));
    QVERIFY(surface);
    QCOMPARE(surface->property("radius").toReal(),
             visualTokens_.value(QStringLiteral("cornerRadiusDip")).toMap()
                 .value(QStringLiteral("menu")).toReal());
    auto* closeButton = item(QStringLiteral("closeMenuButton"));
    QVERIFY(closeButton);
    auto* accessible = QAccessible::queryAccessibleInterface(closeButton);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("收起菜单"));

    // A desktop host owns recovery policy; a standalone smoke window still closes itself.
    QVERIFY(window_->setProperty("dismissalManaged", true));
    QSignalSpy dismissRequested(window_, SIGNAL(dismissRequested()));
    QVERIFY(dismissRequested.isValid());
    click(closeButton);
    QTRY_COMPARE(dismissRequested.count(), 1);
    QVERIFY(window_->isVisible());
    closeButton->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(dismissRequested.count(), 2);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_COMPARE(dismissRequested.count(), 3);
    QVERIFY(window_->isVisible());
    QVERIFY(window_->setProperty("dismissalManaged", false));
    click(closeButton);
    QTRY_VERIFY(!window_->isVisible());
}

void PrototypeWindowTest::customChromePixels() {
    const auto chromeTokens = visualTokens_.value(QStringLiteral("menuChrome")).toMap();
    const auto shadow = chromeTokens.value(QStringLiteral("shadowInsetDip")).toReal();
    const auto radius = visualTokens_.value(QStringLiteral("cornerRadiusDip")).toMap()
                            .value(QStringLiteral("menu")).toReal();
    const auto borderWidth = chromeTokens.value(QStringLiteral("borderWidthDip")).toReal();
    QCOMPARE(window_->property("shadowInset").toReal(), shadow);
    QCOMPARE(window_->property("screenGapDip").toReal(), qreal(8));
    auto* surface = item(QStringLiteral("menuSurface"));
    auto* panel = item(QStringLiteral("menuPanel"));
    auto* rail = item(QStringLiteral("shortcutRail"));
    QVERIFY(surface);
    QVERIFY(panel);
    QVERIFY(rail);
    QCOMPARE(panel->position(), QPointF(shadow, shadow));
    QCOMPARE(panel->size(), QSizeF(980, 680));
    QCOMPARE(window_->minimumSize(), QSize(qRound(480 + 2 * shadow), qRound(480 + 2 * shadow)));
    QCOMPARE(rail->property("color").value<QColor>().alpha(), 0);
    const auto panelScene = panel->mapRectToScene(panel->boundingRect());
    for (const auto* name :
         {"shortcut-account", "shortcut-documents", "shortcut-downloads", "shortcut-settings",
          "shortcut-power", "moreMenuButton", "closeMenuButton"}) {
        auto* control = item(QLatin1String(name));
        QVERIFY(control);
        const auto bounds = control->mapRectToScene(control->boundingRect());
        QVERIFY2(panelScene.contains(bounds), name);
        for (const auto& corner : {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(),
                                   bounds.bottomRight()}) {
            const auto x = std::clamp(corner.x(), panelScene.left() + radius,
                                      panelScene.right() - radius);
            const auto y = std::clamp(corner.y(), panelScene.top() + radius,
                                      panelScene.bottom() - radius);
            QVERIFY2(std::pow(corner.x() - x, 2) + std::pow(corner.y() - y, 2) <=
                         radius * radius + 0.01,
                     name);
        }
    }

    const auto closeColor = [](const QColor& actual, const QColor& expected) {
        return std::abs(actual.red() - expected.red()) <= 2 &&
               std::abs(actual.green() - expected.green()) <= 2 &&
               std::abs(actual.blue() - expected.blue()) <= 2 && actual.alpha() == 255;
    };
    int expectedNearShadowAlpha = -1;
    int expectedFarShadowAlpha = -1;
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        const auto theme = visualTokens_.value(QStringLiteral("themes")).toMap()
                               .value(dark ? QStringLiteral("dark") : QStringLiteral("light"))
                               .toMap();
        const QColor base(theme.value(QStringLiteral("opaqueFallback")).toString());
        const QColor border(theme.value(QStringLiteral("menuBorder")).toString());
        QVERIFY(border.isValid());
        QTRY_COMPARE(surface->property("color").value<QColor>(), base);
        QTRY_COMPARE(surface->property("borderColor").value<QColor>(), border);
        QSignalSpy painted(surface, SIGNAL(painted()));
        QVERIFY(painted.isValid());
        QVERIFY(QMetaObject::invokeMethod(surface, "requestPaint"));
        QTRY_VERIFY(painted.count() > 0);
        const auto captureChrome = [&](QObject* canvas) {
            auto image = window_->grabWindow();
            if (!image.hasAlphaChannel() &&
                QGuiApplication::platformName() == QStringLiteral("offscreen") &&
                window_->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
                // Offscreen software flattens the window to RGB. Retain original
                // Canvas alpha; native QPA must use the complete window capture.
                QString dataUrl;
                if (!QMetaObject::invokeMethod(canvas, "toDataURL", Q_RETURN_ARG(QString, dataUrl),
                                               Q_ARG(QString, QStringLiteral("image/png")))) {
                    return QImage{};
                }
                const auto prefix = QStringLiteral("data:image/png;base64,");
                if (!dataUrl.startsWith(prefix)) {
                    return QImage{};
                }
                const auto png = QByteArray::fromBase64(dataUrl.mid(prefix.size()).toLatin1());
                image = QImage::fromData(png, "PNG");
                qInfo() << "Chrome alpha capture: Canvas toDataURL original paint PNG"
                           " (offscreen software RGB backing-store limitation)";
            } else {
                qInfo() << "Chrome alpha capture: complete QQuickWindow grabWindow";
            }
            return image;
        };
        const auto image = captureChrome(surface);
        QVERIFY(!image.isNull());
        QVERIFY(image.hasAlphaChannel());
        const auto pixel = [&](qreal x, qreal y) {
            return image.pixelColor(std::clamp(qFloor(x * image.width() / window_->width()),
                                               0, image.width() - 1),
                                     std::clamp(qFloor(y * image.height() / window_->height()),
                                                0, image.height() - 1));
        };
        for (const QPoint& corner : {QPoint(0, 0), QPoint(image.width() - 1, 0),
                                    QPoint(0, image.height() - 1),
                                    QPoint(image.width() - 1, image.height() - 1)}) {
            QCOMPARE(image.pixelColor(corner).alpha(), 0);
        }
        const qreal right = window_->width() - shadow;
        const qreal bottom = window_->height() - shadow;
        // All four corners stay translucent, including the two touched by the rail.
        // Opaque rail Rectangle corners would fail the left pair of these checks.
        for (const QPointF& corner : {QPointF(shadow + 0.5, shadow + 0.5),
                                     QPointF(right - 0.5, shadow + 0.5),
                                     QPointF(shadow + 0.5, bottom - 0.5),
                                     QPointF(right - 0.5, bottom - 0.5)}) {
            QVERIFY(pixel(corner.x(), corner.y()).alpha() < 128);
        }
        const auto nearShadow = pixel(shadow - 2, window_->height() / 2).alpha();
        const auto farShadow = pixel(shadow - 16, window_->height() / 2).alpha();
        QVERIFY(nearShadow > farShadow);
        QVERIFY(farShadow > 0);
        QVERIFY(nearShadow < 128);
        if (expectedNearShadowAlpha < 0) {
            expectedNearShadowAlpha = nearShadow;
            expectedFarShadowAlpha = farShadow;
        } else {
            QCOMPARE(nearShadow, expectedNearShadowAlpha);
            QCOMPARE(farShadow, expectedFarShadowAlpha);
        }
        QCOMPARE(pixel(0, window_->height() / 2).alpha(), 0);
        // Sample the straight edges of the rendered outline in both themes,
        // independently of the panel and rail fills beneath the opaque stroke.
        for (const QPointF& edge : {QPointF(shadow + 100, shadow + 0.5),
                                   QPointF(shadow + 100, bottom - 0.5),
                                   QPointF(shadow + 0.5, window_->height() / 2),
                                   QPointF(right - 0.5, window_->height() / 2)}) {
            QVERIFY2(closeColor(pixel(edge.x(), edge.y()), border),
                     qPrintable(QStringLiteral("Border at (%1, %2): %3, expected %4")
                                    .arg(edge.x()).arg(edge.y())
                                    .arg(pixel(edge.x(), edge.y()).name(QColor::HexArgb),
                                         border.name(QColor::HexArgb))));
        }
        QVERIFY(closeColor(pixel(shadow + 100, shadow + 12), base));
        const auto railColor = surface->property("railColor").value<QColor>();
        QVERIFY(closeColor(pixel(shadow + 3, shadow + radius + 4), railColor));
        QVERIFY(closeColor(pixel(shadow + 3, bottom - radius - 4), railColor));
        // The single inset border has finished within 2 DIP; no second native-like
        // line is painted on the inside of the common panel/rail silhouette.
        QVERIFY(closeColor(pixel(shadow + 2, window_->height() / 2), railColor));

        // Changing only the interior colors must not tint the outside contour.
        // Compare with a solid border-colored reference using the same component;
        // the sampled pixel footprints stay wholly outside the inner silhouette.
        QQmlComponent referenceComponent(engine_.get());
        referenceComponent.loadFromModule("XDStarter", "MenuChrome");
        QVERIFY2(referenceComponent.isReady(), qPrintable(referenceComponent.errorString()));
        std::unique_ptr<QObject> reference(referenceComponent.createWithInitialProperties({
            {QStringLiteral("width"), window_->width()},
            {QStringLiteral("height"), window_->height()},
            {QStringLiteral("color"), border}, {QStringLiteral("railColor"), border},
            {QStringLiteral("borderColor"), border}, {QStringLiteral("radius"), radius},
            {QStringLiteral("shadowInset"), shadow},
            {QStringLiteral("borderWidth"), borderWidth}}));
        auto* referenceItem = qobject_cast<QQuickItem*>(reference.get());
        QVERIFY(referenceItem);
        referenceItem->setParentItem(window_->contentItem());
        referenceItem->setZ(10000);
        surface->setVisible(false);
        QSignalSpy referencePainted(reference.get(), SIGNAL(painted()));
        QVERIFY(QMetaObject::invokeMethod(reference.get(), "requestPaint"));
        QTRY_VERIFY(referencePainted.count() > 0);
        const auto referenceImage = captureChrome(reference.get());
        surface->setVisible(true);
        reference.reset();
        QVERIFY(!referenceImage.isNull());
        QCOMPARE(referenceImage.size(), image.size());
        const qreal scaleX = qreal(image.width()) / window_->width();
        const qreal scaleY = qreal(image.height()) / window_->height();
        int sampleCount = 0;
        int maximumDifference = 0;
        for (const bool left : {true, false}) {
            for (const bool top : {true, false}) {
                const QPointF center(left ? shadow + radius : right - radius,
                                     top ? shadow + radius : bottom - radius);
                int cornerSamples = 0;
                for (int y = qFloor((center.y() - radius) * scaleY);
                     y < qCeil((center.y() + radius) * scaleY); ++y) {
                    for (int x = qFloor((center.x() - radius) * scaleX);
                         x < qCeil((center.x() + radius) * scaleX); ++x) {
                        const QRectF footprint(x / scaleX, y / scaleY, 1 / scaleX, 1 / scaleY);
                        const auto middle = footprint.center();
                        if ((left ? middle.x() >= center.x() : middle.x() <= center.x()) ||
                            (top ? middle.y() >= center.y() : middle.y() <= center.y())) {
                            continue;
                        }
                        const auto closestX = std::clamp(center.x(), footprint.left(), footprint.right());
                        const auto closestY = std::clamp(center.y(), footprint.top(), footprint.bottom());
                        const auto nearest = std::hypot(closestX - center.x(), closestY - center.y());
                        if (nearest < radius - borderWidth + 0.15 / scaleX ||
                            std::hypot(middle.x() - center.x(), middle.y() - center.y()) >
                                radius + 0.4 / scaleX || image.pixelColor(x, y).alpha() < 32) {
                            continue;
                        }
                        const auto actual = image.pixelColor(x, y);
                        const auto expected = referenceImage.pixelColor(x, y);
                        const auto difference = std::max({std::abs(actual.red() - expected.red()),
                            std::abs(actual.green() - expected.green()),
                            std::abs(actual.blue() - expected.blue()),
                            std::abs(actual.alpha() - expected.alpha())});
                        maximumDifference = std::max(maximumDifference, difference);
                        QVERIFY2(difference <= 3, qPrintable(QStringLiteral(
                            "Interior tinted corner (%1,%2): %3 vs solid outline %4, delta %5")
                            .arg(x).arg(y).arg(actual.name(QColor::HexArgb), expected.name(QColor::HexArgb))
                            .arg(difference)));
                        ++cornerSamples;
                    }
                }
                QVERIFY(cornerSamples > 0);
                sampleCount += cornerSamples;
            }
        }
        qInfo() << "Corner contour" << (dark ? "dark" : "light")
                << "samples" << sampleCount << "maximum channel difference" << maximumDifference;
        const auto artifactDirectory = qEnvironmentVariable("XDSTARTER_CHROME_VALIDATION_DIR");
        if (!artifactDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(artifactDirectory));
            const auto themeName = dark ? QStringLiteral("dark") : QStringLiteral("light");
            QVERIFY(image.save(QDir(artifactDirectory).filePath(themeName + QStringLiteral(".png"))));
            QFile evidence(QDir(artifactDirectory).filePath(themeName + QStringLiteral(".json")));
            QVERIFY(evidence.open(QIODevice::WriteOnly));
            QVERIFY(evidence.write(QJsonDocument(QJsonObject{
                {QStringLiteral("theme"), themeName},
                {QStringLiteral("devicePixelRatio"), window_->devicePixelRatio()},
                {QStringLiteral("platform"), QGuiApplication::platformName()},
                {QStringLiteral("graphicsApi"), int(window_->rendererInterface()->graphicsApi())},
                {QStringLiteral("cornerSamples"), sampleCount},
                {QStringLiteral("maximumChannelDifference"), maximumDifference},
                {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()}
            }).toJson()) > 0);
        }
        QCOMPARE(item(QStringLiteral("tile-daily-small-1"))->size(), QSizeF(48, 48));
        QCOMPARE(item(QStringLiteral("tile-work-standard-1"))->size(), QSizeF(104, 104));
        QCOMPARE(item(QStringLiteral("tile-daily-wide"))->size(), QSizeF(216, 104));
    }
}

void PrototypeWindowTest::shadowMarginDismissal() {
    QVERIFY(window_->setProperty("dismissalManaged", true));
    QSignalSpy dismissRequested(window_, SIGNAL(dismissRequested()));
    QVERIFY(dismissRequested.isValid());
    const auto shadow = window_->property("shadowInset").toReal();
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                      QPoint(qRound(shadow / 2), window_->height() / 2));
    QTRY_COMPARE(dismissRequested.count(), 1);
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                      QPoint(qFloor(shadow + 0.5), qFloor(shadow + 0.5)));
    QTRY_COMPARE(dismissRequested.count(), 2);
    // Clicking blank panel space remains an internal click, while content actions
    // still receive their events above the transparent-margin dismissal area.
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                      QPoint(qRound(shadow + 70), qRound(shadow + 14)));
    QCOMPARE(dismissRequested.count(), 2);
    const auto before = controller_->darkTheme();
    click(item(QStringLiteral("themeButton")));
    QTRY_COMPARE(controller_->darkTheme(), !before);
    QCOMPARE(dismissRequested.count(), 2);
    QVERIFY(window_->isVisible());
}

void PrototypeWindowTest::internalPopupsGuardAndCleanup() {
    controller_->setEditMode(true);
    const auto before = controller_->groups();
    QVERIFY(!window_->property("dismissalBlocked").toBool());
    for (const auto* name : {"tileSizeMenu", "pinApplicationMenu", "powerMenu", "aboutPopup",
                             "groupMenu", "groupNameDialog", "tileNameDialog", "moreMenu"}) {
        auto* popup = window_->findChild<QObject*>(QLatin1String(name));
        QVERIFY(popup);
        // Popup.Item is deliberate: opening an internal menu must not create another
        // native top-level window and look like activation of an unrelated application.
        QCOMPARE(popup->property("popupType").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(window_->property("internalPopupVisible").toBool());
        QVERIFY(window_->property("dismissalBlocked").toBool());
        QVERIFY(window_->isVisible());
        QCOMPARE(controller_->groups(), before);
        QVERIFY(controller_->editMode());
        QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_VERIFY(!window_->property("internalPopupVisible").toBool());
        QVERIFY(!window_->property("dismissalBlocked").toBool());
        QVERIFY(window_->isVisible());
        QVERIFY(controller_->editMode());
        QCOMPARE(controller_->groups(), before);
        QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    }
}

void PrototypeWindowTest::dismissalCancelsOnlyDragPreview() {
    const auto tileId = QStringLiteral("work-standard-1");
    controller_->setEditMode(true);
    QVERIFY(controller_->moveTile(tileId, QStringLiteral("work"), 0, 2));
    QTRY_COMPARE(tileData(tileId).value(QStringLiteral("row")).toInt(), 2);
    const auto committed = controller_->groups();
    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::groupsChanged);
    beginMouseDrag(tileId);
    QVERIFY(window_->property("dismissalBlocked").toBool());
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QVERIFY(!window_->property("dismissalBlocked").toBool());
    QVERIFY(!item(QStringLiteral("dragPlacementPreview"))->isVisible());
    QCOMPARE(item(QStringLiteral("dragPlacementPreview"))->size(), QSizeF(0, 0));
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(controller_->groups(), committed);
    QVERIFY(controller_->editMode());

    window_->hide();
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    window_->requestActivate();
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    QTRY_VERIFY(item(QStringLiteral("searchField"))->hasActiveFocus());
    QCOMPARE(window_->property("focusedTileId").toString(), tileId);
    QVERIFY(!window_->property("internalPopupVisible").toBool());
    QVERIFY(!window_->property("dragActive").toBool());
    QVERIFY(controller_->editMode());
    QCOMPARE(controller_->groups(), committed);
}

void PrototypeWindowTest::dragOutsideReleaseClearsPreview() {
    controller_->setEditMode(true);
    const auto before = controller_->groups();
    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::groupsChanged);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    const QPoint outside(-32, -32);
    QTest::mouseMove(window_, outside, 20);
    QTRY_VERIFY(!window_->property("dragValid").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, outside);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QVERIFY(!window_->property("dismissalBlocked").toBool());
    QVERIFY(!item(QStringLiteral("dragPlacementPreview"))->isVisible());
    QCOMPARE(item(QStringLiteral("dragPlacementPreview"))->size(), QSizeF(0, 0));
    QVERIFY(window_->property("dragTargetGroupId").toString().isEmpty());
    QCOMPARE(changed.count(), 0);
    QCOMPARE(controller_->groups(), before);
    QVERIFY(controller_->editMode());
    QVERIFY(window_->isVisible());
}

void PrototypeWindowTest::renderedGroups() {
    const auto artifactDirectory = qEnvironmentVariable("XDSTARTER_VALIDATION_DIR");
    if (!artifactDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(artifactDirectory));
    }

    // Keep the entire real window on the current screen, including at 200% scaling.
    auto* screen = window_->screen();
    QVERIFY(screen);
    const auto available = screen->availableGeometry();
    const int shadow = qRound(window_->property("shadowInset").toReal());
    const int width =
        std::min(980 + 2 * shadow, std::max(480 + 2 * shadow, available.width() - 64));
    const int height =
        std::min(680 + 2 * shadow, std::max(480 + 2 * shadow, available.height() - 96));
    window_->resize(width, height);
    window_->setPosition(available.center() - QPoint(width / 2, height / 2));
    QTRY_COMPARE(window_->size(), QSize(width, height));
    QTRY_VERIFY(!window_->property("layoutWidthPending").toBool());
    QCOMPARE(controller_->gridColumns(), 10);
    auto* flickable = item(QStringLiteral("groupsFlickable"));
    auto* themeButton = item(QStringLiteral("moreMenuButton"));
    QVERIFY(flickable);

    // A Windows hardware cursor can retain hover despite synthetic QTest mouse moves.
    // Suppress tooltip painting only in this fresh capture fixture, which cleanup destroys.
    // smallTilesExposeNames separately verifies the production tooltip visibility and text.
    for (const auto& groupValue : controller_->groups()) {
        for (const auto& tileValue : groupValue.toMap().value(QStringLiteral("tiles")).toList()) {
            auto* tile = item(QStringLiteral("tile-") +
                              tileValue.toMap().value(QStringLiteral("id")).toString());
            QVERIFY(tile);
            auto* tooltip = tile->findChild<QObject*>(QStringLiteral("tileToolTip"));
            QVERIFY(tooltip);
            QVERIFY(tooltip->setProperty("opacity", 0.0));
        }
    }

    const auto geometry = tileGeometries();
    const auto artifactPath = [&artifactDirectory](const QString& file) {
        return QDir(artifactDirectory).filePath(file);
    };
    for (const bool dark : {true, false}) {
        window_->setProperty("darkTheme", dark);
        const auto theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
        const auto background = item(QStringLiteral("menuSurface"))->property("color").value<QColor>();
        int twoStandardSpanPx = 0;
        int fourSmallSpanPx = 0;
        for (const auto& groupValue : controller_->groups()) {
            const auto group = groupValue.toMap();
            const auto groupId = group.value(QStringLiteral("id")).toString();
            const auto tiles = group.value(QStringLiteral("tiles")).toList();
            const auto firstId = tiles.first().toMap().value(QStringLiteral("id")).toString();
            auto* first = item(QStringLiteral("tile-") + firstId);
            auto* grid = item(QStringLiteral("grid-") + groupId);
            QVERIFY(first);
            QVERIFY(grid);
            qreal occupiedHeight = 0;
            for (const auto& tileValue : tiles) {
                auto* tile = item(QStringLiteral("tile-") +
                                  tileValue.toMap().value(QStringLiteral("id")).toString());
                QVERIFY(tile);
                occupiedHeight = std::max(occupiedHeight, tile->y() + tile->height());
            }
            const QRectF contentBounds(0, 0, grid->width(), occupiedHeight);
            first->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(first->hasActiveFocus());
            QTRY_VERIFY(flickable->boundingRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(
                grid->mapRectToItem(flickable, contentBounds)));
            themeButton->forceActiveFocus(Qt::TabFocusReason);
            QTest::mouseMove(window_, QPoint(1, 1));
            window_->requestUpdate();
            QTest::qWait(80);
            const auto image = window_->grabWindow();
            QVERIFY2(!image.isNull(), "The real QQuickWindow frame could not be captured");
            const qreal imageScaleX = qreal(image.width()) / window_->width();
            const qreal imageScaleY = qreal(image.height()) / window_->height();
            QVERIFY(std::abs(imageScaleX - window_->devicePixelRatio()) <= 0.01);
            QVERIFY(std::abs(imageScaleY - window_->devicePixelRatio()) <= 0.01);
            // Empty editing rows are drop space; pixel geometry is checked on occupied content.
            const auto gridScene = grid->mapRectToScene(contentBounds);
            const int left = qRound(gridScene.left() * imageScaleX);
            const int top = qRound(gridScene.top() * imageScaleY);
            const QRect gridPixels(left, top,
                                   qRound(gridScene.right() * imageScaleX) - left,
                                   qRound(gridScene.bottom() * imageScaleY) - top);
            QVERIFY(image.rect().contains(gridPixels));

            // Inspect the captured pixels, rather than serializing model values as evidence.
            // The 8/64 DIP rows avoid the 28/36 DIP icons and bottom labels;
            // 64 is also 8 DIP into the second small-tile row.
            const auto topRuns = scanTilePixels(
                image, gridPixels, qRound((gridScene.top() + 8) * imageScaleY), background);
            const auto lowerRuns = scanTilePixels(
                image, gridPixels, qRound((gridScene.top() + 64) * imageScaleY), background);
            const auto verticalRuns = scanTilePixelsVertically(
                image, gridPixels, qRound((gridScene.left() + 8) * imageScaleX), background);
            QVector<PixelRun> smallVerticalRuns;
            if (groupId == QLatin1String("daily")) {
                // Scan beyond the rounded corner and left of the centered symbol.
                // Light glyph antialiasing can match the light window background.
                smallVerticalRuns = scanTilePixelsVertically(
                    image, gridPixels, qRound((gridScene.left() + 456) * imageScaleX), background);
            }
            QVector<int> expectedWidths;
            if (groupId == QLatin1String("work")) {
                expectedWidths = {104, 104, 104, 216};
            } else if (groupId == QLatin1String("daily")) {
                expectedWidths = {216, 104, 104, 48, 48};
            } else {
                expectedWidths = {216, 104, 104, 104};
            }
            const bool topPassed = runsMatch(topRuns, expectedWidths, imageScaleX);
            const bool lowerPassed = runsMatch(lowerRuns, expectedWidths, imageScaleX);
            const bool verticalPassed = runsMatch(verticalRuns, {104}, imageScaleY);
            bool compoundPassed = true;
            QJsonObject compoundChecks;
            if (groupId == QLatin1String("work") && topPassed) {
                twoStandardSpanPx = topRuns[1].start + topRuns[1].width - topRuns[0].start;
                compoundChecks.insert(QStringLiteral("twoStandardOuterWidthPx"), twoStandardSpanPx);
            }
            if (groupId == QLatin1String("daily")) {
                const bool smallVerticalPassed = runsMatch(smallVerticalRuns, {48, 48}, imageScaleY);
                compoundPassed = topPassed && verticalPassed && smallVerticalPassed;
                if (compoundPassed) {
                    fourSmallSpanPx = topRuns[4].start + topRuns[4].width - topRuns[3].start;
                    const int fourSmallHeightPx = smallVerticalRuns[1].start + smallVerticalRuns[1].width;
                    const bool wideEqualsTwoStandard = std::abs(topRuns[0].width - twoStandardSpanPx) <= 1;
                    const bool smallEqualsStandard = std::abs(fourSmallSpanPx - topRuns[1].width) <= 1 &&
                        std::abs(fourSmallHeightPx - verticalRuns[0].width) <= 1;
                    compoundChecks = {{QStringLiteral("fourSmallOuterWidthPx"), fourSmallSpanPx},
                                      {QStringLiteral("fourSmallOuterHeightPx"), fourSmallHeightPx},
                                      {QStringLiteral("standardWidthPx"), topRuns[1].width},
                                      {QStringLiteral("standardHeightPx"), verticalRuns[0].width},
                                      {QStringLiteral("wideWidthPx"), topRuns[0].width},
                                      {QStringLiteral("twoStandardOuterWidthPx"), twoStandardSpanPx},
                                      {QStringLiteral("wideEqualsTwoStandard"), wideEqualsTwoStandard},
                                      {QStringLiteral("fourSmallEqualsStandard"), smallEqualsStandard}};
                    compoundPassed = wideEqualsTwoStandard && smallEqualsStandard;
                }
            } else if (groupId == QLatin1String("entertainment") && topPassed) {
                compoundPassed = std::abs(topRuns[3].width - fourSmallSpanPx) <= 1;
                compoundChecks = {{QStringLiteral("standardWidthPx"), topRuns[3].width},
                                  {QStringLiteral("fourSmallOuterWidthPx"), fourSmallSpanPx},
                                  {QStringLiteral("fourSmallEqualsStandard"), compoundPassed}};
            }

            QJsonArray tileRecords;
            for (const auto& tileValue : tiles) {
                const auto data = tileValue.toMap();
                auto* tile = item(QStringLiteral("tile-") + data.value(QStringLiteral("id")).toString());
                QVERIFY(tile);
                auto* label = visualItem(tile, QStringLiteral("tileLabel"));
                QJsonObject record{{QStringLiteral("id"), data.value(QStringLiteral("id")).toString()},
                                   {QStringLiteral("renderedGeometryDip"), rectangleJson(itemGeometry(tile))},
                                   {QStringLiteral("labelVisible"), label && label->isVisible()},
                                   {QStringLiteral("labelTruncated"), label && label->property("truncated").toBool()}};
                if (auto* accessible = QAccessible::queryAccessibleInterface(tile)) {
                    record.insert(QStringLiteral("accessibleName"), accessible->text(QAccessible::Name));
                }
                tileRecords.append(record);
            }
            const auto api = window_->rendererInterface()->graphicsApi();
            QJsonObject evidence{
                {QStringLiteral("theme"), theme},
                {QStringLiteral("group"), groupId},
                {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
                {QStringLiteral("compiler"), compilerName()},
                {QStringLiteral("kernelType"), QSysInfo::kernelType()},
                {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
                {QStringLiteral("operatingSystem"), QSysInfo::prettyProductName()},
                {QStringLiteral("platformName"), QGuiApplication::platformName()},
                {QStringLiteral("graphicsApi"), graphicsApiName(api)},
                {QStringLiteral("graphicsApiValue"), static_cast<int>(api)},
                {QStringLiteral("requestedScaleFactor"), qEnvironmentVariable("QT_SCALE_FACTOR")},
                {QStringLiteral("windowDevicePixelRatio"), window_->devicePixelRatio()},
                {QStringLiteral("imageWidthRatio"), imageScaleX},
                {QStringLiteral("imageHeightRatio"), imageScaleY},
                {QStringLiteral("windowGeometryDip"), rectangleJson(window_->geometry())},
                {QStringLiteral("windowFrameGeometryDip"), rectangleJson(window_->frameGeometry())},
                {QStringLiteral("screenGeometryDip"), rectangleJson(screen->geometry())},
                {QStringLiteral("screenAvailableGeometryDip"), rectangleJson(available)},
                {QStringLiteral("screenDevicePixelRatio"), screen->devicePixelRatio()},
                {QStringLiteral("screenLogicalDpi"), screen->logicalDotsPerInch()},
                {QStringLiteral("screenPhysicalDpi"), screen->physicalDotsPerInch()},
                {QStringLiteral("imageWidthPx"), image.width()},
                {QStringLiteral("imageHeightPx"), image.height()},
                {QStringLiteral("gridGeometryWindowDip"), rectangleJson(gridScene)},
                {QStringLiteral("dropAreaGeometryWindowDip"),
                 rectangleJson(grid->mapRectToScene(grid->boundingRect()))},
                {QStringLiteral("gridCropPx"), rectangleJson(gridPixels)},
                {QStringLiteral("topScanlineRunsPx"), runsJson(topRuns)},
                {QStringLiteral("lowerScanlineRunsPx"), runsJson(lowerRuns)},
                {QStringLiteral("verticalScanlineRunsPx"), runsJson(verticalRuns)},
                {QStringLiteral("smallVerticalScanlineRunsPx"), runsJson(smallVerticalRuns)},
                {QStringLiteral("compoundPixelChecks"), compoundChecks},
                {QStringLiteral("pixelScanPassed"), topPassed && lowerPassed && verticalPassed && compoundPassed},
                {QStringLiteral("tooltipCaptureOpacity"), 0.0},
                {QStringLiteral("tooltipsHiddenForGeometryCapture"), true},
                {QStringLiteral("tiles"), tileRecords}};
            if (!artifactDirectory.isEmpty()) {
                const auto prefix = theme + u'-' + groupId;
                const auto fullFile = prefix + QStringLiteral("-window.png");
                const auto cropFile = prefix + QStringLiteral("-grid.png");
                QVERIFY(image.save(artifactPath(fullFile)));
                QVERIFY(image.copy(gridPixels).save(artifactPath(cropFile)));
                evidence.insert(QStringLiteral("windowScreenshot"), fullFile);
                evidence.insert(QStringLiteral("gridScreenshot"), cropFile);
                QFile output(artifactPath(prefix + QStringLiteral(".json")));
                QVERIFY2(output.open(QIODevice::WriteOnly), qPrintable(output.errorString()));
                QVERIFY(output.write(QJsonDocument(evidence).toJson()) > 0);
            }
            QVERIFY2(topPassed,
                     qPrintable(theme + u'/' + groupId + QStringLiteral(" top scan: ") +
                                QString::fromUtf8(QJsonDocument(runsJson(topRuns)).toJson(QJsonDocument::Compact))));
            QVERIFY2(lowerPassed,
                     qPrintable(theme + u'/' + groupId + QStringLiteral(" lower scan: ") +
                                QString::fromUtf8(QJsonDocument(runsJson(lowerRuns)).toJson(QJsonDocument::Compact))));
            QVERIFY2(verticalPassed, qPrintable(theme + u'/' + groupId + QStringLiteral(" vertical pixels")));
            QVERIFY2(compoundPassed, qPrintable(theme + u'/' + groupId + QStringLiteral(" compound pixel geometry")));
            QCOMPARE(tileGeometries(), geometry);
        }
    }
}

void PrototypeWindowTest::highFidelityLayoutAndActions() {
    const auto layout = visualTokens_.value(QStringLiteral("layout")).toMap();
    auto* panel = item(QStringLiteral("menuPanel"));
    auto* toolbar = item(QStringLiteral("searchToolbar"));
    auto* field = item(QStringLiteral("searchField"));
    auto* catalog = item(QStringLiteral("catalogPanel"));
    auto* firstGrid = item(QStringLiteral("grid-work"));
    QVERIFY(panel && toolbar && field && catalog && firstGrid);
    QVERIFY(!window_->property("compactLayout").toBool());
    QVERIFY(!item(QStringLiteral("brandMark")));
    QVERIFY(!item(QStringLiteral("feedbackPanel"))->isVisible());
    QCOMPARE(field->height(), layout.value(QStringLiteral("searchHeight")).toReal());
    const auto panelOrigin = panel->mapToScene(QPointF{});
    const auto searchOrigin = field->mapToScene(QPointF{});
    QCOMPARE(searchOrigin - panelOrigin, QPointF(88, 24));
    QCOMPARE(catalog->width(), qreal(240));
    QCOMPARE(firstGrid->mapToScene(QPointF{}) - panelOrigin, QPointF(352, 128));
    const auto initialGeometry = tileGeometries();
    const auto initialGroups = controller_->groups();
    for (const auto* name : {"moreMenuButton", "closeMenuButton"}) {
        auto* button = item(QLatin1String(name));
        QVERIFY(button && button->isVisible());
        QCOMPARE(button->size(), QSizeF(36, 36));
        QVERIFY(toolbar->boundingRect().contains(button->mapRectToItem(toolbar, button->boundingRect())));
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        QVERIFY(accessible && !accessible->text(QAccessible::Name).isEmpty());
    }
    openMoreMenu();
    QVERIFY(window_->property("internalPopupVisible").toBool());
    for (const auto* name : {"themeButton", "saveLayoutButton", "aboutButton"}) {
        auto* action = item(QLatin1String(name));
        QVERIFY(action && action->isVisible());
    }
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->property("internalPopupVisible").toBool());
    QVERIFY(window_->isVisible());
    auto* first = item(QStringLiteral("tile-work-standard-1"));
    const auto identityColor = first->property("baseColor").value<QColor>();
    QVERIFY(identityColor.isValid());
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        const auto theme = visualTokens_.value(QStringLiteral("themes")).toMap()
                               .value(dark ? QStringLiteral("dark") : QStringLiteral("light")).toMap();
        QVERIFY(contrastRatio(QColor(theme.value(QStringLiteral("errorForeground")).toString()),
                              QColor(theme.value(QStringLiteral("surface")).toString())) >= 4.5);
        QCOMPARE(tileGeometries(), initialGeometry);
        QCOMPARE(controller_->groups(), initialGroups);
        for (const auto& group : controller_->groups()) {
            for (const auto& value : group.toMap().value(QStringLiteral("tiles")).toList()) {
                auto* tile = item(QStringLiteral("tile-") + value.toMap().value(QStringLiteral("id")).toString());
                QVERIFY(tile);
                const auto base = tile->property("baseColor").value<QColor>();
                auto* label = visualItem(tile, QStringLiteral("tileLabel"));
                QVERIFY(label);
                QVERIFY(contrastRatio(label->property("color").value<QColor>(), base) >= 4.5);
            }
        }
        QCOMPARE(first->property("baseColor").value<QColor>(), identityColor);
    }
    controller_->setEditMode(true);
    QTRY_COMPARE(firstGrid->height(), qreal(104));
    QCOMPARE(tileGeometries(), initialGeometry);
    controller_->setEditMode(false);
    QTRY_COMPARE(firstGrid->height(), qreal(104));
    QCOMPARE(tileGeometries(), initialGeometry);
}

void PrototypeWindowTest::feedbackFailuresRemainVisible() {
    const auto state = std::make_shared<CatalogTestState>();
    const auto desktop = std::make_shared<DesktopTestState>();
    destroyWindow();
    createWindow({}, true, state, desktop);
    QTRY_VERIFY(!controller_->catalogBusy());
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QVERIFY(!controller_->catalogError());
    state->scanSucceeds = false;
    controller_->refreshApplications();
    QTRY_VERIFY(!controller_->catalogBusy());
    QVERIFY(controller_->catalogError());
    QCOMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QVERIFY(item(QStringLiteral("catalogStatus"))->isVisible());
    state->scanSucceeds = true;
    state->scanWarns = true;
    controller_->refreshApplications();
    QTRY_VERIFY(!controller_->catalogBusy());
    QVERIFY(controller_->catalogError());
    QVERIFY(item(QStringLiteral("catalogStatus"))->isVisible());
    state->scanWarns = false;
    controller_->refreshApplications();
    QTRY_VERIFY(!controller_->catalogBusy());
    QVERIFY(!controller_->catalogError());
    QTRY_VERIFY(!item(QStringLiteral("catalogStatus"))->isVisible());

    const auto before = controller_->groups();
    QVERIFY(
        !controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), -1, 0));
    QVERIFY(controller_->operationError());
    QCOMPARE(controller_->groups(), before);
    auto* operationTimer = window_->findChild<QObject*>(QStringLiteral("operationFeedbackTimer"));
    QVERIFY(operationTimer);
    QVERIFY(QMetaObject::invokeMethod(operationTimer, "stop"));
    QTRY_VERIFY(item(QStringLiteral("operationStatus"))->isVisible());
    controller_->setEditMode(true);
    QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 0, 2));
    QVERIFY(!controller_->operationError());
    QVERIFY(QMetaObject::invokeMethod(operationTimer, "stop"));
    QTRY_VERIFY(!item(QStringLiteral("operationStatus"))->isVisible());

    QVERIFY(controller_->requestDesktopAction(QStringLiteral("documents")));
    QTRY_VERIFY(!controller_->desktopActionBusy());
    QTRY_VERIFY(controller_->desktopActionError());
    QVERIFY(controller_->operationError());
    auto* desktopTimer = window_->findChild<QObject*>(QStringLiteral("desktopFeedbackTimer"));
    QVERIFY(desktopTimer);
    QVERIFY(QMetaObject::invokeMethod(desktopTimer, "stop"));
    QTRY_VERIFY(item(QStringLiteral("desktopActionStatus"))->isVisible());
    desktop->succeeds = true;
    QVERIFY(controller_->requestDesktopAction(QStringLiteral("documents")));
    QTRY_VERIFY(!controller_->desktopActionBusy());
    QTRY_VERIFY(!controller_->desktopActionError());
    QVERIFY(!controller_->operationError());
    QVERIFY(state->workerOnly && desktop->workerOnly);
}

void PrototypeWindowTest::tileNamesAreLiteralText() {
    auto* tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(tile);
    auto data = tile->property("tile").toMap();
    const auto literal = QStringLiteral("<b>工具 · Tools</b>");
    data.insert(QStringLiteral("name"), literal);
    data.insert(QStringLiteral("symbol"), QStringLiteral("<b>X</b>"));
    QVERIFY(tile->setProperty("tile", data));
    auto* label = visualItem(tile, QStringLiteral("tileLabel"));
    auto* tooltip = tile->findChild<QObject*>(QStringLiteral("tileToolTip"));
    QVERIFY(label && tooltip);
    QCOMPARE(label->property("text").toString(), literal);
    QCOMPARE(label->property("textFormat").toInt(), int(Qt::PlainText));
    auto* symbol = visualItem(tile, QStringLiteral("tileSymbol"));
    QVERIFY(symbol);
    QCOMPARE(symbol->property("text").toString(), QStringLiteral("<b>X</b>"));
    QCOMPARE(symbol->property("textFormat").toInt(), int(Qt::PlainText));
    const auto symbolBounds = symbol->mapRectToItem(tile, QRectF(QPointF{}, symbol->size()));
    QVERIFY2(QRectF(QPointF{}, tile->size()).contains(symbolBounds),
             qPrintable(QStringLiteral("Literal symbol bounds %1,%2 %3x%4 exceed tile %5x%6")
                            .arg(symbolBounds.x())
                            .arg(symbolBounds.y())
                            .arg(symbolBounds.width())
                            .arg(symbolBounds.height())
                            .arg(tile->width())
                            .arg(tile->height())));
    data.insert(QStringLiteral("symbol"), QString(16, QLatin1Char('W')));
    QVERIFY(tile->setProperty("tile", data));
    const auto longSymbolBounds = symbol->mapRectToItem(tile, QRectF(QPointF{}, symbol->size()));
    QVERIFY2(QRectF(QPointF{}, tile->size()).contains(longSymbolBounds),
             qPrintable(QStringLiteral("16-character symbol bounds %1,%2 %3x%4 exceed tile %5x%6")
                            .arg(longSymbolBounds.x())
                            .arg(longSymbolBounds.y())
                            .arg(longSymbolBounds.width())
                            .arg(longSymbolBounds.height())
                            .arg(tile->width())
                            .arg(tile->height())));
    const auto geometryBefore = tileGeometries();
    for (const auto& id : {QStringLiteral("daily-small-1"), QStringLiteral("work-wide-long")}) {
        auto* other = item(QStringLiteral("tile-") + id);
        QVERIFY(other);
        auto otherData = other->property("tile").toMap();
        otherData.insert(QStringLiteral("symbol"), QString(16, QLatin1Char('W')));
        QVERIFY(other->setProperty("tile", otherData));
        auto* otherSymbol = visualItem(other, QStringLiteral("tileSymbol"));
        QVERIFY(otherSymbol);
        QCOMPARE(otherSymbol->width(), other->property("iconSizeDip").toReal());
        QCOMPARE(otherSymbol->height(), other->property("iconSizeDip").toReal());
        const auto bounds =
            otherSymbol->mapRectToItem(other, QRectF(QPointF{}, otherSymbol->size()));
        QVERIFY(QRectF(QPointF{}, other->size()).contains(bounds));
        QCOMPARE(otherSymbol->property("textFormat").toInt(), int(Qt::PlainText));
    }
    QCOMPARE(tileGeometries(), geometryBefore);
    auto* tipText = tooltip->property("contentItem").value<QQuickItem*>();
    QVERIFY(tipText);
    QVERIFY(tipText->property("text").toString().contains(literal));
    QCOMPARE(tipText->property("textFormat").toInt(), int(Qt::PlainText));
    QTest::mouseMove(window_, QPoint(1, 1));
    item(QStringLiteral("moreMenuButton"))->forceActiveFocus(Qt::OtherFocusReason);
    for (int count = 0; count < 64 && !tile->hasActiveFocus(); ++count)
        QTest::keyClick(window_, Qt::Key_Tab);
    QTRY_VERIFY(tile->hasActiveFocus());
    QVERIFY(controller_->keyboardNavigation());
    QTRY_VERIFY_WITH_TIMEOUT(tooltip->property("visible").toBool(), 1500);
    QVERIFY(tooltip->property("width").toReal() <= 380);
}

void PrototypeWindowTest::tileDisplayNameEditingAndRestart() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("tile-names.json"));
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow(path, false, state);
    pinFirstApplication(state);
    const auto tileId = service_->previewGroups().first().tiles.first().id;
    const auto targetBefore = service_->targetForTile(tileId);
    const auto geometryBefore = tileGeometries();
    const auto colorBefore = item(QStringLiteral("tile-") + tileId)->property("baseColor");
    openSizeMenu(tileId, true);
    auto* rename = item(QStringLiteral("renameTileItem"));
    QVERIFY(rename && rename->isEnabled());
    rename->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("tileNameDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto* field = item(QStringLiteral("tileNameField"));
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    const auto name = QStringLiteral("<b>办公工具</b>");
    QVERIFY(field->setProperty("text", QStringLiteral("  ") + name + QStringLiteral("  ")));
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(item(QStringLiteral("tile-") + tileId)->hasActiveFocus());
    QCOMPARE(tileData(tileId).value(QStringLiteral("name")).toString(), name);
    QCOMPARE(tileData(tileId).value(QStringLiteral("displayName")).toString(), name);
    QCOMPARE(service_->targetForTile(tileId), targetBefore);
    QCOMPARE(tileGeometries(), geometryBefore);
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QCOMPARE(tile->property("baseColor"), colorBefore);
    auto* label = visualItem(tile, QStringLiteral("tileLabel"));
    QVERIFY(label);
    QCOMPARE(label->property("text").toString(), name);
    QCOMPARE(label->property("textFormat").toInt(), int(Qt::PlainText));
    auto* accessible = QAccessible::queryAccessibleInterface(tile);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), name);
    for (const auto& query : {state->applications.first().name, QStringLiteral("办公工具"),
                              QStringLiteral("bangonggongju"), QStringLiteral("bggj")}) {
        controller_->setSearchQuery(query);
        QTRY_VERIFY(!controller_->searchBusy());
        QTRY_COMPARE(controller_->searchResults().size(), 1);
        QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("appId")).toString(),
                 state->applications.first().id);
        QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("displayName")).toString(),
                 state->applications.first().name);
    }
    QSignalSpy unchanged(controller_.get(), &xdstarter::MenuController::groupsChanged);
    QVERIFY(controller_->renameTile(tileId, name));
    QCOMPARE(unchanged.count(), 0);
    controller_->setSearchQuery({});
    QVERIFY(controller_->flushSave());
    QFile savedFile(path);
    QVERIFY(savedFile.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(savedFile.readAll()).object();
    savedFile.close();
    destroyWindow();
    createWindow(path, false, state);
    QTRY_VERIFY(!controller_->catalogBusy());
    QCOMPARE(service_->snapshot(), saved);
    QCOMPARE(tileData(tileId).value(QStringLiteral("name")).toString(), name);
    QCOMPARE(service_->targetForTile(tileId), targetBefore);
    QVERIFY(controller_->searchQuery().isEmpty());
    QVERIFY(controller_->editMode());
    // Wait for the fresh restart's deferred presentation and scene update before
    // opening a menu; the presentation callback intentionally clears old popups.
    QSignalSpy presented(window_, &QQuickWindow::frameSwapped);
    window_->requestUpdate();
    QTRY_VERIFY(presented.count() > 0);
    openSizeMenu(tileId, true);
    auto* reset = item(QStringLiteral("resetTileNameItem"));
    QVERIFY(reset && reset->isEnabled());
    reset->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_COMPARE(tileData(tileId).value(QStringLiteral("name")).toString(), targetBefore.value(QStringLiteral("name")).toString());
    QTRY_VERIFY(item(QStringLiteral("tile-") + tileId)->hasActiveFocus());
    QVERIFY(tileData(tileId).value(QStringLiteral("displayName")).toString().isEmpty());
    QCOMPARE(service_->targetForTile(tileId), targetBefore);
    controller_->setSearchQuery(QStringLiteral("办公工具"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_VERIFY(controller_->searchResults().isEmpty());
    QVERIFY(controller_->flushSave());
    destroyWindow();
}

void PrototypeWindowTest::tileDisplayNameValidationAndCancellation() {
    const auto before = service_->snapshot();
    const auto tileId = QStringLiteral("work-standard-1");
    openSizeMenu(tileId, true);
    item(QStringLiteral("renameTileItem"))->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window_, Qt::Key_Return);
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("tileNameDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto* field = item(QStringLiteral("tileNameField"));
    QVERIFY(field);
    QVERIFY(field->setProperty("text", QString(129, QLatin1Char('x'))));
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(item(QStringLiteral("tileNameError"))->isVisible());
    QVERIFY(item(QStringLiteral("tileNameError"))->property("text").toString().contains(QStringLiteral("128")));
    QCOMPARE(service_->snapshot(), before);
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("  ")));
    QTest::keyClick(window_, Qt::Key_Return);
    QCOMPARE(service_->snapshot(), before);
    QVERIFY(field->setProperty("text", QString{}));
    QInputMethodEvent preedit(QStringLiteral("gaiming"), {});
    QCoreApplication::sendEvent(field, &preedit);
    QTRY_VERIFY(field->property("inputMethodComposing").toBool());
    QTest::keyClick(window_, Qt::Key_Return);
    QTest::keyClick(window_, Qt::Key_Escape);
    QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(service_->snapshot(), before);
    QInputMethodEvent cancelComposition;
    QCoreApplication::sendEvent(field, &cancelComposition);
    QTRY_VERIFY(!field->property("inputMethodComposing").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("未提交的显示名")));
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(item(QStringLiteral("tile-") + tileId)->hasActiveFocus());
    QCOMPARE(service_->snapshot(), before);
    openSizeMenu(tileId, true);
    click(item(QStringLiteral("renameTileItem")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("取消按钮草稿")));
    click(item(QStringLiteral("tileNameCancelButton")));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(item(QStringLiteral("tile-") + tileId)->hasActiveFocus());
    QCOMPARE(service_->snapshot(), before);
    openSizeMenu(tileId, true);
    click(item(QStringLiteral("renameTileItem")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("失焦丢弃草稿")));
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(service_->snapshot(), before);
    QVERIFY(field->property("text").toString().isEmpty());
}

void PrototypeWindowTest::tileDisplayNameSharedTargetAliases() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("shared-target.json"));
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, false, state);
    pinFirstApplication(state);
    auto snapshot = service_->snapshot();
    const auto targetsBefore = snapshot.value(QStringLiteral("targets"));
    auto groups = snapshot.value(QStringLiteral("groups")).toArray();
    auto group = groups[0].toObject();
    auto tiles = group.value(QStringLiteral("tiles")).toArray();
    auto duplicate = tiles[0].toObject();
    const auto firstId = duplicate.value(QStringLiteral("id")).toString();
    duplicate.insert(QStringLiteral("id"), QStringLiteral("second-shared-tile"));
    duplicate.insert(QStringLiteral("column"), 2);
    tiles.append(duplicate); group.insert(QStringLiteral("tiles"), tiles); groups[0] = group;
    snapshot.insert(QStringLiteral("groups"), groups);
    snapshot.insert(QStringLiteral("schemaVersion"), 2);
    snapshot.remove(QStringLiteral("gridColumns"));
    const auto bytes = QJsonDocument(snapshot).toJson();
    destroyWindow();
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();
    createWindow(path, false, state);
    QTRY_VERIFY(!controller_->catalogBusy());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes); file.close();
    QVERIFY(controller_->renameTile(firstId, QStringLiteral("入口甲")));
    QVERIFY(controller_->renameTile(QStringLiteral("second-shared-tile"), QStringLiteral("入口乙")));
    QCOMPARE(service_->snapshot().value(QStringLiteral("targets")), targetsBefore);
    for (const auto& query : {QStringLiteral("入口甲"), QStringLiteral("入口乙"), state->applications.first().name}) {
        controller_->setSearchQuery(query);
        QTRY_VERIFY(!controller_->searchBusy());
        QTRY_COMPARE(controller_->searchResults().size(), 1);
        QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("appId")).toString(),
                 state->applications.first().id);
    }
    state->applications[0].name = QStringLiteral("目录更新后的名字");
    controller_->refreshApplications();
    QTRY_VERIFY(!controller_->catalogBusy());
    controller_->setSearchQuery(QStringLiteral("A 测试快捷方式"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QCOMPARE(controller_->searchResults().first().toMap().value(QStringLiteral("displayName")).toString(), state->applications.first().name);
    QCOMPARE(tileData(firstId).value(QStringLiteral("name")).toString(), QStringLiteral("入口甲"));
    QVERIFY(controller_->resetTileDisplayName(firstId));
    controller_->setSearchQuery(QStringLiteral("入口甲"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_VERIFY(controller_->searchResults().isEmpty());
    controller_->setSearchQuery(QStringLiteral("入口乙"));
    QTRY_VERIFY(!controller_->searchBusy());
    QTRY_COMPARE(controller_->searchResults().size(), 1);
    QVERIFY(controller_->flushSave());
    QFile backup(path + QStringLiteral(".bak"));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), bytes);
    QCOMPARE(service_->snapshot().value(QStringLiteral("targets")), targetsBefore);
    destroyWindow();
}

void PrototypeWindowTest::layoutImportPreviewAndRestore() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current.json"));
    const auto source = directory.filePath(QStringLiteral("layout & preview.json"));
    xdstarter::PrototypeService sample(grid_);
    auto replacement = sample.snapshot();
    replacement.insert(QStringLiteral("preferences"),
                       QJsonObject{{QStringLiteral("theme"), QStringLiteral("light")}});
    QFile input(source);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QVERIFY(input.write(QJsonDocument(replacement).toJson()) > 0);
    input.close();
    destroyWindow();
    createWindow(path, false);
    const auto before = service_->snapshot();
    QVERIFY(controller_->layoutImportSupported());
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("layoutImportDialog"));
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(!controller_->importBusy());
    QCOMPARE(controller_->importPreview().value(QStringLiteral("groups")).toInt(), 3);
    QCOMPARE(controller_->importPreview().value(QStringLiteral("tiles")).toInt(), 15);
    QVERIFY(item(QStringLiteral("layoutImportCounts"))->isVisible());
    QCOMPARE(item(QStringLiteral("layoutImportSource"))->property("textFormat").toInt(), 0);
    QCOMPARE(item(QStringLiteral("layoutImportSource"))->property("text").toString(),
             QFileInfo(source).path());
    QCOMPARE(item(QStringLiteral("layoutImportFileName"))->property("text").toString(),
             QFileInfo(source).fileName());
    auto* sourceAccessible =
        QAccessible::queryAccessibleInterface(item(QStringLiteral("layoutImportFileName")));
    QVERIFY(sourceAccessible);
    QCOMPARE(sourceAccessible->text(QAccessible::Description), source);
    QCOMPARE(service_->snapshot(), before);
    // Confirm exactly the immutable preview even if the selected file changes afterward.
    QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
    input.write("invalid after preview");
    input.close();
    click(item(QStringLiteral("layoutImportConfirmButton")));
    QTRY_VERIFY(!controller_->importBusy());
    QCOMPARE(service_->snapshot(), replacement);
    QVERIFY(!controller_->darkTheme());
    QVERIFY(!dialog->property("visible").toBool());
    QVERIFY(controller_->flushSave());
    destroyWindow();
    createWindow(path, false);
    QCOMPARE(service_->snapshot(), replacement);
    // The default has no file to preserve until the first actual import write. Make a
    // legitimate current edit; restore uses its cached backup despite the flush changing it.
    controller_->setDarkTheme(true);
    QVERIFY(controller_->flushSave());
    QVERIFY(controller_->stageBackupRestore());
    QTRY_VERIFY(!controller_->importBusy());
    QVERIFY(!controller_->importPreview().isEmpty());
    QVERIFY(controller_->confirmLayoutImport());
    QTRY_VERIFY(!controller_->importBusy());
    QCOMPARE(service_->snapshot(), replacement);
    QVERIFY(!controller_->darkTheme());
}

void PrototypeWindowTest::layoutImportCancellationAndProtection() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("current.json"));
    const auto source = directory.filePath(QStringLiteral("source.json"));
    xdstarter::PrototypeService sample(grid_);
    QVERIFY(xdstarter::LayoutStore(source).save(sample.snapshot()).success);
    // Smoke/sample controllers reject at the C++ boundary even with a configured path.
    QVERIFY(!controller_->layoutImportSupported());
    QVERIFY(!controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    destroyWindow();
    createWindow(path, true);
    QVERIFY(!controller_->layoutImportSupported());
    QVERIFY(!controller_->stageBackupRestore());
    destroyWindow();
    createWindow(path, false);
    const auto before = service_->snapshot();
    QVERIFY(
        !controller_->stageLayoutImport(QUrl(QStringLiteral("https://example.test/layout.json"))));
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    controller_->cancelLayoutImport();
    QTRY_VERIFY(!controller_->importBusy());
    QVERIFY(controller_->importPreview().isEmpty());
    QVERIFY(!controller_->confirmLayoutImport());
    QCOMPARE(service_->snapshot(), before);
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    QVERIFY(!controller_->importPreview().isEmpty());
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("layoutImportDialog"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(controller_->importPreview().isEmpty());
    QVERIFY(window_->isVisible());
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    controller_->setDarkTheme(false); // A legitimate new revision invalidates the preview.
    QVERIFY(controller_->importPreview().isEmpty());
    QVERIFY(!controller_->confirmLayoutImport());
    QVERIFY(controller_->flushSave());
    // Future selected files are refused without making this current layout read-only.
    QFile input(source);
    QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
    input.write("{\"schemaVersion\":999}");
    input.close();
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    QVERIFY(!controller_->importError().isEmpty());
    QVERIFY(!controller_->readOnly());
    destroyWindow();
    QFile current(path);
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Truncate));
    current.write("{\"schemaVersion\":999}");
    current.close();
    createWindow(path, false);
    QVERIFY(controller_->readOnly());
    QVERIFY(!controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QVERIFY(!controller_->stageBackupRestore());
    QVERIFY(!controller_->confirmLayoutImport());
}

void PrototypeWindowTest::layoutImportFailureAndExitFence() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("current.json"));
    const auto source = directory.filePath(QStringLiteral("source.json"));
    xdstarter::PrototypeService sample(grid_);
    const auto replacement = sample.snapshot();
    QVERIFY(xdstarter::LayoutStore(source).save(replacement).success);
    destroyWindow();
    const auto catalogState = std::make_shared<CatalogTestState>();
    createWindow(path, false, catalogState);
    pinFirstApplication(catalogState);
    const auto tileId = controller_->tiles().first().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(controller_->renameTile(tileId, QStringLiteral("有效的先前名称")));
    controller_->setEditMode(true);
    QVERIFY(!controller_->createGroup(QStringLiteral("尚未保存的合法编辑")).isEmpty());
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    const auto before = service_->snapshot();
    // A competing lock refuses the transaction, with no replacement publication.
    QLockFile lock(path + QStringLiteral(".lock"));
    QVERIFY(lock.tryLock(0));
    QVERIFY(controller_->confirmLayoutImport());
    QVERIFY(!controller_->renameTile(tileId, QStringLiteral("被阻止")));
    QVERIFY(!controller_->resetTileDisplayName(tileId));
    QVERIFY(!controller_->moveTile(tileId, QStringLiteral("default"), 2, 0));
    QVERIFY(!controller_->resizeTile(tileId, QStringLiteral("small")));
    QVERIFY(!controller_->unpinTile(tileId));
    QVERIFY(controller_->createGroup(QStringLiteral("被阻止")).isEmpty());
    controller_->setDarkTheme(false);
    QVERIFY(controller_->darkTheme());
    QTRY_VERIFY(!controller_->importBusy());
    QCOMPARE(service_->snapshot(), before);
    QVERIFY(!controller_->importError().isEmpty());
    lock.unlock();
    // Queue an autosave, then an import, then immediately flush without pumping its
    // GUI completion. The worker fence must publish import before considering old memory.
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    controller_->requestSave();
    // Do not pump GUI events between the old save and confirmation: its completion
    // remains queued while the worker executes import and the shutdown fence.
    QVERIFY(controller_->confirmLayoutImport());
    QVERIFY(controller_->flushSave());
    QCOMPARE(service_->snapshot(), replacement);
    QCOMPARE(xdstarter::LayoutStore(path).load(before).snapshot, replacement);
    const auto successStatus = controller_->persistenceStatus();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    QVERIFY(!controller_->saving());
    QCOMPARE(controller_->persistenceStatus(), successStatus);
    QCOMPARE(service_->snapshot(), replacement);
    QCOMPARE(catalogState->launches.load(), 0);
    destroyWindow();
    createWindow(path, false);
    QCOMPARE(service_->snapshot(), replacement);
    // Detect a newer destination appearing between preview and confirmation.
    QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!controller_->importBusy());
    QFile current(path);
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray future("{\"schemaVersion\":999}");
    current.write(future);
    current.close();
    const auto currentMemory = service_->snapshot();
    QVERIFY(controller_->confirmLayoutImport());
    QTRY_VERIFY(!controller_->importBusy());
    QVERIFY(controller_->readOnly());
    QCOMPARE(service_->snapshot(), currentMemory);
    QVERIFY(current.open(QIODevice::ReadOnly));
    QCOMPARE(current.readAll(), future);
}

void PrototypeWindowTest::layoutImportFilePickerOwnership() {
    QTemporaryDir directory;
    destroyWindow();
    createWindow(directory.filePath(QStringLiteral("current.json")), false);
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        openMoreMenu();
        QVERIFY(item(QStringLiteral("importLayoutButton"))->isEnabled());
        click(item(QStringLiteral("importLayoutButton")));
        auto* picker = window_->findChild<QObject*>(QStringLiteral("layoutFileDialog"));
        QVERIFY(picker);
        QTRY_VERIFY(picker->property("visible").toBool());
        // ReadOnly (0x10) is the only requested option. Let Qt use the native
        // platform dialog instead of requiring any particular Quick implementation.
        QCOMPARE(picker->property("options").toInt(), 0x10);
        QCOMPARE(picker->property("parentWindow").value<QWindow*>(),
                 static_cast<QWindow*>(window_));
        QCOMPARE(picker->property("modality").toInt(), int(Qt::WindowModal));
        QVERIFY(window_->property("internalPopupVisible").toBool());
        QVERIFY(window_->property("dismissalBlocked").toBool());
        // The offscreen backend may use a fallback. Native HWND ownership and
        // focus handoffs require a separate Windows product-window acceptance.
        if (dark)
            QVERIFY(QMetaObject::invokeMethod(picker, "reject"));
        else
            QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
        QTRY_VERIFY(!picker->property("visible").toBool());
        QTRY_VERIFY(!window_->property("internalPopupVisible").toBool());
        QVERIFY(!window_->property("dismissalBlocked").toBool());
        QVERIFY(window_->isVisible());
    }
}

void PrototypeWindowTest::layoutImportDialogThemeAndKeyboard_data() {
    QTest::addColumn<bool>("completeDuringKeyClick");
    QTest::newRow("normal") << false;
    QTest::newRow("completion-during-key-click") << true;
}

void PrototypeWindowTest::layoutImportDialogThemeAndKeyboard() {
    QFETCH(bool, completeDuringKeyClick);
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("current.json"));
    const auto source = directory.filePath(QStringLiteral("source.json"));
    xdstarter::PrototypeService sample(grid_);
    auto replacement = sample.snapshot();
    destroyWindow();
    createWindow(path, false);
    if (qEnvironmentVariableIsSet("XDSTARTER_TEST_REQUIRE_D3D11")) {
        QCOMPARE(window_->rendererInterface()->graphicsApi(), QSGRendererInterface::Direct3D11);
    }
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        replacement.insert(QStringLiteral("preferences"),
                           QJsonObject{{QStringLiteral("theme"),
                                        dark ? QStringLiteral("dark") : QStringLiteral("light")}});
        QVERIFY(xdstarter::LayoutStore(source).save(replacement).success);
        QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(!controller_->importBusy());
        auto* dialog = window_->findChild<QObject*>(QStringLiteral("layoutImportDialog"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto* cancel = item(QStringLiteral("layoutImportCancelButton"));
        auto* confirm = item(QStringLiteral("layoutImportConfirmButton"));
        QVERIFY(cancel && confirm);
        QCOMPARE(confirm->height(), 36.0);
        cancel->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window_, Qt::Key_Tab);
        QTRY_VERIFY(confirm->hasActiveFocus());
        const auto background = confirm->property("background").value<QQuickItem*>();
        QVERIFY(background);
        QCOMPARE(background->property("radius").toReal(), 6.0);
        // Read the grouped border through QML's public object metadata.
        QQmlProperty focusBorder(background, QStringLiteral("border.width"));
        QCOMPARE(focusBorder.read().toReal(), 2.0);
        captureLayoutImportFrame(dark ? QStringLiteral("dark-import-preview")
                                      : QStringLiteral("light-import-preview"));
        cancel->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window_, Qt::Key_Return);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(controller_->importPreview().isEmpty());
        QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(!controller_->importBusy());
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        // Closing the parent cancels this preview and cannot later publish its read.
        QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
        QVERIFY(!dialog->property("visible").toBool());
        QVERIFY(controller_->importPreview().isEmpty());
        QTRY_VERIFY(!window_->property("internalPopupVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
        QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(!controller_->importBusy());
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        confirm->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(confirm->hasActiveFocus());
        const bool focusedBeforeReturn = confirm->hasActiveFocus();

        // Key delivery pumps events; a fast save may finish before keyClick()
        // returns. Observe the actual transition and UI state while it occurs.
        QObject observer;
        bool sawCommitting = false;
        bool sawDisabled = false;
        bool sawDimmed = false;
        bool completedBeforeReturn = false;
        const auto observe = [&] {
            if (controller_->importCommitting()) {
                sawCommitting = true;
                sawDisabled = sawDisabled || !confirm->isEnabled();
                sawDimmed = sawDimmed || qFuzzyCompare(confirm->opacity(), 0.45);
            }
        };
        QObject::connect(controller_.get(), &xdstarter::MenuController::importStateChanged,
                         &observer, observe);
        QObject::connect(confirm, &QQuickItem::enabledChanged, &observer, observe);
        QObject::connect(confirm, &QQuickItem::opacityChanged, &observer, observe);
        if (completeDuringKeyClick) {
            // Force the queued completion to run while QTest is still delivering
            // the key, reproducing the timing that made the old assertion flaky.
            QTimer::singleShot(0, &observer, [&] {
                if (!sawCommitting)
                    return;
                QEventLoop loop;
                QTimer timeout;
                timeout.setSingleShot(true);
                QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                QObject::connect(controller_.get(), &xdstarter::MenuController::importStateChanged,
                                 &loop, [&] {
                                     if (!controller_->importCommitting())
                                         loop.quit();
                                 });
                if (controller_->importCommitting()) {
                    timeout.start(5000);
                    loop.exec();
                }
                completedBeforeReturn = !controller_->importCommitting();
            });
        }
        QTest::keyClick(window_, Qt::Key_Return);
        const auto trace = QStringLiteral("dark=%1 focusBefore=%2 committing=%3 disabled=%4 dimmed=%5 "
                                          "completedBeforeReturn=%6")
                               .arg(dark)
                               .arg(focusedBeforeReturn)
                               .arg(sawCommitting)
                               .arg(sawDisabled)
                               .arg(sawDimmed)
                               .arg(completedBeforeReturn);
        qInfo().noquote() << "Layout import confirmation:" << trace;
        QVERIFY2(sawCommitting, qPrintable(trace));
        QVERIFY2(sawDisabled, qPrintable(trace));
        QVERIFY2(sawDimmed, qPrintable(trace));
        if (completeDuringKeyClick)
            QVERIFY2(completedBeforeReturn, qPrintable(trace));
        QTRY_VERIFY(!controller_->importBusy());
        QCOMPARE(service_->snapshot(), replacement);
        QCOMPARE(controller_->darkTheme(), dark);
    }
}

void PrototypeWindowTest::layoutImportMinimumPanel() {
    QTemporaryDir directory;
    const auto folder =
        directory.filePath(QString(40, QLatin1Char('a')) + u'/' + QString(40, QLatin1Char('b')) +
                           u'/' + QString(40, QLatin1Char('c')));
    QVERIFY(QDir().mkpath(folder));
    const auto source = QDir(folder).filePath(QStringLiteral("layout-preview.json"));
    xdstarter::PrototypeService sample(grid_);
    while (sample.previewGroups().size() < xdstarter::LayoutStore::maximumGroups)
        QVERIFY(sample.createGroup(QStringLiteral("额外分组")).success);
    QVERIFY(xdstarter::LayoutStore(source).save(sample.snapshot()).success);
    destroyWindow();
    createWindow(directory.filePath(QStringLiteral("current.json")), false);
    window_->resize(528, 528); // 480 DIP panel plus the shared 24 DIP shadow insets.
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(!controller_->importBusy());
        auto* dialog = window_->findChild<QObject*>(QStringLiteral("layoutImportDialog"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(controller_->importPreview().value(QStringLiteral("groups")).toInt(), 32);
        const auto checkButton = [this](const QString& name) {
            auto* button = item(name);
            if (!button)
                return false;
            const auto bounds = button->mapRectToScene(QRectF(QPointF{}, button->size()));
            return button->isVisible() && bounds.top() >= 24 &&
                   bounds.bottom() <= window_->height() - 24 && bounds.left() >= 24 &&
                   bounds.right() <= window_->width() - 24;
        };
        QTRY_VERIFY(checkButton(QStringLiteral("layoutImportConfirmButton")));
        QVERIFY(checkButton(QStringLiteral("layoutImportCancelButton")));
        QVERIFY(checkButton(QStringLiteral("layoutImportFileName")));
        QCOMPARE(item(QStringLiteral("layoutImportFileName"))->property("text").toString(),
                 QStringLiteral("layout-preview.json"));
        captureLayoutImportFrame(dark ? QStringLiteral("dark-import-minimum")
                                      : QStringLiteral("light-import-minimum"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        // Invalid file errors also leave the close action fully reachable.
        QFile invalid(source);
        QVERIFY(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate));
        invalid.write("not JSON");
        invalid.close();
        QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(!controller_->importBusy());
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(item(QStringLiteral("layoutImportError"))->isVisible());
        QVERIFY(checkButton(QStringLiteral("layoutImportCancelButton")));
        QVERIFY(!item(QStringLiteral("layoutImportConfirmButton"))->isEnabled());
        captureLayoutImportFrame(dark ? QStringLiteral("dark-import-error")
                                      : QStringLiteral("light-import-error"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QVERIFY(xdstarter::LayoutStore(source).save(sample.snapshot()).success);
    }
}

void PrototypeWindowTest::captureLayoutImportFrame(const QString& state) {
    const auto directory = qEnvironmentVariable("XDSTARTER_LAYOUT_IMPORT_VALIDATION_DIR");
    if (directory.isEmpty())
        return;
    QVERIFY(QDir().mkpath(directory));
    std::atomic_int frames{0};
    const auto connection = connect(
        window_, &QQuickWindow::afterRendering, window_, [&frames] { ++frames; },
        Qt::DirectConnection);
    QElapsedTimer timer;
    timer.start();
    while (frames.load() < 2 && timer.elapsed() < 3000) {
        window_->requestUpdate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    disconnect(connection);
    QVERIFY2(frames.load() >= 2, "No fresh rendered frames for import capture");
    const auto frame = window_->grabWindow();
    QVERIFY(!frame.isNull());
    QVERIFY(frame.save(QDir(directory).filePath(state + QStringLiteral(".png"))));
    const auto bounds = [this](const QString& name) {
        const auto* value = item(name);
        if (!value)
            return QJsonObject{};
        const auto rectangle = value->mapRectToScene(QRectF(QPointF{}, value->size()));
        return QJsonObject{{QStringLiteral("x"), rectangle.x()},
                           {QStringLiteral("y"), rectangle.y()},
                           {QStringLiteral("width"), rectangle.width()},
                           {QStringLiteral("height"), rectangle.height()},
                           {QStringLiteral("visible"), value->isVisible()},
                           {QStringLiteral("enabled"), value->isEnabled()}};
    };
    const QJsonObject evidence{
        {QStringLiteral("state"), state},
        {QStringLiteral("theme"),
         controller_->darkTheme() ? QStringLiteral("dark") : QStringLiteral("light")},
        {QStringLiteral("qpa"), QGuiApplication::platformName()},
        {QStringLiteral("dpr"), window_->devicePixelRatio()},
        {QStringLiteral("windowWidthDip"), window_->width()},
        {QStringLiteral("windowHeightDip"), window_->height()},
        {QStringLiteral("frameWidthPixels"), frame.width()},
        {QStringLiteral("frameHeightPixels"), frame.height()},
        {QStringLiteral("freshFrames"), frames.load()},
        {QStringLiteral("source"), bounds(QStringLiteral("layoutImportSource"))},
        {QStringLiteral("fileName"), bounds(QStringLiteral("layoutImportFileName"))},
        {QStringLiteral("counts"), bounds(QStringLiteral("layoutImportCounts"))},
        {QStringLiteral("error"), bounds(QStringLiteral("layoutImportError"))},
        {QStringLiteral("cancel"), bounds(QStringLiteral("layoutImportCancelButton"))},
        {QStringLiteral("confirm"), bounds(QStringLiteral("layoutImportConfirmButton"))}};
    QFile output(QDir(directory).filePath(state + QStringLiteral(".json")));
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(output.write(QJsonDocument(evidence).toJson()) > 0);
}

void PrototypeWindowTest::contextMenuShortcutsRespectFocus() {
    const auto state = std::make_shared<CatalogTestState>();
    destroyWindow();
    createWindow({}, true, state);
    QTRY_COMPARE(catalog_->applicationsModel()->rowCount(), 2);
    auto* field = item(QStringLiteral("searchField"));
    auto* menu = sizeMenu();
    auto* pin = window_->findChild<QObject*>(QStringLiteral("pinApplicationMenu"));
    auto* group = window_->findChild<QObject*>(QStringLiteral("groupMenu"));
    QVERIFY(field && menu && pin && group);
    QSignalSpy tileOpened(menu, SIGNAL(opened()));
    QSignalSpy pinOpened(pin, SIGNAL(opened()));
    QSignalSpy groupOpened(group, SIGNAL(opened()));
    QVERIFY(tileOpened.isValid() && pinOpened.isValid() && groupOpened.isValid());
    QTRY_VERIFY(field->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QVERIFY(!menu->property("visible").toBool());
    QVERIFY(!pin->property("visible").toBool());
    QVERIFY(!group->property("visible").toBool());

    auto* tile = item(QStringLiteral("tile-work-standard-1"));
    QVERIFY(tile);
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(tile->hasActiveFocus());
    // Only the documented exact chord is registered; extra modifiers are not stolen.
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier | Qt::ControlModifier);
    QVERIFY(!menu->property("visible").toBool());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY(menu->property("opened").toBool());
    QCOMPARE(tileOpened.count(), 1);
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QCOMPARE(tileOpened.count(), 1);
    QVERIFY(menu->property("visible").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QTRY_VERIFY(tile->hasActiveFocus());

    tile->setEnabled(false);
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QVERIFY(!menu->property("visible").toBool());
    tile->setEnabled(true);
    controller_->setEditMode(true);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QVERIFY(!menu->property("visible").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));

    field->forceActiveFocus(Qt::OtherFocusReason);
    QInputMethodEvent preedit(QStringLiteral("ceshi"), {});
    QCoreApplication::sendEvent(field, &preedit);
    QTRY_VERIFY(field->property("inputMethodComposing").toBool());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QVERIFY(!menu->property("visible").toBool());
    QVERIFY(!pin->property("visible").toBool());
    QInputMethodEvent cancelComposition;
    QCoreApplication::sendEvent(field, &cancelComposition);
    QTRY_VERIFY(!field->property("inputMethodComposing").toBool());

    auto* application = item(QStringLiteral("applicationLaunch-") + state->applications.first().id);
    QVERIFY(application);
    application->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(application->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY(pin->property("opened").toBool());
    QCOMPARE(pinOpened.count(), 1);
    QCOMPARE(state->launches.load(), 0);
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QCOMPARE(pinOpened.count(), 1);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!pin->property("visible").toBool());

    auto* actions = item(QStringLiteral("groupActions-work"));
    QVERIFY(actions && actions->isVisible());
    actions->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(actions->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY(group->property("opened").toBool());
    QCOMPARE(groupOpened.count(), 1);
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QCOMPARE(groupOpened.count(), 1);
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!group->property("visible").toBool());

    openMoreMenu();
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QVERIFY(!menu->property("visible").toBool());
    QVERIFY(!pin->property("visible").toBool());
    QVERIFY(!group->property("visible").toBool());
    auto* more = window_->findChild<QObject*>(QStringLiteral("moreMenu"));
    QVERIFY(more && more->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
    tile->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(tile->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QVERIFY(!menu->property("visible").toBool());
    QCOMPARE(tileOpened.count(), 1);
    QCOMPARE(state->launches.load(), 0);
}

void PrototypeWindowTest::tileNameMouseContextFromSearchRestoresFocus() {
    const auto tileId = QStringLiteral("work-standard-1");
    auto* tile = item(QStringLiteral("tile-") + tileId);
    auto* field = item(QStringLiteral("searchField"));
    QVERIFY(tile && field);
    const auto before = service_->snapshot();
    QTRY_VERIFY(field->hasActiveFocus());
    // Deliberately do not use openSizeMenu(), which first focuses the tile itself.
    QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier,
                      tile->mapToScene(tile->boundingRect().center()).toPoint());
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    click(item(QStringLiteral("renameTileItem")));
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("tileNameDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto* name = item(QStringLiteral("tileNameField"));
    QVERIFY(name);
    QTRY_VERIFY(name->hasActiveFocus());
    QVERIFY(name->setProperty("text", QStringLiteral("未提交的草稿")));
    click(item(QStringLiteral("tileNameCancelButton")));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(tile->hasActiveFocus());
    QCOMPARE(service_->snapshot(), before);

    field->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(field->hasActiveFocus());
    QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier,
                      tile->mapToScene(tile->boundingRect().center()).toPoint());
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    click(item(QStringLiteral("renameTileItem")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTRY_VERIFY(name->hasActiveFocus());
    QVERIFY(name->setProperty("text", QStringLiteral("鼠标修改名称")));
    QTest::keyClick(window_, Qt::Key_Return);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(item(QStringLiteral("tile-") + tileId)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!sizeMenu()->property("visible").toBool());
    QTest::keyClick(window_, Qt::Key_Menu);
    QTRY_VERIFY(sizeMenu()->property("opened").toBool());
}

void PrototypeWindowTest::contentRightEdges() {
    struct LayoutCase {
        int panelWidth;
        int panelHeight;
        int columns;
        bool snapped;
    };
    const QList<LayoutCase> cases{{928, 950, 10, true},  {928, 480, 10, true},
                                  {1040, 480, 12, true}, {496, 950, 7, true},
                                  {480, 480, 6, false},  {980, 950, 10, false}};
    const int shadow = qRound(window_->property("shadowInset").toReal());
    const auto right = [](QQuickItem* item) {
        return item->mapToScene(QPointF(item->width(), 0)).x();
    };
    QJsonArray measurements;
    bool sawOverflow = false;
    bool sawNoOverflow = false;
    const auto destination = qEnvironmentVariable("XDSTARTER_ALIGNMENT_CAPTURE_DIR");
    if (!destination.isEmpty())
        QVERIFY(QDir().mkpath(destination));
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        for (const auto& entry : cases) {
            window_->resize(entry.panelWidth + 2 * shadow, entry.panelHeight + 2 * shadow);
            QTRY_COMPARE(window_->width(), entry.panelWidth + 2 * shadow);
            QTRY_COMPARE(window_->property("compactLayout").toBool(), entry.panelWidth < 928);
            QTRY_VERIFY(!window_->property("layoutWidthPending").toBool());
            QTRY_COMPARE(controller_->gridColumns(), entry.columns);
            auto* viewport = item(QStringLiteral("groupsFlickable"));
            auto* scroll = item(QStringLiteral("groupsScroll"));
            auto* bar = item(QStringLiteral("groupVerticalScrollBar"));
            auto* close = item(QStringLiteral("closeMenuButton"));
            auto* actions = item(QStringLiteral("groupActions-work"));
            auto* grid = item(QStringLiteral("grid-work"));
            QVERIFY(viewport && scroll && bar && close && actions && grid);
            QTRY_COMPARE(viewport->width(),
                         qreal(entry.panelWidth - (entry.panelWidth < 928 ? 112 : 376)));
            QTRY_VERIFY(qAbs(right(viewport) - right(close)) < 0.01);
            QTRY_VERIFY(qAbs(right(actions) - right(close)) < 0.01);
            QCOMPARE(window_->property("tileViewportWidth").toReal(), viewport->width());
            QCOMPARE(right(scroll), right(close) + bar->width());
            QVERIFY2(bar->mapToScene(QPointF(0, 0)).x() >= right(viewport) - 0.01,
                     qPrintable(QStringLiteral("barLeft=%1 viewportRight=%2 scrollRight=%3 "
                                               "width=%4 rightPadding=%5 barWidth=%6")
                                    .arg(bar->mapToScene(QPointF(0, 0)).x())
                                    .arg(right(viewport))
                                    .arg(right(scroll))
                                    .arg(scroll->width())
                                    .arg(scroll->property("rightPadding").toReal())
                                    .arg(bar->width())));
            QVERIFY2(right(bar) <= shadow + entry.panelWidth,
                     qPrintable(QStringLiteral("barRight=%1 panelRight=%2 requestedWidth=%3 "
                                               "actualPanelWidth=%4 closeRight=%5")
                                    .arg(right(bar))
                                    .arg(shadow + entry.panelWidth)
                                    .arg(entry.panelWidth)
                                    .arg(window_->property("panelWidth").toReal())
                                    .arg(right(close))));
            QCOMPARE(grid->width(), qreal(entry.columns * 56 - 8));
            if (entry.snapped) {
                QTRY_VERIFY(qAbs(right(grid) - right(close)) < 0.01);
                QCOMPARE(window_->property("resizeWidthBaseDip").toReal() + entry.columns * 56,
                         qreal(entry.panelWidth));
            } else {
                QVERIFY(viewport->width() - grid->width() >= 0);
                QVERIFY(viewport->width() - grid->width() < 56);
            }
            const bool overflow = viewport->property("contentHeight").toReal() > viewport->height();
            sawOverflow |= overflow;
            sawNoOverflow |= !overflow;
            QCOMPARE(bar->property("size").toReal() < 1, overflow);
            if (window_->property("compactLayout").toBool()) {
                click(item(QStringLiteral("catalogViewButton")));
                auto* catalog = item(QStringLiteral("catalogPanel"));
                QTRY_VERIFY(catalog->isVisible());
                QTRY_VERIFY(qAbs(right(catalog) - right(close)) < 0.01);
                click(item(QStringLiteral("catalogViewButton")));
                QTRY_VERIFY(scroll->isVisible());
                QTRY_VERIFY(qAbs(right(viewport) - right(close)) < 0.01);
            }
            auto* search = item(QStringLiteral("searchField"));
            QVERIFY(search->setProperty("text", QStringLiteral("missing-fixture")));
            QTRY_VERIFY(controller_->searchActive());
            click(item(QStringLiteral("clearSearchButton")));
            QTRY_VERIFY(!controller_->searchActive());
            QTRY_VERIFY(qAbs(right(viewport) - right(close)) < 0.01);
            measurements.append(QJsonObject{{QStringLiteral("dark"), dark},
                                            {QStringLiteral("panelWidth"), entry.panelWidth},
                                            {QStringLiteral("panelHeight"), entry.panelHeight},
                                            {QStringLiteral("columns"), entry.columns},
                                            {QStringLiteral("snapped"), entry.snapped},
                                            {QStringLiteral("verticalOverflow"), overflow},
                                            {QStringLiteral("contentRight"), right(close)},
                                            {QStringLiteral("viewportRight"), right(viewport)},
                                            {QStringLiteral("gridRight"), right(grid)},
                                            {QStringLiteral("scrollBarRight"), right(bar)}});
            if (!destination.isEmpty()) {
                QTest::qWait(220); // Capture after the bounded grid reflow has settled.
                const auto name = QStringLiteral("%1-%2x%3.png")
                                      .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"))
                                      .arg(entry.panelWidth)
                                      .arg(entry.panelHeight);
                QVERIFY(window_->grabWindow().save(QDir(destination).filePath(name)));
            }
        }
    }
    QVERIFY(sawOverflow && sawNoOverflow);
    if (!destination.isEmpty()) {
        QFile output(QDir(destination).filePath(QStringLiteral("alignment.json")));
        QVERIFY(output.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(measurements).toJson();
        QCOMPARE(output.write(bytes), qint64(bytes.size()));
    }
}

void PrototypeWindowTest::presentationSingleReveal() {
    QVERIFY(window_->setProperty("dismissalManaged", true));
    window_->hide();
    QVERIFY(QMetaObject::invokeMethod(window_, "stagePresentation"));
    QCOMPARE(window_->property("revealProgress").toReal(), qreal(0));
    qreal firstVisibleProgress = -1;
    connect(window_, &QWindow::visibleChanged, this, [&](bool visible) {
        if (visible && firstVisibleProgress < 0)
            firstVisibleProgress = window_->property("revealProgress").toReal();
    });
    window_->show();
    QCOMPARE(firstVisibleProgress, qreal(0));
    QCOMPARE(window_->property("revealProgress").toReal(), qreal(0));
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    QTRY_VERIFY(window_->property("revealProgress").toReal() > 0);
    const qreal progress = window_->property("revealProgress").toReal();
    const int generation = window_->property("presentationGeneration").toInt();
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    QTest::qWait(20);
    QVERIFY(window_->property("revealProgress").toReal() >= progress);
    QCOMPARE(window_->property("presentationGeneration").toInt(), generation);
    QTRY_COMPARE(window_->property("revealProgress").toReal(), qreal(1));

    // A queued standalone visibility callback cannot restart a newer managed reveal.
    window_->hide();
    QVERIFY(window_->setProperty("dismissalManaged", false));
    window_->show();
    window_->hide();
    QVERIFY(window_->setProperty("dismissalManaged", true));
    QVERIFY(QMetaObject::invokeMethod(window_, "stagePresentation"));
    const int currentGeneration = window_->property("presentationGeneration").toInt();
    window_->show();
    QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
    QTest::qWait(40);
    QCOMPARE(window_->property("presentationGeneration").toInt(), currentGeneration);
    QVERIFY(!window_->property("presentationStaged").toBool());
    QTRY_COMPARE(window_->property("revealProgress").toReal(), qreal(1));
}

void PrototypeWindowTest::approvedSurfaceStates() {
    const auto capture = [&](const QString& state) {
        const auto directory = QString::fromLocal8Bit(qgetenv("XDSTARTER_SURFACE_CAPTURE_DIR"));
        if (directory.isEmpty())
            return;
        QVERIFY(QDir().mkpath(directory));
        window_->requestUpdate();
        QTest::qWait(180);
        const auto frame = window_->grabWindow();
        QVERIFY(!frame.isNull());
        QVERIFY(frame.save(QDir(directory).filePath(state + QStringLiteral(".png"))));
    };
    for (const bool dark : {true, false}) {
        controller_->setDarkTheme(dark);
        const auto variant = dark ? QStringLiteral("dark") : QStringLiteral("light");
        const auto colors = visualTokens_.value(QStringLiteral("interactionSurface"))
                                .toMap()
                                .value(QStringLiteral("themes"))
                                .toMap()
                                .value(variant)
                                .toMap();
        openSizeMenu(QStringLiteral("work-standard-1"), false);
        auto* dimensions = item(QStringLiteral("tileDimensionsMenuItem"));
        QVERIFY(dimensions);
        QCOMPARE(dimensions->property("leftPadding").toReal(), qreal(38));
        click(dimensions);
        auto* submenu = window_->findChild<QObject*>(QStringLiteral("tileDimensionsMenu"));
        QVERIFY(submenu);
        QTRY_VERIFY(submenu->property("opened").toBool());
        auto* medium = item(QStringLiteral("sizeStandardItem"));
        QVERIFY(medium);
        QCOMPARE(medium->property("text").toString(), QStringLiteral("中磁贴 (2×2)"));
        QCOMPARE(medium->property("leftPadding").toReal(),
                 dimensions->property("leftPadding").toReal());
        auto* check = medium->property("indicator").value<QQuickItem*>();
        QVERIFY(check);
        QCOMPARE(check->property("name").toString(), QStringLiteral("check"));
        QTRY_COMPARE(check->property("status").toInt(), 1);
        auto* wide = item(QStringLiteral("sizeWideItem"));
        QVERIFY(wide);
        QTest::mouseMove(window_, wide->mapToScene(wide->boundingRect().center()).toPoint());
        QTRY_VERIFY(wide->property("hovered").toBool());
        auto* background = wide->property("background").value<QQuickItem*>();
        QVERIFY(background);
        // The first hover frame is already the final neutral color; no accent flash.
        const QColor hover(colors.value(QStringLiteral("hover")).toString());
        for (int frame = 0; frame < 6; ++frame) {
            QTest::qWait(16);
            QCOMPARE(background->property("color").value<QColor>(), hover);
        }
        capture(variant + QStringLiteral("-menus"));
        QVERIFY(QMetaObject::invokeMethod(sizeMenu(), "close"));
        QTRY_VERIFY(!submenu->property("visible").toBool());
        QTRY_VERIFY(!sizeMenu()->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(window_, "openAbout"));
        auto* about = window_->findChild<QObject*>(QStringLiteral("aboutPopup"));
        QVERIFY(about);
        QTRY_VERIFY(about->property("opened").toBool());
        QVERIFY(about->property("modal").toBool());
        auto* aboutDismiss = item(QStringLiteral("aboutDismissButton"));
        QVERIFY(aboutDismiss);
        auto* aboutContent = about->property("contentItem").value<QQuickItem*>();
        QVERIFY(aboutContent);
        auto* homepage = item(QStringLiteral("aboutHomepageButton"));
        auto* homepageText = item(QStringLiteral("aboutHomepageText"));
        QVERIFY(homepage && homepageText);
        QCOMPARE(homepage->property("leftPadding").toReal(), qreal(0));
        QCOMPARE(homepageText->property("horizontalAlignment").toInt(), int(Qt::AlignLeft));
        QTRY_VERIFY(qAbs(homepageText->mapToItem(aboutContent, QPointF(0, 0)).x()) < 0.01);
        QTRY_VERIFY2(qAbs(aboutDismiss->mapToItem(aboutContent, QPointF(aboutDismiss->width(), 0)).x() -
                         aboutContent->width()) <= 1,
                     qPrintable(QStringLiteral("About close right=%1, content width=%2")
                                    .arg(aboutDismiss->mapToItem(aboutContent,
                                                               QPointF(aboutDismiss->width(), 0)).x())
                                    .arg(aboutContent->width())));
        auto* aboutBackground = about->property("background").value<QQuickItem*>();
        QVERIFY(aboutBackground);
        QCOMPARE(aboutBackground->property("radius").toReal(), qreal(12));
        QVERIFY(aboutBackground->property("shadowEnabled").toBool());
        capture(variant + QStringLiteral("-about"));
        click(item(QStringLiteral("closeAboutButton")));
        QTRY_VERIFY(!about->property("visible").toBool());

        window_->setProperty("selectionStatus", QStringLiteral("正在打开测试应用"));
        window_->setProperty("selectionStatusError", false);
        QVERIFY(!window_->property("showSelectionFeedback").toBool());
        const QString launchError = QStringLiteral("测试应用暂时不可用，请重试。");
        QVERIFY(QMetaObject::invokeMethod(controller_.get(), "launchFailed",
                                          Q_ARG(QString, launchError)));
        auto* feedback = item(QStringLiteral("feedbackPanel"));
        QVERIFY(feedback);
        QTRY_VERIFY(feedback->isVisible());
        capture(variant + QStringLiteral("-failure"));
        click(item(QStringLiteral("feedbackCloseButton")));
        QTRY_VERIFY(!feedback->isVisible());
        // Quiet successful edits must not resurrect a dismissed launch error.
        QVERIFY(controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("daily"), 0,
                                      dark ? 4 : 6));
        QTest::qWait(180);
        QVERIFY(!feedback->isVisible());
        // Retrying and receiving the same failure is a new error event.
        QVERIFY(QMetaObject::invokeMethod(controller_.get(), "launchFailed",
                                          Q_ARG(QString, launchError)));
        QTRY_VERIFY(feedback->isVisible());
        auto* feedbackClose = item(QStringLiteral("feedbackCloseButton"));
        feedbackClose->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(feedbackClose->hasActiveFocus());
        QTest::keyClick(window_, Qt::Key_Return);
        QTRY_VERIFY(!feedback->isVisible());
        QTRY_VERIFY(item(QStringLiteral("searchField"))->hasActiveFocus());
        QVERIFY(!feedbackClose->isEnabled());
    }
    QQmlComponent component(engine_.get());
    component.setData("import QtQuick\nimport XDStarter\nHint {}", QUrl{});
    std::unique_ptr<QObject> tooltip(component.create());
    QVERIFY2(tooltip, qPrintable(component.errorString()));
    QCOMPARE(tooltip->property("delay").toInt(), 600);
    auto* tooltipBackground = tooltip->property("background").value<QQuickItem*>();
    QVERIFY(tooltipBackground);
    QCOMPARE(tooltipBackground->property("radius").toReal(), qreal(4));
    QVERIFY(!tooltipBackground->findChild<QObject*>(QStringLiteral("floatingSurfaceShadow")));
}

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    xdstarter::configureInterfaceFont();
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);
    if (QCoreApplication::arguments().contains(QStringLiteral("--native-chrome-probe")))
        return runNativeChromeProbe();
    if (QCoreApplication::arguments().contains(QStringLiteral("--native-screen-transition-probe")))
        return runNativeScreenTransitionProbe();
    if (QCoreApplication::arguments().contains(QStringLiteral("--visual-acceptance-probe")))
        return runVisualAcceptanceProbe();
    if (QCoreApplication::arguments().contains(QStringLiteral("--ui-search-benchmark")))
        return runUiSearchBenchmark();
    PrototypeWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_prototype_window.moc"
