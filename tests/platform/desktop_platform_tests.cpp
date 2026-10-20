#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <limits>
#include <string_view>

#ifdef Q_OS_WIN
#include <windows.h>

#include <dwmapi.h>
#endif

namespace {

class TestContext {
  public:
    void expect(bool condition, std::string_view description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }
    int failures = 0;
};

QJsonObject jsonRect(const QRect& rect) {
    return {{QStringLiteral("x"), rect.x()},
            {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}

// A native-free event source exercises the same queued session gate used by the
// Windows host. No Shell icon or desktop window is created by this fixture.
class QueuedTrayFixture final : public xdstarter::DesktopTray {
  public:
    xdstarter::DesktopActionResult start(const QImage&) override {
        beginEventSession();
        available_ = true;
        return {true, {}, {}};
    }
    void stop() override {
        endEventSession();
        available_ = false;
    }
    bool isAvailable() const override { return available_; }
    void showContextMenu() override {}
    void queueToggle() {
        queueSessionCallback([this] { emit toggleRequested(); });
    }
    void queueMenu() {
        queueSessionCallback([this] { emit menuRequested(QPoint(10, 20)); });
    }
    void queueRecovery() {
        queueSessionCallback(
            [this] { emit registrationChanged(true, QStringLiteral("recovered")); });
    }

  private:
    bool available_ = false;
};

// Keeps the base-platform fallback independently testable on Windows too.
class NativeFreePlatform final : public xdstarter::DesktopPlatform {
  public:
    xdstarter::DesktopActionResult openDesktopAction(const QString&) override { return {}; }
    xdstarter::DesktopAnchorResult queryStartButtonAnchor() override {
        ++anchorQueries;
        xdstarter::DesktopAnchorResult anchor;
        anchor.monitorName = QStringLiteral("native-free-fixture");
        anchor.workAreaPhysicalRect = QRect(-1920, 0, 1920, 1040);
        anchor.dpi = 144;
        return anchor;
    }
    int anchorQueries = 0;
};

#ifdef Q_OS_WIN
class HiddenNativeWindow {
  public:
    explicit HiddenNativeWindow(DWORD extendedStyle = 0, HWND parent = nullptr)
        : handle_(CreateWindowExW(extendedStyle, L"STATIC", L"XDStarter appearance fixture",
                                  parent ? WS_CHILD : WS_POPUP, 0, 0, 240, 160, parent, nullptr,
                                  GetModuleHandleW(nullptr), nullptr)) {}
    HiddenNativeWindow(const HiddenNativeWindow&) = delete;
    HiddenNativeWindow& operator=(const HiddenNativeWindow&) = delete;
    ~HiddenNativeWindow() {
        if (handle_) {
            DestroyWindow(handle_);
        }
    }
    HWND handle() const { return handle_; }

  private:
    HWND handle_ = nullptr;
};
#endif

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    auto platform = xdstarter::createDesktopPlatform();
    const bool probeMenu = application.arguments().contains(QStringLiteral("--probe-menu-anchor"));
    if (probeMenu || application.arguments().contains(QStringLiteral("--probe-anchor"))) {
        // Explicit read-only native probe, never part of unattended CTest. A
        // trustworthy fallback is a valid observation, not a fabricated pass.
        const auto anchor = probeMenu ? platform->queryMenuAnchor()
                                      : platform->queryStartButtonAnchor();
        const QJsonObject value{
            {QStringLiteral("trusted"), anchor.trusted},
            {QStringLiteral("query"), probeMenu ? QStringLiteral("menu-work-area")
                                                : QStringLiteral("start-button-uia")},
            {QStringLiteral("monitorName"), anchor.monitorName},
            {QStringLiteral("startButtonPhysicalRect"), jsonRect(anchor.startButtonPhysicalRect)},
            {QStringLiteral("monitorPhysicalRect"), jsonRect(anchor.monitorPhysicalRect)},
            {QStringLiteral("workAreaPhysicalRect"), jsonRect(anchor.workAreaPhysicalRect)},
            {QStringLiteral("dpi"), static_cast<int>(anchor.dpi)},
            {QStringLiteral("message"), anchor.message},
            {QStringLiteral("errorCode"), anchor.errorCode}};
        std::cout << QJsonDocument(value).toJson(QJsonDocument::Indented).constData();
        return 0;
    }

    TestContext test;
    for (const auto& action : {QStringLiteral("documents"), QStringLiteral("downloads"),
                               QStringLiteral("settings"), QStringLiteral("account")}) {
        test.expect(xdstarter::isSupportedDesktopAction(action), "known action is allowed");
    }
    for (const auto& action :
         {QString(), QStringLiteral("shutdown"), QStringLiteral("restart"),
          QStringLiteral("logout"), QStringLiteral("Documents"), QStringLiteral("documents\n"),
          QStringLiteral("ms-settings:"), QStringLiteral("account&cmd"),
          QStringLiteral("C:/Windows"), QStringLiteral("https://example.com")}) {
        test.expect(!xdstarter::isSupportedDesktopAction(action), "unknown input is rejected");
        const auto result = platform->openDesktopAction(action);
        test.expect(!result.success && result.errorCode == QStringLiteral("unsupported-action"),
                    "platform rejects non-allowlisted input without opening anything");
    }

    xdstarter::DesktopAnchorResult anchor;
    anchor.trusted = true;
    anchor.startButtonPhysicalRect = QRect(820, 1040, 48, 40);
    anchor.monitorPhysicalRect = QRect(0, 0, 1920, 1080);
    anchor.workAreaPhysicalRect = QRect(0, 0, 1920, 1040);
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(8, 432, 960, 600),
                "panel has equal left and bottom work-area margins");

    anchor.startButtonPhysicalRect.moveLeft(1800);
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(8, 432, 960, 600),
                "start button movement cannot change the panel position");

