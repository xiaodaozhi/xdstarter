// src/experimental/start_bridge/bridge_controller.cpp

#include "experimental/start_bridge/session_server.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <string_view>

class BridgeController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(qulonglong toggles READ toggles NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
  public:
    explicit BridgeController(QObject* parent = nullptr) : QObject(parent) {
        timer_.setInterval(50);
        connect(&timer_, &QTimer::timeout, this, &BridgeController::refresh);
        timer_.start();
    }
    ~BridgeController() override { closeControlledFixture(); }
    QString status() const { return status_; }
    qulonglong toggles() const { return session_ ? session_->observe().toggles : 0; }
    bool running() const { return session_ != nullptr; }
    Q_INVOKABLE void startFixture() {
        if (fixture_.state() != QProcess::NotRunning || session_)
            return;
        const auto directory = QCoreApplication::applicationDirPath();
        fixture_.start(directory + QStringLiteral("/xdstarter_start_bridge_fixture.exe"),
                       {QStringLiteral("--controlled"), QStringLiteral("--visible")});
        if (!fixture_.waitForStarted(1000)) {
            status_ = QStringLiteral("受控替身启动失败。");
            emit changed();
            return;
        }
        fixtureIdentity_ = {};
        fixtureIdentity_.processId = static_cast<DWORD>(fixture_.processId());
        const auto began = GetTickCount64();
        while (GetTickCount64() - began < 1500) {
            EnumWindows(
                [](HWND window, LPARAM parameter) -> BOOL {
                    auto& identity =
                        *reinterpret_cast<xdstarter::bridge_poc::ProcessIdentity*>(parameter);
                    DWORD process = 0;
                    const DWORD thread = GetWindowThreadProcessId(window, &process);
                    wchar_t name[96]{};
                    if (process == identity.processId && GetClassNameW(window, name, 96) &&
                        wcscmp(name, xdstarter::bridge_poc::fixtureClass) == 0) {
                        identity.threadId = thread;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&fixtureIdentity_));
            if (fixtureIdentity_.threadId)
                break;
            Sleep(5);
        }
        session_ = std::make_unique<xdstarter::bridge_poc::SessionServer>();
        if (!session_->start(
                (directory + QStringLiteral("/xdstarter_start_bridge_poc.dll")).toStdWString(),
                fixtureIdentity_.processId, fixtureIdentity_.threadId)) {
            status_ = QString::fromStdWString(session_->error());
            session_.reset();
            fixture_.kill();
            fixture_.waitForFinished(1000);
        } else {
            status_ = QStringLiteral(
                "正在连接受控替身。真实 Explorer InputSite 未适配，不会注入 Explorer。");
        }
        emit changed();
    }
    Q_INVOKABLE void mousePair() { command(xdstarter::bridge_poc::FixtureCommand::MousePair); }
    Q_INVOKABLE void pointerProbe() {
        command(xdstarter::bridge_poc::FixtureCommand::PointerTransportProbe);
    }
    Q_INVOKABLE void recreate() { command(xdstarter::bridge_poc::FixtureCommand::Recreate); }
    Q_INVOKABLE void pause(bool paused) {
        if (session_)
            session_->setEnabled(!paused);
    }
    Q_INVOKABLE void pauseHeartbeat(bool paused) {
        if (session_)
            session_->pauseHeartbeats(paused);
    }
    Q_INVOKABLE bool requestClose() {
        if (session_ && !session_->stop()) {
            status_ = QString::fromStdWString(session_->error());
            emit changed();
            return false;
        }
        closeControlledFixture();
        return true;
    }
  signals:
    void changed();

  private:
    void command(xdstarter::bridge_poc::FixtureCommand command) {
        if (session_)
            if (const HWND window = session_->window())
                PostMessageW(window, xdstarter::bridge_poc::fixtureCommandMessage,
                             static_cast<WPARAM>(command), 0);
    }
    void refresh() {
        if (session_) {
            const auto observed = session_->observe();
            status_ =
                QStringLiteral("受控替身 / IPC %1 / 输入桥接 %2 / 持有手势 %3 / 回调 %4\n真实 "
                               "Explorer 与硬件触控尚未验证。")
                    .arg(observed.connected ? QStringLiteral("已连接") : QStringLiteral("未连接"))
                    .arg(observed.ready ? QStringLiteral("已启用") : QStringLiteral("原生放行"))
                    .arg(observed.ownsPress ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(observed.callbacks);
            if (const HWND target = session_->window()) {
                using namespace xdstarter::bridge_poc;
                const auto posts =
                    reinterpret_cast<UINT_PTR>(GetPropW(target, pointerPostsProperty));
                if (posts)
                    status_ += QStringLiteral("\n原生指针投递 %1 次，成功 %2 次，错误码 %3。")
                                   .arg(posts)
                                   .arg(reinterpret_cast<UINT_PTR>(
                                       GetPropW(target, pointerPostsSucceededProperty)))
                                   .arg(reinterpret_cast<UINT_PTR>(
                                       GetPropW(target, pointerPostErrorProperty)));
            }
        }
        emit changed();
    }
    void closeControlledFixture() {
        if (fixture_.state() != QProcess::NotRunning) {
            command(xdstarter::bridge_poc::FixtureCommand::Exit);
            if (!fixture_.waitForFinished(500)) {
                fixture_.kill(); // This QProcess was created only by startFixture().
                fixture_.waitForFinished(1000);
            }
        }
        session_.reset();
    }
    QTimer timer_;
    QProcess fixture_;
    std::unique_ptr<xdstarter::bridge_poc::SessionServer> session_;
    xdstarter::bridge_poc::ProcessIdentity fixtureIdentity_{};
    QString status_ = QStringLiteral("实验未启动。仅支持本实验创建的受控 InputSite 替身。");
};

int main(int argc, char** argv) {
    bool smoke = false;
    for (int index = 1; index < argc; ++index)
        smoke = smoke || std::string_view(argv[index]) == "--smoke-test";
    if (smoke) {
        std::fputs("Bridge smoke: initializing GUI\n", stderr);
        std::fflush(stderr);
    }
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    if (smoke) {
        std::fputs("Bridge smoke: loading QML\n", stderr);
        std::fflush(stderr);
    }
    bool hadWarnings = false;
    BridgeController controller;
    QQmlApplicationEngine engine;
    if (smoke)
        QObject::connect(&engine, &QQmlEngine::warnings, &application,
                         [&hadWarnings] { hadWarnings = true; });
    engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &controller);
    engine.loadData(R"QML(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    visible: true; width: 640; height: 420
    title: "XDStarter 输入桥接实验 B"
    font.family: "Microsoft YaHei UI"
    onClosing: close => { close.accepted = bridge.requestClose() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        Label { text: "独立 Qt 界面 / 无 Qt 的桥接 DLL"; font.pixelSize: 22 }
        Label { text: bridge.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: "完整手势派发次数：" + bridge.toggles; font.pixelSize: 20 }
        Button { text: "启动受控替身"; enabled: !bridge.running; onClicked: bridge.startFixture() }
        RowLayout {
            Button { text: "发送受控鼠标消息"; enabled: bridge.running; onClicked: bridge.mousePair() }
            Button { text: "诊断原生指针投递"; enabled: bridge.running; onClicked: bridge.pointerProbe() }
        }
        RowLayout {
            CheckBox { text: "暂停接管"; onToggled: bridge.pause(checked) }
            CheckBox { text: "暂停心跳"; onToggled: bridge.pauseHeartbeat(checked) }
            Button { text: "重建替身窗口"; enabled: bridge.running; onClicked: bridge.recreate() }
        }
        Label { text: "消息测试不代表真实 Explorer 或物理触控通过。"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
    }
}
)QML");
    if (engine.rootObjects().isEmpty())
        return 1;
    if (smoke) {
        std::fputs("Bridge smoke: running event loop\n", stderr);
        std::fflush(stderr);
    }
    if (smoke)
        QTimer::singleShot(250, &application,
                           [&hadWarnings] { QCoreApplication::exit(hadWarnings ? 1 : 0); });
    return application.exec();
}

#include "bridge_controller.moc"
