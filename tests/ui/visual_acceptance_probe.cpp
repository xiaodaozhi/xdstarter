// tests/ui/visual_acceptance_probe.cpp

#include "visual_acceptance_probe.h"

#include "app/application_icon_provider.h"
#include "app/menu_controller.h"
#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "platform/platform_integration.h"
#include "services/application_catalog.h"
#include "services/prototype_service.h"

#include <QCoreApplication>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSysInfo>
#include <QThread>
#include <QWaitCondition>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>

namespace {

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::yieldCurrentThread();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

QJsonObject rectangleJson(const QRectF& value) {
    return {{QStringLiteral("x"), value.x()}, {QStringLiteral("y"), value.y()},
            {QStringLiteral("width"), value.width()}, {QStringLiteral("height"), value.height()}};
}

QQuickItem* visualItem(QQuickItem* parent, const QString& name) {
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems())
        if (auto* found = visualItem(child, name))
            return found;
    return nullptr;
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    default: return QStringLiteral("Unknown (%1)").arg(static_cast<int>(api));
    }
}

struct VisualState {
    QVector<xdstarter::LaunchTarget> applications;
    std::atomic_bool workerOnly{true};
    std::atomic_bool firstUnavailable{false};
    std::atomic_int launches{0};
    std::atomic_int scans{0};
    QMutex scanMutex;
    QWaitCondition scanGate;
    bool scanBlocked = false;

    void releaseScan() {
        QMutexLocker locker(&scanMutex);
        scanBlocked = false;
        scanGate.wakeAll();
    }
};

class VisualPlatform final : public xdstarter::ApplicationPlatform {
  public:
    explicit VisualPlatform(std::shared_ptr<VisualState> state) : state_(std::move(state)) {}

    xdstarter::CatalogSnapshot scanApplications() override {
        recordThread();
        ++state_->scans;
        {
            QMutexLocker locker(&state_->scanMutex);
            while (state_->scanBlocked)
                state_->scanGate.wait(&state_->scanMutex);
        }
        auto applications = state_->applications;
        if (state_->firstUnavailable && !applications.isEmpty())
            applications.removeFirst();
        return {applications, {}, true};
    }

    QImage loadIcon(const xdstarter::LaunchTarget& target, int size) override {
        recordThread();
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#F4F7FB")));
        const qreal inset = size * 0.12;
        painter.drawRoundedRect(QRectF(inset, inset, size - 2 * inset, size - 2 * inset),
                                size * 0.14, size * 0.14);
        painter.setBrush(QColor(QStringLiteral("#368BFF")));
        // Anonymous fixture symbols are deliberately unrelated to real brands.
        const bool circular = (target.symbol.front().unicode() % 2) == 0;
        const QRectF mark(size * 0.30, size * 0.27, size * 0.40, size * 0.46);
        if (circular)
            painter.drawEllipse(mark);
        else
            painter.drawRoundedRect(mark, size * 0.05, size * 0.05);
        return image;
    }

    xdstarter::LaunchResult launch(const xdstarter::LaunchTarget&) override {
        recordThread();
        ++state_->launches;
        return {false, QStringLiteral("视觉样例禁止启动应用。"), QStringLiteral("visual_probe_no_launch")};
    }

    xdstarter::LaunchResult checkAvailability(const xdstarter::LaunchTarget& target) override {
        recordThread();
        if (state_->firstUnavailable && !state_->applications.isEmpty() &&
            target.id == state_->applications.first().id)
            return {false, QStringLiteral("此匿名样例目标已移除，可取消固定。"),
                    QStringLiteral("visual_fixture_missing")};
        return {true, {}, {}};
    }

  private:
    void recordThread() {
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            state_->workerOnly = false;
    }
    std::shared_ptr<VisualState> state_;
};

class VisualIntegration final : public xdstarter::PlatformIntegration {
  public:
    QString displayName() const override { return QStringLiteral("Visual fixture"); }
    QString statusDescription() const override { return QStringLiteral("隔离视觉样例，无系统接管。"); }
    bool isSimulated() const override { return true; }
};

struct TileDefinition {
    int application;
    int column;
    int row;
    const char* size;
};