    anchor.trusted = false;
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(8, 432, 960, 600),
                "untrusted detection keeps exactly the same work-area margins");

    for (const int gap : {8, 10, 12, 14, 16}) {
        anchor.trusted = true;
        const QRect trusted = xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor, gap);
        anchor.trusted = false;
        const QRect fallback = xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor, gap);
        test.expect(trusted == fallback && trusted.left() == gap &&
                        anchor.workAreaPhysicalRect.bottom() - trusted.bottom() == gap,
                    "100/125/150/175/200 percent gaps are equal and stable across UIA trust");
    }
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor, -1) ==
                    QRect(0, 440, 960, 600),
                "negative physical gap is bounded to zero");
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor,
                                                   std::numeric_limits<int>::max()) ==
                    QRect(128, 312, 960, 600),
                "extreme positive physical gap is bounded without overflow");

    anchor.monitorPhysicalRect = QRect(-1920, -100, 1920, 1080);
    anchor.workAreaPhysicalRect = QRect(-1920, -100, 1920, 1040);
    anchor.startButtonPhysicalRect = QRect(-1600, 940, 48, 40);
    anchor.trusted = true;
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(-1912, 332, 960, 600),
                "negative desktop coordinates remain in the correct monitor work area");

    anchor.workAreaPhysicalRect = QRect(0, 40, 1920, 1040);
    anchor.startButtonPhysicalRect = QRect(200, 0, 48, 40);
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(8, 472, 960, 600),
                "top taskbar retains the same left and bottom work-area policy");

    anchor.workAreaPhysicalRect = QRect(48, 0, 1872, 1080);
    anchor.startButtonPhysicalRect = QRect(0, 100, 48, 48);
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(56, 472, 960, 600),
                "left taskbar popup stays within the work area");

    anchor.workAreaPhysicalRect = QRect(0, 0, 1872, 1080);
    anchor.startButtonPhysicalRect = QRect(1872, 100, 48, 48);
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor) ==
                    QRect(8, 472, 960, 600),
                "right taskbar popup stays within the work area");

    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(2400, 1600), anchor) ==
                    anchor.workAreaPhysicalRect,
                "oversized popup is bounded by the entire work area");
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(), anchor).isEmpty(),
                "invalid window dimensions are rejected");
    constexpr int maximum = std::numeric_limits<int>::max();
    constexpr int minimum = std::numeric_limits<int>::min();
    anchor.trusted = false;
    // Construct by endpoints: QRect(x, y, width, height) itself evaluates x +
    // width before subtracting 1, which overflows for this valid endpoint span.
    anchor.workAreaPhysicalRect =
        QRect(QPoint(maximum - 99, maximum - 99), QPoint(maximum, maximum));
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(100, 100), anchor, maximum) ==
                    anchor.workAreaPhysicalRect,
                "INT_MAX work-area endpoints and large gap do not overflow placement");
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(60, 60), anchor, 0) ==
                    QRect(QPoint(maximum - 99, maximum - 59), QPoint(maximum - 40, maximum)),
                "result endpoint construction remains valid when bottom reaches INT_MAX");
    anchor.workAreaPhysicalRect =
        QRect(QPoint(minimum, minimum), QPoint(minimum + 99, minimum + 99));
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(60, 60), anchor, 0) ==
                    QRect(QPoint(minimum, minimum + 40), QPoint(minimum + 59, minimum + 99)),
                "INT_MIN coordinates remain representable without signed underflow");
    anchor.workAreaPhysicalRect = QRect(QPoint(minimum, 0), QPoint(maximum, 100));
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(60, 60), anchor).isEmpty(),
                "endpoint spans exceeding int dimensions are rejected before Qt width arithmetic");
    anchor.workAreaPhysicalRect = QRect();
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), anchor).isEmpty(),
                "missing work area never becomes a guessed screen coordinate");

    const auto placement = platform->placeWindow(0, anchor);
    test.expect(!placement.success && !placement.errorCode.isEmpty(),
                "invalid native handle is rejected without moving another window");
    const auto invalidAppearance = platform->configureMenuWindow(0);
    test.expect(!invalidAppearance.success && !invalidAppearance.nativeDecorationsDisabled &&
                    !invalidAppearance.errorCode.isEmpty(),
                "invalid native handle never claims native decorations are disabled");
    NativeFreePlatform nativeFree;
    const auto delegatedAnchor = nativeFree.queryMenuAnchor();
    test.expect(nativeFree.anchorQueries == 1 && delegatedAnchor.dpi == 144 &&
                    delegatedAnchor.monitorName == QStringLiteral("native-free-fixture") &&
                    delegatedAnchor.workAreaPhysicalRect == QRect(-1920, 0, 1920, 1040),
                "base menu query preserves injected platform work-area and scale behavior");
    const auto unsupportedAppearance = nativeFree.configureMenuWindow(1);
    test.expect(!unsupportedAppearance.success && !unsupportedAppearance.nativeDecorationsDisabled &&
                    unsupportedAppearance.errorCode == QStringLiteral("unsupported-platform"),
                "base platform explicitly falls back without touching a native handle");
