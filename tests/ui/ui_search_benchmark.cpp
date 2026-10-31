#include "ui_search_benchmark.h"

#include "app/application_icon_provider.h"
#include "app/desktop_window_host.h"
#include "app/menu_controller.h"
#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/platform_integration.h"
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/prototype_service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSysInfo>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace {

constexpr int catalogSize = 1000;
constexpr int tileCount = 100;
constexpr int warmupCount = 5;

class SyntheticPlatform final : public xdstarter::ApplicationPlatform {
  public:
    explicit SyntheticPlatform(QVector<xdstarter::LaunchTarget> entries)
        : entries_(std::move(entries)) {}
    xdstarter::CatalogSnapshot scanApplications() override { return {entries_, {}, true, {}}; }
    QImage loadIcon(const xdstarter::LaunchTarget&, int size) override {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QStringLiteral("#36d6cb")));
        return image;
    }
    xdstarter::LaunchResult launch(const xdstarter::LaunchTarget&) override {
        return {false, QStringLiteral("Synthetic benchmark cannot launch applications."),
                QStringLiteral("benchmark-launch-disabled")};
    }
    xdstarter::LaunchResult checkAvailability(const xdstarter::LaunchTarget&) override {
        return {true, {}, {}};
    }

  private:
    QVector<xdstarter::LaunchTarget> entries_;
};

class SyntheticIntegration final : public xdstarter::PlatformIntegration {
  public:
    QString displayName() const override { return QStringLiteral("Synthetic benchmark"); }
    QString statusDescription() const override { return QStringLiteral("No system integration"); }
    bool isSimulated() const override { return true; }
};

class SyntheticRevealAnchor final : public xdstarter::DesktopPlatform {
  public:
    xdstarter::DesktopActionResult openDesktopAction(const QString&) override { return {}; }
    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override {
        xdstarter::DesktopAnchorResult anchor;
        anchor.monitorPhysicalRect = QRect(0, 0, 1920, 1080);
        anchor.workAreaPhysicalRect = QRect(0, 0, 1920, 1008);
        anchor.startButtonPhysicalRect = QRect(24, 1008, 72, 72);
        anchor.dpi = 96;
        return anchor;
    }
};

class SyntheticRevealPlacement final : public xdstarter::DesktopPlatform {
  public:
    xdstarter::DesktopActionResult openDesktopAction(const QString&) override { return {}; }
    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override { return {}; }
    QScreen* screenForAnchor(const xdstarter::DesktopAnchorResult&) override {
        return QGuiApplication::primaryScreen();
    }
    bool hasOwnedForegroundWindow(quintptr) override { return true; }
    xdstarter::DesktopPlacementResult placeWindow(quintptr, const xdstarter::DesktopAnchorResult&,
                                                   int, bool, int) override {
        return {true, QRect(0, 0, 980, 680), {}, {}};
    }
};

class SyntheticRevealTray final : public xdstarter::DesktopTray {
  public:
    xdstarter::DesktopActionResult start(const QImage&) override {
        available_ = true;
        return {true, QStringLiteral("Synthetic tray"), {}};
    }
    void stop() override { available_ = false; }
    bool isAvailable() const override { return available_; }
    void showContextMenu() override {}
  private:
    bool available_ = false;
};

QVector<xdstarter::LaunchTarget> syntheticEntries() {
    QVector<xdstarter::LaunchTarget> entries;
    entries.reserve(catalogSize);
    for (int i = 0; i < catalogSize; ++i) {
        xdstarter::LaunchTarget target;
        target.type = QStringLiteral("shortcut");
        target.shortcutPath = QStringLiteral("C:/XDStarterBenchmark/item%1.lnk").arg(i, 4, 10, QLatin1Char('0'));
        target.id = xdstarter::applicationTargetId(target.type, target.shortcutPath);
        target.name = QStringLiteral("Fixture item%1").arg(i, 4, 10, QLatin1Char('0'));
        target.symbol = QStringLiteral("F");
        target.iconKey = target.id;
        entries.append(target);
    }
    return entries;
}