QVector<xdstarter::LaunchTarget> fixtureApplications() {
    const QStringList names{QStringLiteral("A 笔记"), QStringLiteral("B 表格"),
                            QStringLiteral("C 演示"), QStringLiteral("D 代码编辑器"),
                            QStringLiteral("E 浏览器"), QStringLiteral("F 消息"),
                            QStringLiteral("G 照片"), QStringLiteral("H 计算器"),
                            QStringLiteral("I 时钟"), QStringLiteral("J 记事本"),
                            QStringLiteral("K 设置"), QStringLiteral("L 游戏"),
                            QStringLiteral("M 音乐"), QStringLiteral("N 视频"),
                            QStringLiteral("O 终端")};
    QVector<xdstarter::LaunchTarget> applications;
    for (qsizetype index = 0; index < names.size(); ++index) {
        const auto path = QStringLiteral("C:/XDStarterVisualFixture/Application-%1.lnk")
                              .arg(index + 1, 2, 10, QLatin1Char('0'));
        const auto id = xdstarter::applicationTargetId(QStringLiteral("shortcut"), path);
        applications.append({id, QStringLiteral("shortcut"), names[index], names[index].first(1),
                             path, {}, id});
    }
    return applications;
}

QJsonObject fixtureSnapshot(const QVector<xdstarter::LaunchTarget>& applications, bool dark,
                            bool empty) {
    QJsonArray targets;
    if (!empty)
        for (const auto& application : applications)
            targets.append(xdstarter::applicationTargetJson(application));
    const auto group = [&applications](const char* id, const QString& name,
                                       std::initializer_list<TileDefinition> definitions) {
        QJsonArray tiles;
        for (const auto& definition : definitions) {
            tiles.append(QJsonObject{
                {QStringLiteral("id"), QStringLiteral("visual-%1").arg(definition.application + 1)},
                {QStringLiteral("targetId"), applications[definition.application].id},
                {QStringLiteral("column"), definition.column},
                {QStringLiteral("row"), definition.row},
                {QStringLiteral("size"), QString::fromLatin1(definition.size)}});
        }
        return QJsonObject{{QStringLiteral("id"), QString::fromLatin1(id)},
                           {QStringLiteral("name"), name}, {QStringLiteral("tiles"), tiles}};
    };
    QJsonArray groups;
    if (empty) {
        groups.append(group("default", QStringLiteral("常用应用"), {}));
    } else {
        groups.append(group("work", QStringLiteral("工作与学习"),
                            {{0, 0, 0, "standard"}, {1, 2, 0, "standard"},
                             {2, 4, 0, "standard"}, {3, 6, 0, "wide"}}));
        groups.append(group("daily", QStringLiteral("日常"),
                            {{4, 0, 0, "wide"}, {5, 4, 0, "standard"},
                             {6, 6, 0, "standard"}, {7, 8, 0, "small"},
                             {8, 9, 0, "small"}, {9, 8, 1, "small"},
                             {10, 9, 1, "small"}}));
        groups.append(group("entertainment", QStringLiteral("休闲"),
                            {{11, 0, 0, "wide"}, {12, 4, 0, "standard"},
                             {13, 6, 0, "standard"}, {14, 8, 0, "standard"}}));
    }
    return {{QStringLiteral("schemaVersion"), xdstarter::LayoutStore::schemaVersion},
            {QStringLiteral("gridColumns"), 10},
            {QStringLiteral("targets"), targets},
            {QStringLiteral("groups"), groups},
            {QStringLiteral("preferences"),
             QJsonObject{{QStringLiteral("theme"),
                          dark ? QStringLiteral("dark") : QStringLiteral("light")}}}};
}