#ifdef Q_OS_WIN
    const DPI_AWARENESS_CONTEXT previousDpi =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    test.expect(previousDpi != nullptr, "hidden fixture uses explicit physical coordinates");
    // These windows stay hidden throughout the test. The test checks boundary
    // behavior and accepted requests, never infers a visible shadow from S_OK.
    HiddenNativeWindow hidden;
    test.expect(hidden.handle() != nullptr, "isolated hidden native fixture is created");
    if (hidden.handle()) {
        // Seed the previous native decoration policy, so the fixture proves
        // configuration removes it instead of merely observing a bare HWND.
        const DWORD initialCornerPreference = 2;
        DwmSetWindowAttribute(hidden.handle(), 33, &initialCornerPreference,
                              sizeof(initialCornerPreference));
        const DWMNCRENDERINGPOLICY initialPolicy = DWMNCRP_ENABLED;
        const HRESULT seededPolicy = DwmSetWindowAttribute(
            hidden.handle(), DWMWA_NCRENDERING_POLICY, &initialPolicy, sizeof(initialPolicy));
        const MARGINS initialMargins{1, 1, 1, 1};
        const HRESULT seededFrame = DwmExtendFrameIntoClientArea(hidden.handle(), &initialMargins);
        BOOL composition = FALSE;
        if (SUCCEEDED(DwmIsCompositionEnabled(&composition)) && composition &&
            SUCCEEDED(seededPolicy) && SUCCEEDED(seededFrame)) {
            BOOL initialNonClientRendering = FALSE;
            test.expect(SUCCEEDED(DwmGetWindowAttribute(
                            hidden.handle(), DWMWA_NCRENDERING_ENABLED,
                            &initialNonClientRendering, sizeof(initialNonClientRendering))) &&
                            initialNonClientRendering,
                        "fixture starts with the previous native non-client rendering enabled");
        }
        const LONG_PTR styleBefore = GetWindowLongPtrW(hidden.handle(), GWL_STYLE);
        const LONG_PTR extendedBefore = GetWindowLongPtrW(hidden.handle(), GWL_EXSTYLE);
        RECT boundsBefore{};
        GetWindowRect(hidden.handle(), &boundsBefore);
        const auto appearance =
            platform->configureMenuWindow(reinterpret_cast<quintptr>(hidden.handle()));
        test.expect(appearance.success == appearance.nativeDecorationsDisabled,
                    "appearance success means all native decorations were disabled");
        test.expect(appearance.success ? appearance.errorCode.isEmpty()
                                       : !appearance.errorCode.isEmpty(),
                    "unsupported compositor requests carry an explicit fallback reason");
        test.expect(!appearance.message.isEmpty(), "appearance always describes the actual request result");
        test.expect(GetWindowLongPtrW(hidden.handle(), GWL_STYLE) == styleBefore &&
                        GetWindowLongPtrW(hidden.handle(), GWL_EXSTYLE) == extendedBefore,
                    "appearance never adds caption, resize, layered or other Qt window styles");
        RECT boundsAfter{};
        GetWindowRect(hidden.handle(), &boundsAfter);
        test.expect(EqualRect(&boundsBefore, &boundsAfter) && !IsWindowVisible(hidden.handle()),
                    "appearance does not move, resize or show a hidden window");
        if (appearance.nativeDecorationsDisabled) {
            DWORD preference = 0;
            test.expect(SUCCEEDED(DwmGetWindowAttribute(hidden.handle(), 33, &preference,
                                                        sizeof(preference))) &&
                            preference == 1,
                        "DWM reports DONOTROUND for the self-drawn QML surface");
            DWORD color = 0;
            const HRESULT borderReadResult =
                DwmGetWindowAttribute(hidden.handle(), 34, &color, sizeof(color));
            // Microsoft's attribute contract documents BORDER_COLOR for Set
            // only. Its getter is diagnostic, never evidence that the strict
            // COLOR_NONE setter or non-client suppression failed/succeeded.
            std::cout << "INFO: optional DWM border getter HRESULT=0x" << std::hex
                      << static_cast<unsigned long>(borderReadResult) << std::dec << '\n';
            BOOL nonClientRendering = TRUE;
            test.expect(SUCCEEDED(DwmGetWindowAttribute(hidden.handle(), DWMWA_NCRENDERING_ENABLED,
                                                        &nonClientRendering,
                                                        sizeof(nonClientRendering))) &&
                            !nonClientRendering,
                        "DWM non-client rendering and its native shadow are disabled");
        }
        xdstarter::DesktopAnchorResult placementAnchor;
        placementAnchor.monitorPhysicalRect = QRect(100, 50, 1000, 900);
        placementAnchor.workAreaPhysicalRect = QRect(100, 50, 1000, 800);
        placementAnchor.startButtonPhysicalRect = QRect(100, 850, 48, 48);
        placementAnchor.trusted = true;
        for (const int gap : {8, 10, 12, 14, 16}) {
            const int inset = gap * 3;
            const QSize panelSize(240 - inset * 2, 160 - inset * 2);
            const QRect expectedPanel =
                xdstarter::desktopPopupPhysicalRect(panelSize, placementAnchor, gap);
            const auto placed = platform->placeWindow(reinterpret_cast<quintptr>(hidden.handle()),
                                                       placementAnchor, gap, false, inset);
            RECT nativeBounds{};
            GetWindowRect(hidden.handle(), &nativeBounds);
            test.expect(placed.success && placed.physicalRect == expectedPanel &&
                            nativeBounds.left == expectedPanel.left() - inset &&
                            nativeBounds.top == expectedPanel.top() - inset &&
                            nativeBounds.right - nativeBounds.left == 240 &&
                            nativeBounds.bottom - nativeBounds.top == 160 &&
                            !IsWindowVisible(hidden.handle()),
                        "shadow padding is excluded from panel alignment at all five scales");
            placementAnchor.trusted = false;
            const auto repeated = platform->placeWindow(
                reinterpret_cast<quintptr>(hidden.handle()), placementAnchor, gap, false, inset);
            test.expect(repeated.success && repeated.physicalRect == placed.physicalRect,
                        "repeated hidden placement is unchanged by a lost UIA result");
            placementAnchor.trusted = true;
        }
        const auto noShadow = platform->placeWindow(
            reinterpret_cast<quintptr>(hidden.handle()), placementAnchor, 12, false, 0);
        test.expect(noShadow.success &&
                        noShadow.physicalRect ==
                            xdstarter::desktopPopupPhysicalRect(QSize(240, 160), placementAnchor, 12),
                    "zero shadow inset keeps the complete HWND as the aligned panel");
        for (const int invalidInset : {-1, 80, 120, maximum}) {
            const auto rejected = platform->placeWindow(
                reinterpret_cast<quintptr>(hidden.handle()), placementAnchor, 8, false,
                invalidInset);
            test.expect(!rejected.success &&
                            rejected.errorCode == QStringLiteral("invalid-shadow-inset"),
                        "negative, excessive and overflowing shadow insets are rejected");
        }
        placementAnchor.workAreaPhysicalRect =
            QRect(QPoint(minimum, 0), QPoint(minimum + 999, 799));
        const auto shadowOverflow = platform->placeWindow(
            reinterpret_cast<quintptr>(hidden.handle()), placementAnchor, 8, false, 24);
        test.expect(!shadowOverflow.success &&
                        shadowOverflow.errorCode == QStringLiteral("window-coordinate-overflow"),
                    "shadow expansion cannot underflow the native coordinate domain");
        HiddenNativeWindow child(0, hidden.handle());
        test.expect(child.handle() != nullptr, "isolated hidden child fixture is created");
        if (child.handle()) {
            const auto childAppearance =
                platform->configureMenuWindow(reinterpret_cast<quintptr>(child.handle()));
            test.expect(!childAppearance.success &&
                            childAppearance.errorCode == QStringLiteral("invalid-owned-window"),
                        "appearance refuses even an owned child HWND");
        }
    }
    HiddenNativeWindow layered(WS_EX_LAYERED);
    test.expect(layered.handle() != nullptr, "isolated hidden layered fixture is created");
    if (layered.handle()) {
        const LONG_PTR styleBefore = GetWindowLongPtrW(layered.handle(), GWL_EXSTYLE);
        const auto layeredAppearance =
            platform->configureMenuWindow(reinterpret_cast<quintptr>(layered.handle()));
        test.expect(layeredAppearance.success == layeredAppearance.nativeDecorationsDisabled &&
                        layeredAppearance.errorCode != QStringLiteral("layered-window-unsupported") &&
                        GetWindowLongPtrW(layered.handle(), GWL_EXSTYLE) == styleBefore &&
                        !IsWindowVisible(layered.handle()),
                    "Qt alpha surfaces are configured without rejecting or mutating layered style");
    }
    const auto desktopAppearance =
        platform->configureMenuWindow(reinterpret_cast<quintptr>(GetDesktopWindow()));
    test.expect(!desktopAppearance.success &&
                    desktopAppearance.errorCode == QStringLiteral("invalid-owned-window"),
                "appearance refuses the foreign system desktop HWND before DWM calls");
    if (previousDpi) {
        SetThreadDpiAwarenessContext(previousDpi);
    }
