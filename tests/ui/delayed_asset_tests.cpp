// Controlled image readiness experiment; no product window or system settings.
#include "ready_image_capture.h"
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QSemaphore>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <atomic>
#include <memory>

namespace {
struct Gate {
    QSemaphore release;
    std::atomic_bool entered{false};
    std::atomic_bool timedOut{false};
};

class DelayedProvider final : public QQuickImageProvider {
  public:
    explicit DelayedProvider(std::shared_ptr<Gate> gate)
        : QQuickImageProvider(QQuickImageProvider::Image, ForceAsynchronousImageLoading), gate_(std::move(gate)) {}

    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        gate_->entered.store(true, std::memory_order_release);
        if (!gate_->release.tryAcquire(1, 8000)) {
            gate_->timedOut.store(true, std::memory_order_release);
            return {};
        }
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QStringLiteral("#18d759")));
        if (size) *size = image.size();
        return image;
    }
  private:
    std::shared_ptr<Gate> gate_;
};

struct ReleaseOnExit {
    std::shared_ptr<Gate> gate;
    ~ReleaseOnExit() { gate->release.release(); }
};

QColor center(const QImage& image) {
    return image.isNull() ? QColor{} : image.pixelColor(image.width() / 2, image.height() / 2);
}
} // namespace

class DelayedAssetTests final : public QObject {
    Q_OBJECT
  private slots:
    void loadingFrameAndLaterReadyFrame();
    void captureWaitsForDelayedPixels();
};

void DelayedAssetTests::loadingFrameAndLaterReadyFrame() {
    auto gate = std::make_shared<Gate>();
    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("delayed-fixture"), new DelayedProvider(gate));
    QScopedPointer<QObject> root;
    // Declared after root so the provider is always released before window
    // destruction, even when an assertion returns early.
    ReleaseOnExit release{gate};
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        Window {
            width: 96; height: 96; visible: true; color: "#202020"
            Image {
                objectName: "delayedImage"
                x: 24; y: 24; width: 48; height: 48
                source: "image://delayed-fixture/probe"
                asynchronous: true; cache: false
            }
        }
    )", QUrl(QStringLiteral("inline:delayed-asset.qml")));
    QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    root.reset(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.data());
    QVERIFY(window);
    auto* item = window->findChild<QQuickItem*>(QStringLiteral("delayedImage"));
    QVERIFY(item);
    QSignalSpy swaps(window, &QQuickWindow::frameSwapped);
    QVERIFY(swaps.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5000);
    if (qEnvironmentVariableIntValue("XDSTARTER_DELAYED_ASSET_REQUIRE_D3D11") == 1) {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
        QCOMPARE(window->rendererInterface()->graphicsApi(), QSGRendererInterface::Direct3D11);
    }
    if (qEnvironmentVariableIntValue("XDSTARTER_DELAYED_ASSET_REQUIRE_SOFTWARE") == 1) {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QCOMPARE(window->rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
    }
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(std::memory_order_acquire), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(item->property("status").toInt(), 2, 5000); // Image.Loading
    // The initial frame can precede signal observation on a fast renderer.
    window->requestUpdate();
    QTRY_VERIFY_WITH_TIMEOUT(swaps.count() > 0, 2000);
    QCOMPARE(item->property("status").toInt(), 2);
    const auto early = window->grabWindow();
    QVERIFY(!early.isNull());
    QCOMPARE(center(early), QColor(QStringLiteral("#202020")));
    const int loadingSwaps = swaps.count();
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(item->property("status").toInt(), 1, 5000); // Image.Ready
    QVERIFY(!gate->timedOut.load(std::memory_order_acquire));
    const auto firstReady = window->grabWindow();
    QVERIFY(!firstReady.isNull());
    QImage final;
    const QColor expected(QStringLiteral("#18d759"));
    QTRY_VERIFY_WITH_TIMEOUT(([&] { final = window->grabWindow(); return center(final) == expected; })(), 5000);
    qInfo() << "Delayed asset frames: qpa=" << QGuiApplication::platformName()
            << "graphicsApi=" << int(window->rendererInterface()->graphicsApi())
            << "loadingSwaps=" << loadingSwaps << "totalSwaps=" << swaps.count()
            << "early=" << center(early) << "firstReady=" << center(firstReady)
            << "eventual=" << center(final);
    window->close();
}

void DelayedAssetTests::captureWaitsForDelayedPixels() {
    auto gate = std::make_shared<Gate>();
    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("delayed-fixture"), new DelayedProvider(gate));
    QScopedPointer<QObject> root;
    ReleaseOnExit release{gate};
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        Window {
            width: 96; height: 96; visible: true; color: "#202020"
            Image {
                objectName: "delayedImage"
                x: 24; y: 24; width: 48; height: 48
                source: "image://delayed-fixture/capture"
                asynchronous: true; cache: false
            }
        }
    )", QUrl(QStringLiteral("inline:delayed-capture.qml")));
    QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    root.reset(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.data());
    QVERIFY(window);
    auto* item = window->findChild<QQuickItem*>(QStringLiteral("delayedImage"));
    QVERIFY(item);
    QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(std::memory_order_acquire), 5000);
    QCOMPARE(item->property("status").toInt(), 2); // Image.Loading
    const QColor expected(QStringLiteral("#18d759"));
    const auto pixelsMatch = [expected](const QImage& image) { return center(image) == expected; };
    const auto blocked = captureReadyImage(window, item, pixelsMatch, 100);
    QCOMPARE(blocked.status, ReadyImageCaptureStatus::TimedOut);
    QVERIFY(blocked.image.isNull());
    QCOMPARE(center(window->grabWindow()), QColor(QStringLiteral("#202020")));
    // Ready is not sufficient: keep the loaded image invisible to model a
    // frame that does not yet contain its pixels.
    item->setOpacity(0);
    QTimer::singleShot(100, root.data(), [gate] { gate->release.release(); });
    QTRY_COMPARE_WITH_TIMEOUT(item->property("status").toInt(), 1, 5000);
    const auto notPainted = captureReadyImage(window, item, pixelsMatch, 100);
    QCOMPARE(notPainted.status, ReadyImageCaptureStatus::TimedOut);
    QVERIFY(notPainted.image.isNull());
    QTimer::singleShot(100, root.data(), [item] { item->setOpacity(1); });
    const auto captured = captureReadyImage(window, item, pixelsMatch);
    QCOMPARE(captured.status, ReadyImageCaptureStatus::Captured);
    QCOMPARE(item->property("status").toInt(), 1); // Image.Ready
    QCOMPARE(center(captured.image), expected);
    QVERIFY(!gate->timedOut.load(std::memory_order_acquire));
    window->close();
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    DelayedAssetTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "delayed_asset_tests.moc"