class Fixture final {
  public:
    Fixture(const QVariantMap& tokens, bool dark, bool empty, bool blocked,
            QStringList& warnings, QStringList& errors)
        : state(std::make_shared<VisualState>()), service(grid, {}, false) {
        state->applications = fixtureApplications();
        const auto result = service.restoreSnapshot(fixtureSnapshot(state->applications, dark, empty));
        if (!result.success) {
            errors.append(QStringLiteral("Fixture snapshot validation: ") + result.message);
            return;
        }
        if (empty)
            state->applications.clear();
        state->scanBlocked = blocked;
        catalog = std::make_unique<xdstarter::ApplicationCatalog>(
            std::make_unique<VisualPlatform>(state));
        controller = std::make_unique<xdstarter::MenuController>(
            service, integration, tokens, nullptr, catalog.get());
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider(QStringLiteral("application-icons"),
                                 new xdstarter::ApplicationIconProvider(catalog.get()));
        QObject::connect(engine.get(), &QQmlEngine::warnings, engine.get(),
                         [&warnings](const QList<QQmlError>& values) {
                             for (const auto& value : values)
                                 warnings.append(value.toString());
                         });
        engine->setInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue(controller.get())}});
        engine->loadFromModule("XDStarter", "Main");
        if (!engine->rootObjects().isEmpty())
            window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        if (!window) {
            errors.append(QStringLiteral("Visual fixture Main QML did not produce a window."));
            return;
        }
        QObject::connect(window, &QQuickWindow::afterRendering, window,
                         [this] { ++renderedFrames; }, Qt::DirectConnection);
        window->requestActivate();
    }

    ~Fixture() {
        // Release a controlled worker before ApplicationCatalog joins its thread,
        // including on any early assertion/capture failure.
        state->releaseScan();
        if (window)
            window->close();
        engine.reset();
        controller.reset();
        catalog.reset();
    }

    QQuickItem* item(const QString& name) const {
        return window ? visualItem(window->contentItem(), name) : nullptr;
    }

    QObject* object(const QString& name) const {
        if (auto* value = item(name))
            return value;
        return window ? window->findChild<QObject*>(name) : nullptr;
    }

    bool ready(bool includeCatalog = true) {
        if (!window || !waitUntil([this] { return window->isVisible() && window->isExposed(); }))
            return false;
        if (includeCatalog && !waitUntil([this] { return !catalog->busy(); }))
            return false;
        if (includeCatalog) {
            for (const auto& application : state->applications)
                catalog->requestIcon(application.id);
            if (!waitUntil([this] {
                    for (const auto& application : state->applications)
                        if (catalog->cachedIcon(application.id).isNull())
                            return false;
                    return true;
                }))
                return false;
        }
        return waitUntil([this] {
            const auto imagesReady = [](auto&& self, QQuickItem* current) -> bool {
                const auto source = current->property("source");
                const auto status = current->property("status");
                // TileButton hides its Image until Ready, so its visible parent
                // must also participate or a loading fallback would pass here.
                const bool pendingTile = current->objectName() == QStringLiteral("tileIcon") &&
                                         current->parentItem() && current->parentItem()->isVisible();
                if ((current->isVisible() || pendingTile) && source.isValid() &&
                    !source.toUrl().isEmpty() && status.isValid() && status.toInt() != 1)
                    return false;
                for (auto* child : current->childItems())
                    if (!self(self, child))
                        return false;
                return true;
            };
            return imagesReady(imagesReady, window->contentItem());
        });
    }

    bool newFrames() {
        for (int index = 0; index < 2; ++index) {
            const auto before = renderedFrames.load();
            window->update();
            if (!waitUntil([this, before] { return renderedFrames.load() > before; }))
                return false;
        }
        return true;
    }

    bool activate(const QString& name) {
        auto* control = item(name);
        if (!control || !control->isVisible() || !control->isEnabled())
            return false;
        control->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Return);
        return true;
    }

    bool popupOpened(const QString& name) const {
        auto* popup = object(name);
        return popup && popup->property("opened").toBool();
    }

    void closePopups() {
        QMetaObject::invokeMethod(window, "closeInternalPopups");
        waitUntil([this] { return !window->property("internalPopupVisible").toBool(); });
    }

    std::shared_ptr<VisualState> state;
    xdstarter::TileGrid grid;
    xdstarter::PrototypeService service;
    VisualIntegration integration;
    std::unique_ptr<xdstarter::ApplicationCatalog> catalog;
    std::unique_ptr<xdstarter::MenuController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    std::atomic_int renderedFrames{0};
};

QJsonArray geometrySignature(Fixture& fixture) {
    QJsonArray result;
    for (const auto& groupValue : fixture.controller->groups()) {
        const auto group = groupValue.toMap();
        for (const auto& tileValue : group.value(QStringLiteral("tiles")).toList()) {
            const auto tile = tileValue.toMap();
            auto* item = fixture.item(QStringLiteral("tile-") + tile.value(QStringLiteral("id")).toString());
            if (!item)
                continue;
            result.append(QJsonObject{
                {QStringLiteral("id"), tile.value(QStringLiteral("id")).toString()},
                {QStringLiteral("name"), tile.value(QStringLiteral("name")).toString()},
                {QStringLiteral("boundsDip"), rectangleJson(QRectF(item->position(), item->size()))}});
        }
    }
    return result;
}