#endif
    auto tray = xdstarter::createDesktopTray();
    test.expect(!tray->isAvailable(), "unstarted tray does not claim availability");
    const auto trayResult = tray->start(QImage());
    test.expect(!trayResult.success && !trayResult.errorCode.isEmpty() && !tray->isAvailable(),
                "invalid icon cannot register an unusable recovery entry");
    tray->stop();
    tray->stop();
    test.expect(!tray->isAvailable(), "tray cleanup is idempotent");

    QueuedTrayFixture queuedTray;
    int toggles = 0;
    int menus = 0;
    int recoveries = 0;
    QObject::connect(&queuedTray, &xdstarter::DesktopTray::toggleRequested, &application,
                     [&] { ++toggles; });
    QObject::connect(&queuedTray, &xdstarter::DesktopTray::menuRequested, &application,
                     [&](QPoint) { ++menus; });
    QObject::connect(&queuedTray, &xdstarter::DesktopTray::registrationChanged, &application,
                     [&](bool, const QString&) { ++recoveries; });
    const auto started = queuedTray.start(QImage());
    test.expect(started.success, "event source starts an isolated session");
    queuedTray.queueToggle();
    queuedTray.queueMenu();
    queuedTray.queueRecovery();
    queuedTray.stop();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    test.expect(toggles == 0 && menus == 0 && recoveries == 0,
                "stop invalidates already queued toggle, menu and Explorer recovery events");
    const auto restarted = queuedTray.start(QImage());
    test.expect(restarted.success, "event source restarts an isolated session");
    queuedTray.queueToggle();
    queuedTray.queueMenu();
    queuedTray.queueRecovery();
    queuedTray.stop();
    const auto newest = queuedTray.start(QImage());
    test.expect(newest.success, "newest event session starts");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    test.expect(toggles == 0 && menus == 0 && recoveries == 0,
                "restart cannot deliver queued notifications from the previous session");
    queuedTray.queueToggle();
    queuedTray.queueMenu();
    queuedTray.queueRecovery();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    test.expect(toggles == 1 && menus == 1 && recoveries == 1,
                "current live session still delivers all notification types");
    queuedTray.stop();
    queuedTray.queueToggle();
    queuedTray.queueMenu();
    queuedTray.queueRecovery();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    test.expect(toggles == 1 && menus == 1 && recoveries == 1,
                "events received while stopped never become future session notifications");

    if (test.failures == 0) {
        std::cout
            << "PASS: desktop action boundary, appearance requests, physical placement and recovery lifecycle\n";
    }
    return test.failures == 0 ? 0 : 1;
}