QJsonObject syntheticLayout(xdstarter::PrototypeService& service,
                            const QVector<xdstarter::LaunchTarget>& entries, int fixtureTiles = tileCount) {
    auto snapshot = service.snapshot();
    QJsonArray targets;
    QJsonArray tiles;
    for (int i = 0; i < fixtureTiles; ++i) {
        targets.append(xdstarter::applicationTargetJson(entries.at(i)));
        tiles.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("tile-%1").arg(i)},
                                 {QStringLiteral("targetId"), entries.at(i).id},
                                 {QStringLiteral("column"), i % 10},
                                 {QStringLiteral("row"), i / 10},
                                 {QStringLiteral("size"), QStringLiteral("small")}});
    }
    snapshot.insert(QStringLiteral("targets"), targets);
    snapshot.insert(QStringLiteral("groups"), QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("benchmark")},
        {QStringLiteral("name"), QStringLiteral("Benchmark")},
        {QStringLiteral("tiles"), tiles}}});
    return snapshot;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 10000) {
    QElapsedTimer timeout;
    timeout.start();
    while (!predicate() && timeout.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}

QQuickItem* visualItem(QQuickItem* parent, const QString& name) {
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems()) {
        if (auto* found = visualItem(child, name))
            return found;
    }
    return nullptr;
}

using Clock = std::chrono::steady_clock;
struct FrameObservation {
    Clock::time_point start;
    std::atomic_bool synchronized{false};
    std::atomic<qint64> elapsedNanoseconds{-1};
};

QString graphicsApi(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    default: return QStringLiteral("Other");
    }
}

QJsonObject statistics(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto nearestRank = [&values](int percent) {
        const auto rank = (values.size() * static_cast<std::size_t>(percent) + 99) / 100;
        return values.at(std::max<std::size_t>(1, rank) - 1);
    };
    return {{QStringLiteral("p50Milliseconds"), nearestRank(50)},
            {QStringLiteral("p95Milliseconds"), nearestRank(95)},
            {QStringLiteral("maximumMilliseconds"), values.back()}};
}

} // namespace

