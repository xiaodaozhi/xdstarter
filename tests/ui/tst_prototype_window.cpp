#include "native_chrome_probe.h"
#include "visual_acceptance_probe.h"

#include "app/application_icon_provider.h"
#include "app/menu_controller.h"
#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "platform/desktop_platform.h"
#include "platform/platform_integration.h"
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/prototype_service.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSGRendererInterface>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace {

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
        return {state_->includeApplications ? state_->applications
                                           : QVector<xdstarter::LaunchTarget>{},
                state_->scanWarns ? QStringList{QStringLiteral("测试目录部分不可用")} : QStringList{},
                state_->scanSucceeds};
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
    void themeSwitchRetainsGeometry();
    void keyboardAndMouseSelection();
    void smallTilesExposeNames();
    void longNamesRemainBounded();
    void resizePreservesGeometryAndRevealsFocus();
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
    void framelessWindowDismissalIntent();
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
    void layoutImportDialogThemeAndKeyboard();
    void layoutImportMinimumPanel();

  private:
    QQuickItem* item(const QString& name) const;
    QVariantMap tileData(const QString& id) const;
    QMap<QString, QRectF> tileGeometries() const;
    void click(QQuickItem* item);
    void openMoreMenu();
    void createWindow(const QString& path = {}, bool sampleMode = true,
                      std::shared_ptr<CatalogTestState> catalogState = {},
                      std::shared_ptr<DesktopTestState> desktopState = {});
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
                                      std::shared_ptr<DesktopTestState> desktopState) {
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
        *service_, *platform_, visualTokens_, nullptr, catalog_.get(), desktopShell_.get());
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

void PrototypeWindowTest::click(QQuickItem* target) {
    QVERIFY(target);
    const auto name = target->objectName();
    if (name == QLatin1String("themeButton") || name == QLatin1String("saveLayoutButton") ||
        name == QLatin1String("aboutButton")) {
        openMoreMenu();
        QTRY_VERIFY(target->isVisible());
    }
    auto* targetWindow = target->window();
    QVERIFY(targetWindow);
    QTest::mouseClick(targetWindow, Qt::LeftButton, Qt::NoModifier,
                      target->mapToScene(target->boundingRect().center()).toPoint());
}

void PrototypeWindowTest::openMoreMenu() {
    auto* menu = window_->findChild<QObject*>(QStringLiteral("moreMenu"));
    QVERIFY(menu);
    if (!menu->property("visible").toBool()) {
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
    QTRY_VERIFY(window_->property("darkTheme").toBool());
    QCOMPARE(surface->property("color").value<QColor>(), originalColor);
    QCOMPARE(tileGeometries(), geometry);
    QCOMPARE(window_->size(), size);
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
    small->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(small->hasActiveFocus());
    QTRY_VERIFY(tooltip->property("visible").toBool());
    QVERIFY(small->property("visualFocus").toBool());
    item(QStringLiteral("moreMenuButton"))->forceActiveFocus(Qt::TabFocusReason);
    QTest::mouseMove(window_, QPoint(1, 1));
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
    QTRY_VERIFY(flickable->width() < 552);
    last->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(last->hasActiveFocus());
    QTRY_VERIFY(flickable->property("contentX").toReal() > 0);
    QTRY_VERIFY(flickable->property("contentY").toReal() > 0);
    QTRY_VERIFY(flickable->boundingRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(
        last->mapRectToItem(flickable, last->boundingRect())));
    QCOMPARE(tileGeometries(), geometry);
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
    auto* tile = item(QStringLiteral("tile-") + tileId);
    QVERIFY(tile);
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(viewportScene().adjusted(-1, -1, 1, 1).contains(
        tile->mapRectToScene(tile->boundingRect())));
    const auto start = tile->mapToScene(tile->boundingRect().center()).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window_, start + QPoint(24, 0), 20);
    QTRY_VERIFY(window_->property("dragActive").toBool());
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
    click(item(QStringLiteral("editLayoutButton")));
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
    QTRY_COMPARE(item(QStringLiteral("grid-work"))->height(), 216.0);
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
    controller_->setEditMode(true);
    const auto before = controller_->groups();
    const auto selection = item(QStringLiteral("selectionStatus"))->property("text").toString();
    QSignalSpy changed(controller_.get(), &xdstarter::MenuController::groupsChanged);
    beginMouseDrag(QStringLiteral("work-standard-1"));
    moveMouseToGrid(QStringLiteral("work"), QPointF(164, 52));
    QVERIFY(!window_->property("dragValid").toBool());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("work"), QPointF(164, 52)));
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QCOMPARE(controller_->groups(), before);
    QCOMPARE(changed.count(), 0);
    QVERIFY(item(QStringLiteral("selectionStatus"))->property("text").toString() != selection);

    beginMouseDrag(QStringLiteral("work-standard-1"));
    moveMouseToGrid(QStringLiteral("work"), QPointF(52, 164));
    QVERIFY(window_->property("dragValid").toBool());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->property("dragActive").toBool());
    QVERIFY(window_->isVisible());
    QVERIFY(controller_->editMode());
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                        gridPosition(QStringLiteral("work"), QPointF(52, 164)));
    QCOMPARE(controller_->groups(), before);
    QCOMPARE(changed.count(), 0);
    QVERIFY(item(QStringLiteral("selectionStatus"))->property("text").toString().contains(
        QStringLiteral("取消")));
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!controller_->editMode());
    QVERIFY(window_->isVisible());
    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY(!window_->isVisible());
}

