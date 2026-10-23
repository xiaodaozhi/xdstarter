#include "application_icon_provider.h"
#include "desktop_window_host.h"
#include "interface_font.h"
#include "menu_controller.h"

#include "core/tile_grid.h"
#include "platform/application_platform.h"
#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"
#include "platform/platform_integration.h"
#include "platform/start_button_capture.h"
#include "platform/win_key_capture.h"
#include "services/application_catalog.h"
#include "services/desktop_shell_service.h"
#include "services/prototype_service.h"
#include "services/session_instance.h"

#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

#include <cstdlib>
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
    for (const auto *key : {"blue", "cyan", "ink"}) {
        valid = valid && QColor(brand.value(QLatin1String(key)).toString()).isValid();
    }
    for (const auto *themeName : {"dark", "light"}) {
        const auto theme = themes.value(QLatin1String(themeName)).toObject();
        for (const auto *key : {"foreground", "secondary", "opaqueFallback"}) {
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

int main(int argc, char *argv[]) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("xiaodaozhi"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.com/xiaodaozhi"));
    QGuiApplication::setApplicationName(QStringLiteral("XDStarter"));
    xdstarter::configureInterfaceFont();
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
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("加载界面后短暂运行并退出，用于资源 smoke 检查。"));
    parser.addOption(smokeTestOption);
    const QCommandLineOption layoutFileOption(
        QStringLiteral("layout-file"),
        QStringLiteral("使用指定布局文件，便于隔离测试；默认保存到当前用户应用数据目录。"),
        QStringLiteral("path"));
    parser.addOption(layoutFileOption);
    parser.process(application);

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
        if (result == xdstarter::SessionInstance::Result::ActivatedExisting)
            return EXIT_SUCCESS;
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
    xdstarter::MenuController controller(prototypeService, *platform, *visualTokens, nullptr,
                                         catalog.get(), desktopShell.get());
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
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)},
                                 {QStringLiteral("visible"), parser.isSet(smokeTestOption)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("XDStarter", "Main");

    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    auto *window = qobject_cast<QWindow *>(engine.rootObjects().first());
    if (!window) {
        qCritical() << "Root QML object is not a window";
        return EXIT_FAILURE;
    }
    window->setIcon(applicationIcon);
    std::unique_ptr<xdstarter::DesktopPlatform> placement;
    std::unique_ptr<xdstarter::DesktopTray> tray;
    std::unique_ptr<xdstarter::WinKeyCapture> winKeyCapture;
    std::unique_ptr<xdstarter::WinKeyCapture> startButtonCapture;
    std::unique_ptr<xdstarter::DesktopWindowHost> desktopHost;
    if (desktopShell) {
        placement = xdstarter::createDesktopPlatform();
        tray = xdstarter::createDesktopTray();
        winKeyCapture = xdstarter::createWinKeyCapture();
        startButtonCapture = xdstarter::createStartButtonCapture();
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
    if (parser.isSet(smokeTestOption)) {
        QTimer::singleShot(250, &application, [&smokeHadWarnings] {
            QCoreApplication::exit(smokeHadWarnings ? EXIT_FAILURE : EXIT_SUCCESS);
        });
    }
    return application.exec();
}