int runUiSearchBenchmark() {
    const auto qpa = QGuiApplication::platformName();
    const bool hosted = qEnvironmentVariable("GITHUB_ACTIONS") == QLatin1String("true") &&
                        qEnvironmentVariable("RUNNER_ENVIRONMENT") == QLatin1String("github-hosted");
    const bool localNative = qEnvironmentVariable("XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE") == QLatin1String("1");
    const bool inputTiming = qEnvironmentVariable("XDSTARTER_UI_BENCHMARK_INPUT_TIMING") == QLatin1String("1");
    if (qpa != QLatin1String("offscreen") &&
        !(qpa == QLatin1String("windows") && (hosted || localNative))) {
        qCritical() << "Native benchmark windows require a hosted runner or explicit local opt-in.";
        return 1;
    }
    const auto executionMode = qpa == QLatin1String("offscreen") ? QStringLiteral("offscreen")
        : hosted ? QStringLiteral("hosted-native") : QStringLiteral("local-native");
    const auto outputPath = qEnvironmentVariable("XDSTARTER_UI_BENCHMARK_OUTPUT");
    if (outputPath.isEmpty() || !QFileInfo(outputPath).isAbsolute() || QFile::exists(outputPath)) {
        qCritical() << "Set XDSTARTER_UI_BENCHMARK_OUTPUT to a new absolute file path.";
        return 1;
    }
    bool countOk = true;
    const auto requested = qEnvironmentVariable("XDSTARTER_UI_BENCHMARK_SAMPLES");
    const int sampleCount = requested.isEmpty() ? 100 : requested.toInt(&countOk);
    if (!countOk || sampleCount < 1 || sampleCount > 100) {
        qCritical() << "The sample count must be between 1 and 100.";
        return 1;
    }
    QFile executable(QCoreApplication::applicationFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!executable.open(QIODevice::ReadOnly) || !hash.addData(&executable)) {
        qCritical() << "Cannot hash benchmark executable.";
        return 1;
    }
    QFile tokensFile(QStringLiteral(":/xdstarter/tokens/visual.json"));
    if (!tokensFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot read bundled tokens.";
        return 1;
    }
    const auto tokens = QJsonDocument::fromJson(tokensFile.readAll()).object().toVariantMap();
    if (tokens.isEmpty()) {
        qCritical() << "Invalid bundled tokens.";
        return 1;
    }

    const auto entries = syntheticEntries();
    const xdstarter::TileGrid grid;
    xdstarter::PrototypeService service(grid, {}, true);
    const auto restored = service.restoreSnapshot(syntheticLayout(service, entries));
    if (!restored.success) {
        qCritical() << "Synthetic layout rejected:" << restored.message;
        return 1;
    }
    SyntheticIntegration integration;
    xdstarter::ApplicationCatalog catalog(std::make_unique<SyntheticPlatform>(entries));
    xdstarter::MenuController controller(service, integration, tokens, nullptr, &catalog);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("application-icons"),
                            new xdstarter::ApplicationIconProvider(&catalog));
    QStringList qmlWarnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&qmlWarnings](const QList<QQmlError>& warnings) {
                         for (const auto& warning : warnings)
                             qmlWarnings.append(warning.toString());
                     });
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)},
                                 {QStringLiteral("visible"), false}});
    engine.loadFromModule("XDStarter", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Cannot load QML:" << qmlWarnings;
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        qCritical() << "QML root is not a window.";
        return 1;
    }
    window->show();
    if (!waitUntil([&] { return window->isExposed() && !catalog.busy() &&
                                catalog.applicationsModel()->rowCount() == catalogSize; })) {
        qCritical() << "Synthetic window or catalog did not become ready.";
        return 1;
    }
    auto* field = visualItem(window->contentItem(), QStringLiteral("searchField"));
    auto* list = visualItem(window->contentItem(), QStringLiteral("searchResultsList"));
    if (!field || !list) {
        qCritical() << "Search controls were not created.";
        return 1;
    }
    QJsonArray samples;
    std::vector<double> modelTimes;
    std::vector<double> frameTimes;
    std::vector<double> prefixTimes;
    std::vector<double> inputReturnTimes;
    std::vector<double> controllerTimes;
    std::vector<double> finalKeyToControllerTimes;
    std::vector<double> finalKeyToRowTimes;
    std::vector<double> controllerToRowTimes;
    for (int i = -warmupCount; i < sampleCount; ++i) {
        const int index = i < 0 ? 900 + i + warmupCount : 100 + i;
        const auto query = QStringLiteral("item%1").arg(index, 4, 10, QLatin1Char('0'));
        const auto expected = entries.at(index).id;
        field->setProperty("text", QString{});
        field->forceActiveFocus(Qt::OtherFocusReason);
        if (!waitUntil([&] { return field->hasActiveFocus() && controller.searchQuery().isEmpty(); })) {
            qCritical() << "Cannot focus or clear synthetic search field.";
            return 1;
        }
        Clock::time_point controllerReady;
        bool controllerReadySeen = false;
        qint64 controllerObserverNs = 0;
        qint64 rowObserverNs = 0;
        int controllerObserverCalls = 0;
        int rowObserverCalls = 0;
        const auto readyConnection = QObject::connect(&controller, &xdstarter::MenuController::searchChanged,
            &controller, [&] {
                const auto observerStart = inputTiming ? Clock::now() : Clock::time_point{};
                if (!controllerReadySeen && controller.searchQuery() == query && !controller.searchBusy()) {
                    const auto results = controller.searchResults();
                    if (results.size() == 1 && results.first().toMap().value(QStringLiteral("appId")) == expected) {
                        controllerReady = Clock::now();
                        controllerReadySeen = true;
                    }
                }
                if (inputTiming) {
                    ++controllerObserverCalls;
                    controllerObserverNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - observerStart).count();
                }
            });
        const auto started = Clock::now();
        Clock::time_point finalKeyStarted;
        std::array<std::array<Clock::time_point, 5>, 8> keyTimes{};
        for (qsizetype position = 0; position < query.size(); ++position) {
            if (position == query.size() - 1)
                finalKeyStarted = Clock::now();
            const char key = query.at(position).toLatin1();
            if (!inputTiming) {
                QTest::keyClick(window, key, Qt::NoModifier, 0);
                continue;
            }
            // Qt 6.8.2's QTest QWindow keyClick dispatches each key via
            // qt_handleKeyEvent followed by processEvents for press and release.
            // Keep this diagnostic path explicit and separate from the baseline.
            auto& times = keyTimes.at(static_cast<std::size_t>(position));
            const auto code = QTest::asciiToKey(key);
            const QString text(QChar::fromLatin1(key));
            times[0] = Clock::now();
            qt_handleKeyEvent(window, QEvent::KeyPress, code, Qt::NoModifier, text, false, 0);
            times[1] = Clock::now();
            QCoreApplication::processEvents();
            times[2] = Clock::now();
            qt_handleKeyEvent(window, QEvent::KeyRelease, code, Qt::NoModifier, text, false, 0);
            times[3] = Clock::now();
            QCoreApplication::processEvents();
            times[4] = Clock::now();
        }
        const auto inputReturned = Clock::now();
        const bool ready = waitUntil([&] {
            const auto observerStart = inputTiming ? Clock::now() : Clock::time_point{};
            const auto results = controller.searchResults();
            auto* row = visualItem(window->contentItem(), QStringLiteral("searchResult-") + expected);
            const bool found = controller.searchQuery() == query && !controller.searchBusy() &&
                   results.size() == 1 && results.first().toMap().value(QStringLiteral("appId")) == expected &&
                   list->property("count").toInt() == 1 && row && row->isVisible();
            if (inputTiming) {
                ++rowObserverCalls;
                rowObserverNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - observerStart).count();
            }
            return found;
        });
        const auto rowReady = Clock::now();
        QObject::disconnect(readyConnection);
        if (!ready) {
            qCritical() << "Search did not publish the expected visible row for" << query;
            return 1;
        }
        if (!controllerReadySeen) {
            qCritical() << "No controller readiness signal for" << query;
            return 1;
        }
        const auto elapsedMs = [](Clock::time_point end, Clock::time_point begin) {
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        const double modelMs = elapsedMs(rowReady, started);
        // Observe only a frame synchronized after the expected row is ready.
        // frameSwapped is not proof that pixels reached a physical display.
        auto frame = std::make_shared<FrameObservation>();
        frame->start = started;
        const auto syncConnection = QObject::connect(window, &QQuickWindow::afterSynchronizing, window,
            [frame] { frame->synchronized.store(true, std::memory_order_release); }, Qt::DirectConnection);
        const auto swapConnection = QObject::connect(window, &QQuickWindow::frameSwapped, window,
            [frame] {
                if (!frame->synchronized.load(std::memory_order_acquire))
                    return;
                qint64 unset = -1;
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - frame->start).count();
                frame->elapsedNanoseconds.compare_exchange_strong(unset, elapsed);
            }, Qt::DirectConnection);
        window->requestUpdate();
        const bool swapped = waitUntil([&] { return frame->elapsedNanoseconds.load() >= 0; });
        QObject::disconnect(syncConnection);
        QObject::disconnect(swapConnection);
        if (!swapped) {
            qCritical() << "No synchronized frame swap for" << query;
            return 1;
        }
        if (i >= 0) {
            const double frameMs = double(frame->elapsedNanoseconds.load()) / 1000000.0;
            const double prefixMs = elapsedMs(finalKeyStarted, started);
            const double inputReturnMs = elapsedMs(inputReturned, started);
            const double controllerMs = elapsedMs(controllerReady, started);
            const double finalKeyToControllerMs = elapsedMs(controllerReady, finalKeyStarted);
            const double finalKeyToRowMs = elapsedMs(rowReady, finalKeyStarted);
            const double controllerToRowMs = elapsedMs(rowReady, controllerReady);
            QJsonObject sample{{QStringLiteral("query"), query},
                                       {QStringLiteral("expectedAppId"), expected},
                                       {QStringLiteral("prefixInputMilliseconds"), prefixMs},
                                       {QStringLiteral("inputReturnMilliseconds"), inputReturnMs},
                                       {QStringLiteral("controllerReadyMilliseconds"), controllerMs},
                                       {QStringLiteral("finalKeyToControllerMilliseconds"), finalKeyToControllerMs},
                                       {QStringLiteral("finalKeyToRowMilliseconds"), finalKeyToRowMs},
                                       {QStringLiteral("controllerToRowMilliseconds"), controllerToRowMs},
                                       {QStringLiteral("modelReadyMilliseconds"), modelMs},
                                       {QStringLiteral("synchronizedFrameMilliseconds"), frameMs}};
            if (inputTiming) {
                QJsonArray keys;
                for (qsizetype position = 0; position < query.size(); ++position) {
                    const auto& t = keyTimes.at(static_cast<std::size_t>(position));
                    const auto ns = [](Clock::time_point end, Clock::time_point begin) {
                        return static_cast<qint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
                    };
                    keys.append(QJsonObject{{QStringLiteral("key"), QString(query.at(position))},
                        {QStringLiteral("pressDispatchNs"), ns(t[1], t[0])},
                        {QStringLiteral("pressProcessEventsNs"), ns(t[2], t[1])},
                        {QStringLiteral("releaseDispatchNs"), ns(t[3], t[2])},
                        {QStringLiteral("releaseProcessEventsNs"), ns(t[4], t[3])}});
                }
                sample.insert(QStringLiteral("inputTiming"), QJsonObject{
                    {QStringLiteral("keys"), keys},
                    {QStringLiteral("controllerObserverCalls"), controllerObserverCalls},
                    {QStringLiteral("controllerObserverNs"), controllerObserverNs},
                    {QStringLiteral("rowObserverCalls"), rowObserverCalls},
                    {QStringLiteral("rowObserverNs"), rowObserverNs}});
            }
            samples.append(sample);
            modelTimes.push_back(modelMs);
            frameTimes.push_back(frameMs);
            prefixTimes.push_back(prefixMs);
            inputReturnTimes.push_back(inputReturnMs);
            controllerTimes.push_back(controllerMs);
            finalKeyToControllerTimes.push_back(finalKeyToControllerMs);
            finalKeyToRowTimes.push_back(finalKeyToRowMs);
            controllerToRowTimes.push_back(controllerToRowMs);
        }
    }
    window->close();
    if (!qmlWarnings.isEmpty()) {
        qCritical() << "QML warnings:" << qmlWarnings;
        return 1;
    }