QJsonObject keyBounds(Fixture& fixture) {
    QJsonObject result;
    for (const auto* name :
         {"menuPanel", "shortcutRail", "searchToolbar", "searchField", "moreMenuButton",
          "closeMenuButton", "catalogPanel", "applicationList", "groupsFlickable", "searchPanel",
          "feedbackPanel", "groupNameField", "groupRenameAction", "themeButton"}) {
        if (auto* item = fixture.item(QLatin1String(name))) {
            result.insert(QLatin1String(name), QJsonObject{
                {QStringLiteral("visible"), item->isVisible()},
                {QStringLiteral("sceneBoundsDip"),
                 rectangleJson(item->mapRectToScene(QRectF(QPointF(0, 0), item->size())))}});
        }
    }
    return result;
}

bool defaultGeometryPassed(Fixture& fixture) {
    auto* panel = fixture.item(QStringLiteral("menuPanel"));
    auto* rail = fixture.item(QStringLiteral("shortcutRail"));
    auto* search = fixture.item(QStringLiteral("searchField"));
    auto* viewport = fixture.item(QStringLiteral("groupsFlickable"));
    if (!panel || panel->size() != QSizeF(980, 680) || !rail || rail->width() != 64 || !search ||
        search->height() != 40 || !viewport || !search->isVisible())
        return false;
    const auto panelBounds = panel->mapRectToScene(QRectF(QPointF(0, 0), panel->size()));
    const auto searchBounds = search->mapRectToScene(QRectF(QPointF(0, 0), search->size()));
    if (std::abs(searchBounds.top() - panelBounds.top() - 24) > 0.1 ||
        std::abs(searchBounds.left() - panelBounds.left() - 64 - 24) > 0.1)
        return false;
    for (const auto* name : {"moreMenuButton", "closeMenuButton"}) {
        auto* action = fixture.item(QLatin1String(name));
        if (!action || !action->isVisible() || action->size() != QSizeF(36, 36) ||
            std::abs(action->mapToScene(action->boundingRect().center()).y() -
                     searchBounds.center().y()) > 0.1)
            return false;
    }
    auto* brand = fixture.item(QStringLiteral("brandMark"));
    auto* feedback = fixture.item(QStringLiteral("feedbackPanel"));
    if ((brand && brand->isVisible()) || !feedback || feedback->isVisible())
        return false;
    const auto small = fixture.grid.rectangle(0, 0, xdstarter::TileSize::Small);
    const auto standard = fixture.grid.rectangle(0, 0, xdstarter::TileSize::Standard);
    const auto wide = fixture.grid.rectangle(0, 0, xdstarter::TileSize::Wide);
    const auto adjacent = fixture.grid.rectangle(2, 0, xdstarter::TileSize::Standard);
    if (!small || !standard || !wide || !adjacent || small->width != 48 || small->height != 48 ||
        standard->width != 104 || standard->height != 104 || wide->width != 216 ||
        wide->height != 104 || adjacent->x - standard->width != 8)
        return false;
    int count = 0;
    const auto viewportBounds = viewport->mapRectToScene(QRectF(QPointF(0, 0), viewport->size()));
    for (const auto& group : fixture.service.previewGroups()) {
        for (const auto& tile : group.tiles) {
            auto* item = fixture.item(QStringLiteral("tile-") + tile.id);
            const auto expected = fixture.grid.rectangle(tile.column, tile.row, tile.size);
            if (!item || !expected || item->position() != QPointF(expected->x, expected->y) ||
                item->size() != QSizeF(expected->width, expected->height) ||
                !viewportBounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(
                    item->mapRectToScene(QRectF(QPointF(0, 0), item->size()))))
                return false;
            ++count;
        }
    }
    return count == 15;
}

bool visibleHint(Fixture& fixture, const QString& name) {
    auto* hint = fixture.item(name);
    return hint && hint->isVisible() && !hint->property("text").toString().isEmpty();
}

bool visibleItem(Fixture& fixture, const QString& name) {
    auto* item = fixture.item(name);
    return item && item->isVisible();
}

bool unavailableMarkerVisible(Fixture& fixture) {
    auto* tile = fixture.item(QStringLiteral("tile-visual-1"));
    auto* marker = visualItem(tile, QStringLiteral("unavailableMarker"));
    return marker && marker->isVisible();
}

} // namespace