QObject* PrototypeWindowTest::sizeMenu() const {
    return window_->findChild<QObject*>(QStringLiteral("tileSizeMenu"));
}

void PrototypeWindowTest::openSizeMenu(const QString& tileId, bool keyboard) {
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
    QCOMPARE(tileData(id).value(QStringLiteral("size")).toString(), QStringLiteral("standard"));
    QVERIFY(!controller_->operationMessage().isEmpty());

    controller_->setEditMode(true);
    QTRY_VERIFY(item(QStringLiteral("tile-") + id)->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(tileData(id).value(QStringLiteral("column")).toInt(), 0);
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
    QCOMPARE(item(QStringLiteral("tile-") + id)->size(), QSizeF(216, 104));
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
        const QColor hover(theme.value(QStringLiteral("hover")).toString());
        const QColor pressed(theme.value(QStringLiteral("pressed")).toString());
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
    const QByteArray original = "{\"schemaVersion\":4,\"futureData\":\"keep\"}";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), qint64(original.size()));
    file.close();
    destroyWindow();
    createWindow(path);
    QVERIFY(controller_->readOnly());
    QVERIFY(controller_->persistenceError());
    QTRY_VERIFY(item(QStringLiteral("persistenceStatus"))->isVisible());
    QVERIFY(!item(QStringLiteral("editLayoutButton"))->isEnabled());
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
    QVERIFY(!controller_->flushSave());
    destroyWindow();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
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
    const auto buttonName = QStringLiteral("pinButton-") + applicationId;
    QTRY_VERIFY(item(buttonName));
    QVERIFY(item(buttonName)->isEnabled());
    click(item(buttonName));
    auto* menu = window_->findChild<QObject*>(QStringLiteral("pinApplicationMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("visible").toBool());
    const auto groupId = controller_->groups().first().toMap().value(QStringLiteral("id")).toString();
    const auto groupItem = QStringLiteral("pinToGroup-") + groupId;
    QTRY_VERIFY(item(groupItem));
    QTRY_VERIFY(menu->property("opened").toBool());
    item(groupItem)->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(item(groupItem)->hasActiveFocus());
    QVERIFY(item(groupItem)->window());
    QTest::keyClick(item(groupItem)->window(), Qt::Key_Return);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QTRY_COMPARE(tileGeometries().size(), 1);
    QTRY_VERIFY(!item(buttonName)->isEnabled());
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
        const auto center = icon->mapToScene(icon->boundingRect().center());
        const auto pixel = QPoint(qRound(center.x() * image.width() / window_->width()),
                                   qRound(center.y() * image.height() / window_->height()));
        return image.rect().contains(pixel) &&
               image.pixelColor(pixel) == QColor(QStringLiteral("#36D6CB"));
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
    QTRY_VERIFY(item(QStringLiteral("pinButton-") + applicationId)->isEnabled());
    QCOMPARE(catalog_->applicationsModel()->rowCount(), 2);
    QCOMPARE(state->launches.load(), 0);
    QTRY_VERIFY(item(QStringLiteral("editLayoutButton"))->hasActiveFocus());

    // Compact browsing changes panes without changing or shrinking the tile grid.
    const int shadow = qRound(window_->property("shadowInset").toReal());
    window_->resize(480 + 2 * shadow, 480 + 2 * shadow);
    QTRY_VERIFY(item(QStringLiteral("catalogViewButton"))->isVisible());
    QVERIFY(item(QStringLiteral("groupsScroll"))->isVisible());
    click(item(QStringLiteral("catalogViewButton")));
    QTRY_VERIFY(item(QStringLiteral("catalogPanel"))->isVisible());
    QVERIFY(!item(QStringLiteral("groupsScroll"))->isVisible());
    click(item(QStringLiteral("catalogViewButton")));
    QTRY_VERIFY(item(QStringLiteral("groupsScroll"))->isVisible());
    QCOMPARE(item(QStringLiteral("grid-default"))->width(), qreal(552));

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
    controller_->setEditMode(true);
    click(item(QStringLiteral("tile-") + tileId));
    QTest::qWait(50);
    QCOMPARE(state->launches.load(), 2);
    QVERIFY(window_->isVisible());
    controller_->setEditMode(false);
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
    QTRY_VERIFY(!controller_->editMode());
    QCOMPARE(dismissRequested.count(), 0);
    QTest::keyClick(window_, Qt::Key_Escape);
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
    QCOMPARE(controller_->groups(), before);
    QCOMPARE(item(QStringLiteral("grid-work"))->width(), qreal(552));
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
    click(item(QStringLiteral("editLayoutButton")));
    QVERIFY(controller_->editMode());
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
    QTRY_VERIFY(item(QStringLiteral("groupActions-") + groupId)->hasActiveFocus());

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
    QVERIFY(label->width() <= item(QStringLiteral("grid-") + groupId)->width());
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
    QTRY_VERIFY(item(QStringLiteral("searchField"))->hasActiveFocus());
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
    click(item(QStringLiteral("pinButton-") + applicationId));
    auto* pinMenu = window_->findChild<QObject*>(QStringLiteral("pinApplicationMenu"));
    QVERIFY(pinMenu);
    QTRY_VERIFY(pinMenu->property("opened").toBool());
    auto* pinToNewGroup = item(QStringLiteral("pinToGroup-") + newId);
    QVERIFY(pinToNewGroup && pinToNewGroup->isEnabled());
    QCOMPARE(pinToNewGroup->property("text").toString(), QStringLiteral("工具 Tools"));
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
            item(QStringLiteral("editLayoutButton"))->forceActiveFocus(Qt::TabFocusReason);
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
    QVERIFY(!controller_->editMode());
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
        for (const auto* action : {"account", "documents", "downloads", "settings", "power"}) {
            const auto id = QLatin1String(action);
            auto* button = item(QStringLiteral("shortcut-") + id);
            QVERIFY(button);
            QCOMPARE(button->size(), QSizeF(48, 48));
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
    QCOMPARE(window_->flags() & Qt::WindowType_Mask, Qt::WindowFlags(Qt::Window));
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
    for (const auto* name : {"shortcut-account", "shortcut-documents", "shortcut-downloads",
                             "shortcut-settings", "shortcut-power", "editLayoutButton",
                             "moreMenuButton", "closeMenuButton"}) {
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
        auto image = window_->grabWindow();
        if (!image.hasAlphaChannel() &&
            QGuiApplication::platformName() == QStringLiteral("offscreen") &&
            window_->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
            // The offscreen software backing store and item-grab layer flatten
            // to the RGB screen format. Inspect the actual Canvas paint PNG,
            // retaining its original alpha without synthesizing a channel.
            // Rail transparency and child bounds are independently checked above.
            QVERIFY(surface->metaObject()->indexOfMethod("toDataURL(QString)") >= 0);
            QString dataUrl;
            QVERIFY(QMetaObject::invokeMethod(surface, "toDataURL", Q_RETURN_ARG(QString, dataUrl),
                                              Q_ARG(QString, QStringLiteral("image/png"))));
            const auto prefix = QStringLiteral("data:image/png;base64,");
            QVERIFY(dataUrl.startsWith(prefix));
            const auto png = QByteArray::fromBase64(dataUrl.mid(prefix.size()).toLatin1());
            image = QImage::fromData(png, "PNG");
            qInfo() << "Chrome alpha capture: Canvas toDataURL original paint PNG"
                       " (offscreen software RGB backing-store limitation)";
        } else {
            qInfo() << "Chrome alpha capture: complete QQuickWindow grabWindow";
        }
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
    for (const auto* name : {"editLayoutButton", "moreMenuButton", "closeMenuButton"}) {
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
    QTRY_COMPARE(firstGrid->height(), qreal(216));
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
    QVERIFY(!controller_->moveTile(QStringLiteral("work-standard-1"), QStringLiteral("work"), 2, 0));
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
    const auto literal = QStringLiteral("<b>工具 Tools</b>");
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
    tile->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(tooltip->property("visible").toBool());
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
    QVERIFY(!controller_->editMode());
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
        // Public Qt 6.8 FileDialog ReadOnly (0x10) and DontUseNativeDialog (0x08)
        // are missing from SDK qmltypes, so also verify the actual runtime flags.
        QCOMPARE(picker->property("options").toInt() & 0x18, 0x18);
        QVERIFY(window_->property("internalPopupVisible").toBool());
        // A forced Quick picker must stay in the product window, or have a public
        // QWindow transient chain belonging to it; native COM dialogs do not count.
        auto* focus = QGuiApplication::focusWindow();
        QTRY_VERIFY(QGuiApplication::focusWindow());
        focus = QGuiApplication::focusWindow();
        while (focus && focus != window_)
            focus = focus->transientParent();
        QCOMPARE(focus, window_);
        if (dark)
            QVERIFY(QMetaObject::invokeMethod(picker, "reject"));
        else
            QVERIFY(QMetaObject::invokeMethod(window_, "prepareForDismissal"));
        QTRY_VERIFY(!picker->property("visible").toBool());
        QVERIFY(!window_->property("internalPopupVisible").toBool());
        QVERIFY(window_->isVisible());
    }
}

void PrototypeWindowTest::layoutImportDialogThemeAndKeyboard() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("current.json"));
    const auto source = directory.filePath(QStringLiteral("source.json"));
    xdstarter::PrototypeService sample(grid_);
    auto replacement = sample.snapshot();
    destroyWindow();
    createWindow(path, false);
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
        confirm->forceActiveFocus(Qt::TabFocusReason);
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
        QVERIFY(!window_->property("internalPopupVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window_, "prepareForPresentation"));
        QVERIFY(controller_->stageLayoutImport(QUrl::fromLocalFile(source)));
        QTRY_VERIFY(!controller_->importBusy());
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        confirm->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window_, Qt::Key_Return);
        QVERIFY(controller_->importCommitting());
        QVERIFY(!confirm->isEnabled());
        QCOMPARE(confirm->opacity(), 0.45);
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

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);
    if (QCoreApplication::arguments().contains(QStringLiteral("--native-chrome-probe")))
        return runNativeChromeProbe();
    if (QCoreApplication::arguments().contains(QStringLiteral("--native-screen-transition-probe")))
        return runNativeScreenTransitionProbe();
    if (QCoreApplication::arguments().contains(QStringLiteral("--visual-acceptance-probe")))
        return runVisualAcceptanceProbe();
    PrototypeWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_prototype_window.moc"