#ifdef _MSC_FULL_VER
    const QString compiler = QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#else
    const QString compiler = QStringLiteral("other");
#endif
#ifdef NDEBUG
    constexpr bool releaseNdebug = true;
#else
    constexpr bool releaseNdebug = false;
#endif
    const QJsonObject report{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("inputTimingMethod"), inputTiming ? QStringLiteral("qt-test-window-dispatch-phases")
                                                           : QStringLiteral("none")},
        {QStringLiteral("executionMode"), executionMode},
        {QStringLiteral("capturedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("scope"), QStringLiteral("synthetic catalog and layout; in-process Qt key events; no real input, desktop integration, application launch or user layout")},
        {QStringLiteral("boundary"), QStringLiteral("first simulated key, final key start, Qt key loop return, controller readiness signal, expected visible QML row, then explicitly requested synchronized frameSwapped; events and observers add unmeasured overhead; not physical presentation or first interactive frame")},
        {QStringLiteral("qpa"), qpa},
        {QStringLiteral("graphicsApi"), graphicsApi(window->rendererInterface()->graphicsApi())},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("compiler"), compiler},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("binarySha256"), QString::fromLatin1(hash.result().toHex())},
        {QStringLiteral("releaseNdebug"), releaseNdebug},
        {QStringLiteral("steadyClock"), Clock::is_steady},
        {QStringLiteral("entries"), catalogSize},
        {QStringLiteral("tiles"), tileCount},
        {QStringLiteral("warmups"), warmupCount},
        {QStringLiteral("measuredQueries"), sampleCount},
        {QStringLiteral("prefixInput"), statistics(prefixTimes)},
        {QStringLiteral("inputReturn"), statistics(inputReturnTimes)},
        {QStringLiteral("controllerReady"), statistics(controllerTimes)},
        {QStringLiteral("finalKeyToController"), statistics(finalKeyToControllerTimes)},
        {QStringLiteral("finalKeyToRow"), statistics(finalKeyToRowTimes)},
        {QStringLiteral("controllerToRow"), statistics(controllerToRowTimes)},
        {QStringLiteral("modelReady"), statistics(modelTimes)},
        {QStringLiteral("synchronizedFrame"), statistics(frameTimes)},
        {QStringLiteral("samples"), samples}};
    QFile output(outputPath);
    const auto bytes = QJsonDocument(report).toJson();
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(bytes) != bytes.size()) {
        qCritical() << "Cannot write benchmark report:" << output.errorString();
        return 1;
    }
    qInfo() << "Synthetic UI benchmark:" << samples.size() << "samples," << qpa << outputPath;
    return 0;
}