int runVisualAcceptanceProbe() {
    const auto outputDirectory = qEnvironmentVariable("XDSTARTER_VISUAL_VALIDATION_DIR");
    if (outputDirectory.isEmpty() || !QDir::isAbsolutePath(outputDirectory) ||
        !QDir().mkpath(outputDirectory)) {
        qCritical() << "Set XDSTARTER_VISUAL_VALIDATION_DIR to an absolute writable directory.";
        return 1;
    }
    QStringList errors;
    QStringList warnings;
    QJsonArray captures;
    QJsonArray referenceGeometry;
    bool sharedThemeGeometry = false;
    const auto require = [&errors](bool passed, const QString& message) {
        if (!passed)
            errors.append(message);
        return passed;
    };
    QJsonObject evidence{
        {QStringLiteral("scope"), QStringLiteral("同产品 QML/controller 的隔离固定数据视觉状态；无真实目录、应用启动、托盘、系统接管或用户布局读写。")},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platformName"), QGuiApplication::platformName()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("buildAbi"), QSysInfo::buildAbi()},
        {QStringLiteral("qtScaleFactorEnvironment"), qEnvironmentVariable("QT_SCALE_FACTOR")},
        {QStringLiteral("qtScreenScaleFactorsEnvironment"), qEnvironmentVariable("QT_SCREEN_SCALE_FACTORS")},
        {QStringLiteral("noUserLayoutIO"), true},
        {QStringLiteral("noRealApplicationCatalogOrLaunch"), true},
        {QStringLiteral("noTrayOrGlobalInput"), true},
        {QStringLiteral("systemDisplaySettingsChanged"), false}};
    QFile tokenFile(QStringLiteral(":/xdstarter/tokens/visual.json"));
    const auto execute = [&]() -> bool {
        if (!require(tokenFile.open(QIODevice::ReadOnly), QStringLiteral("Cannot open visual tokens.")))
            return false;
        const auto document = QJsonDocument::fromJson(tokenFile.readAll());
        if (!require(document.isObject(), QStringLiteral("Visual tokens are not an object.")))
            return false;
        const auto tokens = document.object().toVariantMap();
        QQuickWindow::setDefaultAlphaBuffer(true);
        const auto capture = [&](Fixture& fixture, const QString& theme, const QString& state,
                                  const std::function<bool()>& stateCheck, bool catalogReady = true) {
            const bool ready = fixture.ready(catalogReady) && fixture.newFrames();
            const bool stateBeforeCapture = ready && stateCheck();
            const auto captured = ready ? fixture.window->grabWindow() : QImage{};
            const bool statePassed = stateBeforeCapture && stateCheck();
            // QQuickTextInput::boundingRect() measures text/cursor ink; layout bounds
            // elsewhere use size() explicitly, including an empty search field.
            // Store a portable RGBA PNG without pretending an RGB offscreen backing
            // store supplied native transparency. Windows alpha is checked separately.
            const auto image = captured.convertToFormat(QImage::Format_RGBA8888);
            const bool nativeAlphaRequired = QGuiApplication::platformName() == QStringLiteral("windows");
            const bool transparentCorners = !captured.isNull() && captured.hasAlphaChannel() &&
                captured.pixelColor(0, 0).alpha() == 0 &&
                captured.pixelColor(captured.width() - 1, 0).alpha() == 0 &&
                captured.pixelColor(0, captured.height() - 1).alpha() == 0 &&
                captured.pixelColor(captured.width() - 1, captured.height() - 1).alpha() == 0;
            const bool nativeAlphaPassed = !nativeAlphaRequired || transparentCorners;
            const auto filename = theme + QLatin1Char('-') + state + QStringLiteral(".png");
            const bool saved = !image.isNull() && image.hasAlphaChannel() &&
                               image.save(QDir(outputDirectory).filePath(filename));
            const auto dpr = fixture.window ? fixture.window->devicePixelRatio() : 0;
            const bool pixelDimensions = fixture.window && !image.isNull() &&
                image.size() == QSize(qRound(fixture.window->width() * dpr),
                                      qRound(fixture.window->height() * dpr));
            const bool passed = statePassed && ready && saved && pixelDimensions && nativeAlphaPassed;
            captures.append(QJsonObject{
                {QStringLiteral("theme"), theme}, {QStringLiteral("state"), state},
                {QStringLiteral("screenshot"), filename}, {QStringLiteral("widthPx"), image.width()},
                {QStringLiteral("heightPx"), image.height()}, {QStringLiteral("devicePixelRatio"), dpr},
                {QStringLiteral("windowWidthDip"), fixture.window ? fixture.window->width() : 0},
                {QStringLiteral("windowHeightDip"), fixture.window ? fixture.window->height() : 0},
                {QStringLiteral("graphicsApi"), fixture.window ? graphicsApiName(fixture.window->rendererInterface()->graphicsApi()) : QStringLiteral("Unavailable")},
                {QStringLiteral("renderedFrameSerial"), fixture.renderedFrames.load()},
                {QStringLiteral("freshFramesPassed"), ready}, {QStringLiteral("stateAssertionPassed"), statePassed},
                {QStringLiteral("pixelDimensionsPassed"), pixelDimensions},
                {QStringLiteral("fullRgbaWindowFrame"), saved},
                {QStringLiteral("originalCaptureHasAlphaChannel"), captured.hasAlphaChannel()},
                {QStringLiteral("originalTransparentCorners"), transparentCorners},
                {QStringLiteral("nativeAlphaRequired"), nativeAlphaRequired},
                {QStringLiteral("nativeAlphaPassed"), nativeAlphaPassed},
                {QStringLiteral("bounds"), fixture.window ? keyBounds(fixture) : QJsonObject{}},
                {QStringLiteral("tiles"), fixture.controller ? geometrySignature(fixture) : QJsonArray{}},
                {QStringLiteral("passed"), passed}});
            return require(passed, theme + QLatin1Char('/') + state + QStringLiteral(" visual assertion/capture failed."));
        };

        for (const bool dark : {true, false}) {
            const auto theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
            Fixture fixture(tokens, dark, false, false, warnings, errors);
            if (!require(fixture.ready(), theme + QStringLiteral(" fixture readiness timed out.")))
                return false;
            const auto currentGeometry = geometrySignature(fixture);
            if (dark)
                referenceGeometry = currentGeometry;
            else
                sharedThemeGeometry = currentGeometry == referenceGeometry;
            capture(fixture, theme, QStringLiteral("browse"), [&] { return defaultGeometryPassed(fixture); });

            const bool openedMore = fixture.activate(QStringLiteral("moreMenuButton")) &&
                waitUntil([&] { return fixture.popupOpened(QStringLiteral("moreMenu")); });
            capture(fixture, theme, QStringLiteral("more-menu"), [&] { return openedMore &&
                visibleHint(fixture, QStringLiteral("themeButton")) &&
                visibleHint(fixture, QStringLiteral("saveLayoutButton")) &&
                visibleHint(fixture, QStringLiteral("aboutButton")); });
            fixture.closePopups();

            QTest::keyClick(fixture.window, Qt::Key_F, Qt::ControlModifier);
            fixture.controller->setSearchQuery(QStringLiteral("笔记"));
            const bool found = waitUntil([&] {
                return !fixture.controller->searchBusy() && fixture.controller->searchResults().size() == 1;
            });
            capture(fixture, theme, QStringLiteral("search-results"), [&] { return found &&
                visibleItem(fixture, QStringLiteral("searchPanel")); });
            fixture.controller->setSearchQuery(QStringLiteral("zzzz-visual-no-match"));
            const bool noResults = waitUntil([&] {
                return !fixture.controller->searchBusy() && fixture.controller->searchResults().isEmpty();
            });
            capture(fixture, theme, QStringLiteral("search-empty"), [&] { return noResults &&
                visibleHint(fixture, QStringLiteral("emptySearchHint")); });
            fixture.controller->setSearchQuery({});
            if (!require(waitUntil([&] { return !fixture.controller->searchActive(); }),
                         theme + QStringLiteral(" clearing search timed out.")))
                return false;

            const bool edited = fixture.controller->editMode();
            capture(fixture, theme, QStringLiteral("edit"), [&] { return edited &&
                visibleItem(fixture, QStringLiteral("groupActions-work")); });
            QMetaObject::invokeMethod(fixture.window, "openMoreMenu");
            waitUntil([&] { return fixture.popupOpened(QStringLiteral("moreMenu")); });
            const bool openedDialog = fixture.activate(QStringLiteral("newGroupButton")) &&
                waitUntil([&] { return fixture.popupOpened(QStringLiteral("groupNameDialog")); });
            capture(fixture, theme, QStringLiteral("group-name-dialog"), [&] { return openedDialog &&
                visibleHint(fixture, QStringLiteral("groupNameSaveButton")) &&
                visibleHint(fixture, QStringLiteral("groupNameCancelButton")); });
            fixture.closePopups();
            auto* actions = fixture.item(QStringLiteral("groupActions-work"));
            if (actions) {
                actions->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(fixture.window, Qt::Key_F10, Qt::ShiftModifier);
            }
            const bool openedGroup = waitUntil([&] { return fixture.popupOpened(QStringLiteral("groupMenu")); });
            capture(fixture, theme, QStringLiteral("group-menu"), [&] {
                return openedGroup && visibleHint(fixture, QStringLiteral("groupRenameAction")) &&
                       visibleHint(fixture, QStringLiteral("groupMoveDownAction")) &&
                       visibleItem(fixture, QStringLiteral("groupDeleteAction"));
            });
            fixture.closePopups();

            fixture.state->firstUnavailable = true;
            fixture.controller->refreshApplications();
            const bool missing = waitUntil([&] {
                auto* tile = fixture.item(QStringLiteral("tile-visual-1"));
                return !fixture.catalog->busy() && tile && !tile->property("targetAvailable").toBool();
            });
            capture(fixture, theme, QStringLiteral("unavailable"), [&] { return missing &&
                visibleHint(fixture, QStringLiteral("groupTitle-work")) &&
                unavailableMarkerVisible(fixture); });

            const auto shadow = fixture.window->property("shadowInset").toReal();
            fixture.window->resize(qRound(480 + 2 * shadow), qRound(480 + 2 * shadow));
            const bool compact = waitUntil([&] { return fixture.window->property("compactLayout").toBool(); });
            capture(fixture, theme, QStringLiteral("narrow"), [&] { return compact &&
                visibleItem(fixture, QStringLiteral("catalogViewButton")) &&
                geometrySignature(fixture) == referenceGeometry; });
            require(fixture.state->workerOnly && fixture.state->launches == 0,
                    theme + QStringLiteral(" fixture attempted GUI-thread platform work or application launch."));

            Fixture empty(tokens, dark, true, false, warnings, errors);
            if (!require(empty.ready(), theme + QStringLiteral(" empty fixture timed out.")))
                return false;
            capture(empty, theme, QStringLiteral("empty"), [&] { return geometrySignature(empty).isEmpty() &&
                visibleHint(empty, QStringLiteral("emptyGroupHint")) &&
                visibleHint(empty, QStringLiteral("emptyCatalogHint")); });

            Fixture loading(tokens, dark, true, true, warnings, errors);
            const bool enteredScan = waitUntil([&] { return loading.state->scans > 0; });
            capture(loading, theme, QStringLiteral("loading"), [&] { return enteredScan &&
                loading.controller && loading.controller->catalogBusy() &&
                visibleHint(loading, QStringLiteral("emptyCatalogHint")); }, false);
            loading.state->releaseScan();
            require(waitUntil([&] { return !loading.catalog->busy(); }) && loading.state->workerOnly &&
                        loading.state->launches == 0,
                    theme + QStringLiteral(" controlled loading worker did not release cleanly."));
        }
        require(sharedThemeGeometry, QStringLiteral("Deep/light tile names and shared geometry differ."));
        require(warnings.isEmpty(), QStringLiteral("QML emitted warnings."));
        return errors.isEmpty();
    };
    const bool passed = execute();
    evidence.insert(QStringLiteral("captures"), captures);
    evidence.insert(QStringLiteral("requestedCaptures"), 22);
    evidence.insert(QStringLiteral("completedCaptures"), captures.size());
    evidence.insert(QStringLiteral("themesShareTileNamesAndGeometry"), sharedThemeGeometry);
    evidence.insert(QStringLiteral("qmlWarnings"), QJsonArray::fromStringList(warnings));
    evidence.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
    evidence.insert(QStringLiteral("passed"), passed);
    QFile output(QDir(outputDirectory).filePath(QStringLiteral("visual-acceptance-probe.json")));
    const auto serialized = QJsonDocument(evidence).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(serialized) != serialized.size()) {
        qCritical() << "Cannot write visual acceptance evidence:" << output.errorString();
        return 1;
    }
    qInfo().noquote() << "Visual acceptance probe:" << (passed ? "PASS" : "FAIL")
                      << "(fixture catalog/icons, full QML frames, no system actions)";
    return passed ? 0 : 1;
}
