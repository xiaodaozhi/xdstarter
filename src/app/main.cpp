#include "application_icon_provider.h"
#include "build_identity.h"
#include "desktop_window_host.h"
#include "interface_font.h"
#include "menu_controller.h"
#include "runtime_support.h"
#include "scene_graph_failure_guard.h"
#include "scene_graph_failure_notice.h"

#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/graphics_failure_notice.h"
#include "platform/installation_guard.h"
#include "platform/platform_integration.h"
#include "platform/motion_preference.h"
#include "platform/contrast_preference.h"
#include "platform/start_button_capture.h"
#include "platform/win_key_capture.h"
#ifdef Q_OS_WIN
#include "platform/windows/native_picker_protocol.h"
#endif
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/layout_store.h"
#include "services/prototype_service.h"
#include "services/session_instance.h"

#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QProcess>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWindow>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>

namespace {

std::optional<QVariantMap> loadVisualTokens() {
    QFile file(QStringLiteral(":/xdstarter/tokens/visual.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot open bundled visual tokens:" << file.errorString();
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCritical() << "Cannot parse bundled visual tokens:" << error.errorString();
        return std::nullopt;
    }

    const auto object = document.object();
    const auto brand = object.value(QStringLiteral("brand")).toObject();
    const auto themes = object.value(QStringLiteral("themes")).toObject();
    const auto radius = object.value(QStringLiteral("cornerRadiusDip")).toObject();
    const auto menuSize = object.value(QStringLiteral("defaultMenuSizeDip")).toObject();
    bool valid = radius.value(QStringLiteral("menu")).toDouble() > 0 &&
                 radius.value(QStringLiteral("tile")).toDouble() > 0 &&
                 menuSize.value(QStringLiteral("width")).toDouble() > 0 &&
                 menuSize.value(QStringLiteral("height")).toDouble() > 0 &&
                 object.value(QStringLiteral("tileLabelFontDip")).toDouble() > 0;
    for (const auto* key : {"blue", "cyan", "ink"}) {
        valid = valid && QColor(brand.value(QLatin1String(key)).toString()).isValid();
    }
    for (const auto* themeName : {"dark", "light"}) {
        const auto theme = themes.value(QLatin1String(themeName)).toObject();
        for (const auto* key : {"foreground", "secondary", "opaqueFallback", "focusAccent", "focusControl", "focusTileInner"}) {
            valid = valid && QColor(theme.value(QLatin1String(key)).toString()).isValid();
        }
    }
    if (!valid) {
        qCritical() << "Bundled visual tokens are missing required theme or geometry values";
        return std::nullopt;
    }
    return object.toVariantMap();
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String(xdstarter::native_picker::argument))
        return xdstarter::native_picker::runHelper(argv[2]);
#endif
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) ==
                         QLatin1String(xdstarter::graphicsFailureNoticeArgument)) {
        return xdstarter::runGraphicsFailureNotice();
    }
    // Identity queries neither initialize the platform plugin nor activate a resident instance.
    if (argc == 2 && (QString::fromLocal8Bit(argv[1]) == QLatin1String("--version") ||
                      QString::fromLocal8Bit(argv[1]) == QLatin1String("--build-info"))) {
        if (QString::fromLocal8Bit(argv[1]) == QLatin1String("--version")) {
            std::cout << "XDStarter " << xdstarter::build::version << " (build "
                      << xdstarter::build::number << ", " << xdstarter::build::revision
                      << (xdstarter::build::dirty ? ", dirty" : "") << ")\n";
        } else {
            const QJsonObject info{
                {QStringLiteral("version"), QLatin1String(xdstarter::build::version)},
                {QStringLiteral("revision"), QLatin1String(xdstarter::build::revision)},
                {QStringLiteral("build"), xdstarter::build::number},
                {QStringLiteral("dirty"), xdstarter::build::dirty},
                {QStringLiteral("distribution"), xdstarter::build::distribution},
                {QStringLiteral("qt"), QStringLiteral(QT_VERSION_STR)},
                {QStringLiteral("layoutSchema"), xdstarter::LayoutStore::schemaVersion}};
            std::cout << QJsonDocument(info).toJson(QJsonDocument::Compact).constData() << '\n';
        }
        return EXIT_SUCCESS;
    }
    bool traceSmoke = false;
    for (int index = 1; index < argc; ++index)
        traceSmoke |= QString::fromLocal8Bit(argv[index]) == QLatin1String("--smoke-test");
    QElapsedTimer smokeClock;
    smokeClock.start();
    const auto smokeStage = [&](const char* stage) {
        if (traceSmoke)
            std::cerr << "Smoke stage " << stage << " " << smokeClock.elapsed() << " ms" << std::endl;
    };
    smokeStage("before-application");
    QGuiApplication application(argc, argv);
    smokeStage("application-ready");
    QGuiApplication::setOrganizationName(QStringLiteral("xiaodaozhi"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.com/xiaodaozhi"));
    QGuiApplication::setApplicationName(QStringLiteral("XDStarter"));
    QGuiApplication::setApplicationVersion(QStringLiteral(XDSTARTER_VERSION));
    xdstarter::configureInterfaceFont();
    smokeStage("font-ready");
    QIcon applicationIcon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024}) {
        const auto resource = QStringLiteral(":/xdstarter/app/xdstarter-app-icon-%1.png").arg(size);
        const QImage image(resource);
        if (image.size() != QSize(size, size)) {
            qCritical() << "Cannot load bundled application icon:" << resource;
            return EXIT_FAILURE;
        }
        applicationIcon.addFile(resource, QSize(size, size));
    }
    QGuiApplication::setWindowIcon(applicationIcon);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XDStarter Qt Quick 菜单弹层原型"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("加载界面后短暂运行并退出，用于资源 smoke 检查。"));
    parser.addOption(smokeTestOption);
    const QCommandLineOption layoutFileOption(
        QStringLiteral("layout-file"),
        QStringLiteral("使用指定布局文件，便于隔离测试；默认保存到当前用户应用数据目录。"),
        QStringLiteral("path"));
    parser.addOption(layoutFileOption);
    const QCommandLineOption captureModeOption(
        QStringLiteral("start-capture-mode"),
        QStringLiteral("开始按钮实验模式：point 或 area-cache；默认 point。"),
        QStringLiteral("mode"), QStringLiteral("point"));
    const QCommandLineOption captureDiagnosticsOption(
        QStringLiteral("start-capture-diagnostics"),
        QStringLiteral("定期将有界接管诊断写入指定文件，不记录应用列表或完整输入轨迹。"),
        QStringLiteral("path"));
    parser.addOption(captureModeOption);
    parser.addOption(captureDiagnosticsOption);
    const QCommandLineOption captureDurationOption(
        QStringLiteral("start-capture-duration"),
        QStringLiteral("诊断实验限时1至900秒，到期正常退出；须同时指定诊断文件。"),
        QStringLiteral("seconds"));
    parser.addOption(captureDurationOption);
    parser.process(application);
    bool durationValid = true;
    const int captureDuration = parser.isSet(captureDurationOption)
                                    ? parser.value(captureDurationOption).toInt(&durationValid)
                                    : 0;
    if (parser.isSet(captureDurationOption) &&
        (!durationValid || captureDuration < 1 || captureDuration > 900 ||
         !parser.isSet(captureDiagnosticsOption))) {
        qCritical() << "Capture duration requires diagnostics and a duration of 1..900 seconds";
        return EXIT_FAILURE;
    }
    const QString captureMode = parser.value(captureModeOption);
    if (captureMode != QLatin1String("point") && captureMode != QLatin1String("area-cache")) {
        qCritical() << "Unknown start capture mode:" << captureMode;
        return EXIT_FAILURE;
    }
    if (parser.isSet(smokeTestOption) &&
        (parser.isSet(captureModeOption) || parser.isSet(captureDiagnosticsOption) ||
         parser.isSet(captureDurationOption))) {
        qCritical() << "Start capture experiments cannot run in smoke mode";
        return EXIT_FAILURE;
    }

    smokeStage("before-installation-guard");
    auto installationGuard = xdstarter::createInstallationGuard();
    if (!installationGuard->acquire()) {
        qCritical() << installationGuard->errorString();
        return EXIT_FAILURE;
    }
    smokeStage("installation-guard-ready");
    xdstarter::initializeQtTranslation();
    std::unique_ptr<xdstarter::SessionInstance> instance;
    bool pendingActivation = false;
    if (!parser.isSet(smokeTestOption)) {
        instance = std::make_unique<xdstarter::SessionInstance>(
            xdstarter::createPlatformIntegration()->sessionKey(),
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        instance->setForegroundGrant([](qint64 ownerPid) {
            xdstarter::createPlatformIntegration()->allowForegroundActivation(ownerPid);
        });
        const auto result = instance->acquire();
        if (result == xdstarter::SessionInstance::Result::ActivatedExisting) {
            if (parser.isSet(captureModeOption) || parser.isSet(captureDiagnosticsOption))
                qWarning() << "An existing instance keeps its capture settings; exit it before"
                              " starting a capture experiment";
            return EXIT_SUCCESS;
        }
        if (result == xdstarter::SessionInstance::Result::Failed) {
            qCritical() << instance->errorString();
            return EXIT_FAILURE;
        }
        QObject::connect(instance.get(), &xdstarter::SessionInstance::activationRequested,
                         &application, [&pendingActivation] { pendingActivation = true; });
    }

    const auto visualTokens = loadVisualTokens();
    if (!visualTokens) {
        return EXIT_FAILURE;
    }

    const xdstarter::TileGrid grid;
    QString layoutPath;
    if (!parser.isSet(smokeTestOption)) {
        if (parser.isSet(layoutFileOption)) {
            layoutPath = QFileInfo(parser.value(layoutFileOption)).absoluteFilePath();
        } else {
            const auto directory =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            if (directory.isEmpty()) {
                qCritical() << "Cannot determine the current user's layout data directory";
                return EXIT_FAILURE;
            }
            layoutPath = QDir(directory).filePath(QStringLiteral("layout.json"));
        }
    }
    xdstarter::PrototypeService prototypeService(grid, layoutPath, parser.isSet(smokeTestOption));
    std::unique_ptr<xdstarter::ApplicationCatalog> catalog;
    std::unique_ptr<xdstarter::DesktopShellService> desktopShell;
    if (!parser.isSet(smokeTestOption)) {
        catalog =
            std::make_unique<xdstarter::ApplicationCatalog>(xdstarter::createApplicationPlatform());
        desktopShell =
            std::make_unique<xdstarter::DesktopShellService>(xdstarter::createDesktopPlatform());
    }
    const auto platform = xdstarter::createPlatformIntegration();
    smokeStage("before-motion-preference");
    const auto motionPreference = xdstarter::createMotionPreference();
    smokeStage("motion-preference-ready");
    smokeStage("before-contrast-preference");
    const auto contrastPreference = xdstarter::createContrastPreference();
    smokeStage("contrast-preference-ready");
    xdstarter::MenuController controller(prototypeService, *platform, *visualTokens, nullptr,
                                         catalog.get(), desktopShell.get(), motionPreference.get(),
                                         contrastPreference.get());
    QObject::connect(&controller, &xdstarter::MenuController::exitRequested, &application,
                     [&application] { application.quit(); });
    bool smokeHadWarnings = false;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("application-icons"),
                            new xdstarter::ApplicationIconProvider(catalog.get()));
    if (parser.isSet(smokeTestOption)) {
        QObject::connect(&engine, &QQmlEngine::warnings, &application,
                         [&smokeHadWarnings] { smokeHadWarnings = true; });
    }
    // Connect the scene graph failure handler before either smoke or resident
    // mode can expose the window. Otherwise Qt may enter a blocking native
    // error dialog before the application can release its input captures.
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)},
                                 {QStringLiteral("visible"), false}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    smokeStage("before-qml-load");
    engine.loadFromModule("XDStarter", "Main");
    smokeStage("qml-loaded");

    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        qCritical() << "Root QML object is not a Qt Quick window";
        return EXIT_FAILURE;
    }
    QObject::connect(window, &QWindow::visibleChanged, motionPreference.get(),
                     [source = motionPreference.get(), &smokeStage](bool visible) {
                         if (visible) {
                             smokeStage("before-visible-motion-refresh");
                             source->refresh();
                             smokeStage("visible-motion-refreshed");
                         }
                     });
    QObject::connect(window, &QWindow::visibleChanged, contrastPreference.get(),
                     [source = contrastPreference.get(), &smokeStage](bool visible) {
                         if (visible) {
                             smokeStage("before-visible-contrast-refresh");
                             source->refresh();
                             smokeStage("visible-contrast-refreshed");
                         }
                     });
    xdstarter::SceneGraphFailureGuard graphicsFailure(*window, application);
    window->setIcon(applicationIcon);
    std::unique_ptr<xdstarter::DesktopPlatform> placement;
    std::unique_ptr<xdstarter::DesktopTray> tray;
    std::unique_ptr<xdstarter::WinKeyCapture> winKeyCapture;
    std::unique_ptr<xdstarter::WinKeyCapture> startButtonCapture;
    std::unique_ptr<xdstarter::DesktopWindowHost> desktopHost;
    xdstarter::SceneGraphFailureNotice graphicsNotice(
        application, graphicsFailure, !parser.isSet(smokeTestOption),
        [&desktopHost] {
            if (desktopHost)
                desktopHost->shutdown();
        },
        [] {
            qint64 pid = 0;
            const bool started = QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                {QLatin1String(xdstarter::graphicsFailureNoticeArgument)}, {}, &pid);
            if (started)
                qInfo() << "Graphics failure notice launched:" << pid;
            return started;
        });
    QTimer captureDiagnosticsTimer;
    quint64 captureDiagnosticsWriteFailures = 0;
    bool captureDiagnosticsFailureReported = false;
    std::function<void()> finalCaptureDiagnostics;
    if (desktopShell) {
        placement = xdstarter::createDesktopPlatform();
        tray = xdstarter::createDesktopTray();
        winKeyCapture = xdstarter::createWinKeyCapture();
        xdstarter::StartButtonCaptureOptions captureOptions;
        if (captureMode == QLatin1String("area-cache"))
            captureOptions.mode = xdstarter::StartButtonCaptureMode::ExperimentalAreaCache;
        startButtonCapture = xdstarter::createStartButtonCapture(captureOptions);
        if (parser.isSet(captureDiagnosticsOption)) {
            const QString diagnosticsPath =
                QFileInfo(parser.value(captureDiagnosticsOption)).absoluteFilePath();
            const auto runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QFile executable(QCoreApplication::applicationFilePath());
            QCryptographicHash hash(QCryptographicHash::Sha256);
            const auto executableHash =
                executable.open(QIODevice::ReadOnly) && hash.addData(&executable)
                    ? QString::fromLatin1(hash.result().toHex())
                    : QString();
            const auto writeDiagnostics = [&, diagnosticsPath, runId, executableHash] {
                auto snapshot =
                    xdstarter::startButtonCaptureDiagnosticSnapshot(*startButtonCapture);
                snapshot.insert(QStringLiteral("applicationVersion"),
                                QGuiApplication::applicationVersion());
                snapshot.insert(QStringLiteral("mode"), captureMode);
                snapshot.insert(QStringLiteral("runId"), runId);
                snapshot.insert(QStringLiteral("processId"), QCoreApplication::applicationPid());
                snapshot.insert(QStringLiteral("executableSha256"), executableHash);
                snapshot.insert(QStringLiteral("diagnosticWriteFailures"),
                                static_cast<double>(captureDiagnosticsWriteFailures));
                const auto bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
                QSaveFile output(diagnosticsPath);
                output.setDirectWriteFallback(false);
                if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
                    !output.commit()) {
                    ++captureDiagnosticsWriteFailures;
                    if (!captureDiagnosticsFailureReported)
                        qWarning()
                            << "Cannot write start capture diagnostics:" << output.errorString();
                    captureDiagnosticsFailureReported = true;
                    // A reader can briefly prevent atomic replacement on Windows.
                    // Keep the next timer attempt; never publish partial JSON.
                    return;
                }
                captureDiagnosticsFailureReported = false;
            };
            captureDiagnosticsTimer.setInterval(250);
            QObject::connect(&captureDiagnosticsTimer, &QTimer::timeout, &application,
                             writeDiagnostics);
            finalCaptureDiagnostics = writeDiagnostics;
            captureDiagnosticsTimer.start();
            if (captureDuration)
                QTimer::singleShot(captureDuration * 1000, &application, &QCoreApplication::quit);
        }
        desktopHost = std::make_unique<xdstarter::DesktopWindowHost>(*window, *desktopShell,
                                                                     *placement, *tray);
        desktopHost->attachWinKeyCapture(*winKeyCapture);
        desktopHost->attachStartButtonCapture(*startButtonCapture);
        QObject::connect(desktopHost.get(), &xdstarter::DesktopWindowHost::exitRequested,
                         &application, [&application] { application.quit(); });
        QObject::connect(&controller, &xdstarter::MenuController::launchSucceeded,
                         desktopHost.get(),
                         &xdstarter::DesktopWindowHost::keepReachableAfterLaunch);
        QObject::connect(&controller, &xdstarter::MenuController::desktopActionSucceeded,
                         desktopHost.get(),
                         &xdstarter::DesktopWindowHost::keepReachableAfterLaunch);
        QObject::connect(&application, &QCoreApplication::aboutToQuit, desktopHost.get(),
                         &xdstarter::DesktopWindowHost::shutdown);
        if (finalCaptureDiagnostics)
            QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
                             [&] { finalCaptureDiagnostics(); });
        desktopHost->start(QImage(QStringLiteral(":/xdstarter/app/xdstarter-app-icon-32.png")));
        QObject::connect(instance.get(), &xdstarter::SessionInstance::activationRequested,
                         desktopHost.get(), &xdstarter::DesktopWindowHost::showWindow);
        if (pendingActivation)
            desktopHost->showWindow();
    }
    // Release global input before waiting for the final layout write.
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &controller, [&controller] {
        if (!controller.readOnly() && !controller.flushSave()) {
            qWarning() << "Layout save failed:" << controller.persistenceStatus();
        }
    });
    bool smokeSceneGraphReady = false;
    if (parser.isSet(smokeTestOption)) {
        // The old fixed timer could report success while a slow renderer was
        // still initializing. Start it only after the scene graph is ready.
        QObject::connect(window, &QQuickWindow::sceneGraphInitialized, &application,
                         [&application, &smokeSceneGraphReady, &smokeHadWarnings, &smokeStage] {
                             if (smokeSceneGraphReady)
                                 return;
                             smokeSceneGraphReady = true;
                             smokeStage("scene-graph-ready");
                             QTimer::singleShot(250, &application, [&smokeHadWarnings] {
                                 QCoreApplication::exit(smokeHadWarnings ? EXIT_FAILURE
                                                                       : EXIT_SUCCESS);
                             });
                         }, Qt::QueuedConnection);
        QTimer::singleShot(10000, &application, [&smokeSceneGraphReady] {
            if (!smokeSceneGraphReady) {
                qCritical() << "Graphics initialization did not finish during the smoke test";
                QCoreApplication::exit(EXIT_FAILURE);
            }
        });
        smokeStage("before-window-show");
        window->setVisible(true);
        smokeStage("window-show-returned");
    }
    // exit() has no effect before exec(); also retain a failure if another
    // exit request is already pending when Qt reports the graphics error.
    if (graphicsFailure.failed()) {
        graphicsNotice.dispatch();
        return EXIT_FAILURE;
    }
    const int result = application.exec();
    smokeStage("event-loop-returned");
    graphicsNotice.dispatch();
    return graphicsFailure.failed() ? EXIT_FAILURE : result;
}