int runUiRevealBenchmark() {
    const auto fixtureStarted = Clock::now();
    const auto qpa = QGuiApplication::platformName();
    const bool hosted = qEnvironmentVariable("GITHUB_ACTIONS") == QLatin1String("true") &&
                        qEnvironmentVariable("RUNNER_ENVIRONMENT") == QLatin1String("github-hosted");
    if (qpa != QLatin1String("offscreen") && !(qpa == QLatin1String("windows") && hosted)) {
        qCritical() << "Reveal benchmark permits offscreen or hosted Windows only.";
        return 1;
    }
    const auto outputPath = qEnvironmentVariable("XDSTARTER_UI_REVEAL_OUTPUT");
    const auto eventsPath = qEnvironmentVariable("XDSTARTER_UI_REVEAL_EVENTS");
    if (outputPath.isEmpty() || eventsPath.isEmpty() || outputPath == eventsPath ||
        !QFileInfo(outputPath).isAbsolute() || !QFileInfo(eventsPath).isAbsolute() ||
        QFile::exists(outputPath) || QFile::exists(eventsPath)) {
        qCritical() << "Reveal benchmark requires two new absolute output paths.";
        return 1;
    }
    const auto emptyLayoutOption = qEnvironmentVariable("XDSTARTER_UI_REVEAL_EMPTY_LAYOUT");
    if (!emptyLayoutOption.isEmpty() && emptyLayoutOption != QLatin1String("1")) {
        qCritical() << "Invalid empty-layout option.";
        return 1;
    }
    const int fixtureTiles = emptyLayoutOption == QLatin1String("1") ? 0 : tileCount;
    bool countOk = true;
    const auto requested = qEnvironmentVariable("XDSTARTER_UI_REVEAL_SAMPLES");
    const bool firstOnly = qEnvironmentVariable("XDSTARTER_UI_REVEAL_FIRST_ONLY") == QLatin1String("1");
    const int sampleCount = firstOnly ? 0 : requested.isEmpty() ? 100 : requested.toInt(&countOk);
    const int revealWarmups = firstOnly ? 0 : warmupCount;
    if (!countOk || (!firstOnly && sampleCount < 1) || sampleCount > 100) {
        qCritical() << "Reveal benchmark sample count must be between 1 and 100.";
        return 1;
    }
    QFile binary(QCoreApplication::applicationFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!binary.open(QIODevice::ReadOnly) || !hash.addData(&binary)) {
        qCritical() << "Cannot hash reveal benchmark executable.";
        return 1;
    }
    QFile events(eventsPath);
    if (!events.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        qCritical() << "Cannot create reveal benchmark event stream.";
        return 1;
    }
    const auto writeEvent = [&](const QJsonObject& event) {
        const QByteArray bytes = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
        return events.write(bytes) == bytes.size() && events.flush();
    };
    QFile tokensFile(QStringLiteral(":/xdstarter/tokens/visual.json"));
    if (!tokensFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot read bundled tokens.";
        return 1;
    }
    const auto tokens = QJsonDocument::fromJson(tokensFile.readAll()).object().toVariantMap();
    if (tokens.isEmpty()) {
        qCritical() << "Invalid bundled tokens.";
        return 1;
    }
    const auto entries = syntheticEntries();
    const xdstarter::TileGrid grid;
    xdstarter::PrototypeService service(grid, {}, true);
    const auto restored = service.restoreSnapshot(syntheticLayout(service, entries, fixtureTiles));
    if (!restored.success) {
        qCritical() << "Synthetic layout rejected:" << restored.message;
        return 1;
    }
    const auto groups = service.previewGroups();
    if (groups.size() != 1 || groups.first().tiles.size() != fixtureTiles) {
        qCritical() << "Synthetic reveal layout has an unexpected number of tiles.";
        return 1;
    }
    SyntheticIntegration integration;
    xdstarter::ApplicationCatalog catalog(std::make_unique<SyntheticPlatform>(entries));
    xdstarter::MenuController controller(service, integration, tokens, nullptr, &catalog);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("application-icons"),
                            new xdstarter::ApplicationIconProvider(&catalog));
    QStringList qmlWarnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&qmlWarnings](const QList<QQmlError>& warnings) {
                         for (const auto& warning : warnings)
                             qmlWarnings.append(warning.toString());
                     });
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)},
                                 {QStringLiteral("visible"), false}});
    engine.loadFromModule("XDStarter", "Main");
    auto* window = engine.rootObjects().isEmpty() ? nullptr
        : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitUntil([&] { return !catalog.busy() &&
                                           catalog.applicationsModel()->rowCount() == catalogSize; })) {
        qCritical() << "Synthetic reveal window or catalog is unavailable:" << qmlWarnings;
        return 1;
    }
    auto* field = visualItem(window->contentItem(), QStringLiteral("searchField"));
    if (!field || window->isVisible()) {
        qCritical() << "Reveal benchmark did not start hidden with a search field.";
        return 1;
    }
    xdstarter::DesktopShellService shell(std::make_unique<SyntheticRevealAnchor>());
    SyntheticRevealPlacement placement;
    SyntheticRevealTray tray;
    xdstarter::DesktopWindowHost host(*window, shell, placement, tray);
    host.start(QImage(1, 1, QImage::Format_ARGB32_Premultiplied));
    const auto elapsedMs = [](Clock::time_point end, Clock::time_point begin) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    };
    QJsonObject first;
    QJsonArray samples;
    std::vector<double> readyTimes;
    std::vector<double> frameTimes;
    for (int round = -revealWarmups - 1; round < sampleCount; ++round) {
        const QString phase = round == -revealWarmups - 1 ? QStringLiteral("first")
            : round < 0 ? QStringLiteral("warmup") : QStringLiteral("warm");
        const int index = round == -revealWarmups - 1 ? 0 : round < 0 ? round + revealWarmups : round;
        if (window->isVisible()) {
            host.dismissWindow();
            if (!waitUntil([&] { return !window->isVisible(); })) {
                writeEvent({{QStringLiteral("phase"), phase}, {QStringLiteral("index"), index},
                            {QStringLiteral("failure"), QStringLiteral("hide-timeout")}});
                qCritical() << "Synthetic reveal window did not hide.";
                return 1;
            }
        }
        const auto started = Clock::now();
        host.showWindow();
        const bool ready = waitUntil([&] {
            return window->isVisible() && window->isExposed() && field->isVisible() &&
                   field->isEnabled() && field->hasActiveFocus() &&
                   !window->property("presentationSuspended").toBool();
        });
        const auto interactive = Clock::now();
        if (!ready) {
            writeEvent({{QStringLiteral("phase"), phase}, {QStringLiteral("index"), index},
                        {QStringLiteral("failure"), QStringLiteral("focus-ready-timeout")}});
            qCritical() << "Synthetic reveal did not reach focused, visible controls.";
            return 1;
        }
        auto frame = std::make_shared<FrameObservation>();
        frame->start = started;
        const auto syncConnection = QObject::connect(window, &QQuickWindow::afterSynchronizing, window,
            [frame] { frame->synchronized.store(true, std::memory_order_release); }, Qt::DirectConnection);
        const auto swapConnection = QObject::connect(window, &QQuickWindow::frameSwapped, window,
            [frame] {
                if (!frame->synchronized.load(std::memory_order_acquire))
                    return;
                qint64 unset = -1;
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - frame->start).count();
                frame->elapsedNanoseconds.compare_exchange_strong(unset, elapsed);
            }, Qt::DirectConnection);
        window->update();
        const bool swapped = waitUntil([&] { return frame->elapsedNanoseconds.load() >= 0; });
        QObject::disconnect(syncConnection);
        QObject::disconnect(swapConnection);
        if (!swapped) {
            writeEvent({{QStringLiteral("phase"), phase}, {QStringLiteral("index"), index},
                        {QStringLiteral("failure"), QStringLiteral("frame-timeout")}});
            qCritical() << "Synthetic reveal did not produce a synchronized frame.";
            return 1;
        }
        const double readyMs = elapsedMs(interactive, started);
        const double frameMs = frame->elapsedNanoseconds.load() / 1'000'000.0;
        QTest::keyClick(window, Qt::Key_X);
        const bool accepted = waitUntil([&] { return field->property("text").toString() == QLatin1String("x"); });
        field->setProperty("text", QString{});
        const bool cleared = waitUntil([&] { return controller.searchQuery().isEmpty() && !controller.searchBusy(); });
        if (!accepted || !cleared || frameMs < readyMs) {
            writeEvent({{QStringLiteral("phase"), phase}, {QStringLiteral("index"), index},
                        {QStringLiteral("failure"), QStringLiteral("input-or-order")}});
            qCritical() << "Synthetic reveal failed its input or frame-order check.";
            return 1;
        }
        QJsonObject sample{{QStringLiteral("phase"), phase}, {QStringLiteral("index"), index},
                           {QStringLiteral("focusReadyMilliseconds"), readyMs},
                           {QStringLiteral("synchronizedFrameMilliseconds"), frameMs},
                           {QStringLiteral("syntheticKeyAccepted"), true}};
        if (phase == QLatin1String("first"))
            sample.insert(QStringLiteral("fixtureToFocusMilliseconds"), elapsedMs(interactive, fixtureStarted));
        if (!writeEvent(sample)) {
            qCritical() << "Cannot persist reveal benchmark sample.";
            return 1;
        }
        if (phase == QLatin1String("first")) {
            first = sample;
        } else if (phase == QLatin1String("warm")) {
            samples.append(sample);
            readyTimes.push_back(readyMs);
            frameTimes.push_back(frameMs);
        }
    }
    const auto api = graphicsApi(window->rendererInterface()->graphicsApi());
    host.shutdown();
    if (!qmlWarnings.isEmpty()) {
        qCritical() << "QML warnings:" << qmlWarnings;
        return 1;
    }
#ifdef _MSC_FULL_VER
    const QString compiler = QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#else
    const QString compiler = QStringLiteral("other");
#endif
#ifdef NDEBUG
    constexpr bool releaseNdebug = true;
#else
    constexpr bool releaseNdebug = false;
#endif
    const QJsonObject report{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("scope"), QStringLiteral("real DesktopWindowHost with synthetic anchor, placement, tray and catalog; no system hooks or real user input")},
        {QStringLiteral("boundary"), QStringLiteral("host show request to exposed focused enabled search field, then a synchronized frame and a separately verified synthetic key; first reveal follows initialized QML and catalog; not physical presentation or cold OS startup")},
        {QStringLiteral("capturedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("qpa"), qpa}, {QStringLiteral("graphicsApi"), api},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("compiler"), compiler}, {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("binarySha256"), QString::fromLatin1(hash.result().toHex())},
        {QStringLiteral("releaseNdebug"), releaseNdebug}, {QStringLiteral("steadyClock"), Clock::is_steady},
        {QStringLiteral("entries"), catalogSize}, {QStringLiteral("tiles"), fixtureTiles},
        {QStringLiteral("warmups"), revealWarmups}, {QStringLiteral("measuredReveals"), sampleCount},
        {QStringLiteral("firstReveal"), first},
        {QStringLiteral("focusReady"), sampleCount ? QJsonValue(statistics(readyTimes)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("synchronizedFrame"), sampleCount ? QJsonValue(statistics(frameTimes)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("samples"), samples}};
    QFile output(outputPath);
    const auto bytes = QJsonDocument(report).toJson();
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(bytes) != bytes.size()) {
        qCritical() << "Cannot write reveal benchmark report:" << output.errorString();
        return 1;
    }
    qInfo() << "Synthetic host reveal benchmark:" << samples.size() << "warm samples," << qpa << outputPath;
    return 0;
}
