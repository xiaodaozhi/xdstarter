#include "platform/desktop_platform.h"
#include "platform/desktop_tray.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

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

QJsonObject jsonNativeWindow(const xdstarter::DesktopNativeWindowObservation& window) {
    // Hex strings preserve the complete integer even in JSON consumers that
    // represent every number as a double. They are never reusable identities.
    return {{QStringLiteral("handle"), QStringLiteral("0x%1").arg(window.handle, 0, 16)},
            {QStringLiteral("rootHandle"), QStringLiteral("0x%1").arg(window.rootHandle, 0, 16)},
            {QStringLiteral("processId"), static_cast<qint64>(window.processId)},
            {QStringLiteral("className"), window.className},
            {QStringLiteral("physicalRect"), jsonRect(window.physicalRect)},
            {QStringLiteral("exists"), window.exists},
            {QStringLiteral("visible"), window.visible}};
}

QJsonObject probeEnvironment() {
    QFile executable(QCoreApplication::applicationFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool hashed = executable.open(QIODevice::ReadOnly) && hash.addData(&executable);
#ifdef _MSC_FULL_VER
    const QString compiler = QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#elif defined(__clang__)
    const QString compiler = QStringLiteral("Clang ") + QString::fromLatin1(__clang_version__);
#elif defined(__GNUC__)
    const QString compiler = QStringLiteral("GCC ") + QString::fromLatin1(__VERSION__);
#else
    const QString compiler = QStringLiteral("unknown");
#endif
    return {{QStringLiteral("program"), QStringLiteral("xdstarter_desktop_platform_tests")},
            {QStringLiteral("binarySha256"), hashed ? QString::fromLatin1(hash.result().toHex())
                                                    : QString()},
            {QStringLiteral("qtBuildVersion"), QStringLiteral(QT_VERSION_STR)},
            {QStringLiteral("qtRuntimeVersion"), QString::fromLatin1(qVersion())},
            {QStringLiteral("product"), QSysInfo::prettyProductName()},
            {QStringLiteral("productVersion"), QSysInfo::productVersion()},
            {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
            {QStringLiteral("compiler"), compiler},
            {QStringLiteral("buildAbi"), QSysInfo::buildAbi()},
            {QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("buildArchitecture"), QSysInfo::buildCpuArchitecture()}};
}

xdstarter::DesktopStartButtonHitResult nativeFreeHitFixture() {
    xdstarter::DesktopStartButtonHitResult result;
    result.anchor.trusted = true;
    result.anchor.monitorPhysicalRect = QRect(-1920, 0, 1920, 1080);
    result.anchor.workAreaPhysicalRect = QRect(-1920, 0, 1920, 1040);
    result.anchor.startButtonPhysicalRect = QRect(-1912, 1040, 48, 40);
    result.taskbar = {1, 1, 501, QStringLiteral("Shell_TrayWnd"), QRect(-1920, 1040, 1920, 40),
                      true, true};
    result.startWindow = {2, 1, 501, QStringLiteral("Start"), result.anchor.startButtonPhysicalRect,
                          true, true};
    result.uiaProviderProcessId = 501;
    result.taskbarVerifiedExplorer = true;
    result.nativeHandleRead = true;
    result.startWindowIsTaskbarChild = true;
    result.observationsStable = true;
    const QRect button = result.anchor.startButtonPhysicalRect;
    const std::array<QPoint, 5> points{
        button.center(), QPoint(button.left() + 1, button.top() + 1),
        QPoint(button.right() - 1, button.top() + 1),
        QPoint(button.left() + 1, button.bottom() - 1),
        QPoint(button.right() - 1, button.bottom() - 1)};
    for (const QPoint& point : points) {
        auto& sample = result.samples[result.sampleCount++];
        sample = {point, result.startWindow, true, true};
    }
    return result;
}

void testStartButtonHitClassification(TestContext& test) {
    using Classification = xdstarter::DesktopStartButtonHitClassification;
    const auto baseline = nativeFreeHitFixture();
    const auto expect = [&test](const xdstarter::DesktopStartButtonHitResult& value,
                                Classification expected, std::string_view description) {
        test.expect(xdstarter::classifyStartButtonHitTarget(value) == expected, description);
    };
    expect(baseline, Classification::IndependentCandidate,
           "five matching samples on an independent window are only a diagnostic candidate");
    auto value = baseline;
    value.anchor.trusted = false;
    expect(value, Classification::Unidentified,
           "unidentified UIA button cannot become a candidate");
    value = baseline;
    value.nativeHandleRead = false;
    expect(value, Classification::Unavailable,
           "failed native HWND property is not a zero-HWND result");
    value = baseline;
    value.startWindow = {};
    expect(value, Classification::NoNativeWindow,
           "UIA elements without HWND have an explicit result");
    value = baseline;
    value.startWindow = value.taskbar;
    value.startWindowIsTaskbarChild = false;
    expect(value, Classification::SharedHost, "taskbar HWND is not an independent button window");
    value = baseline;
    value.startWindow.physicalRect = value.taskbar.physicalRect;
    expect(value, Classification::SharedHost, "a native host covering neighboring icons is shared");
    value = baseline;
    value.startWindow.physicalRect.translate(1, 0);
    expect(value, Classification::GeometryMismatch,
           "even a one-pixel geometry difference is rejected");
    value = baseline;
    value.startWindow.processId = 502;
    expect(value, Classification::IdentityMismatch,
           "foreign native process cannot use trusted UIA bounds");
    value = baseline;
    value.uiaProviderProcessId = 0;
    expect(value, Classification::IdentityMismatch, "missing provider identity is rejected");
    value = baseline;
    value.taskbar.processId = 502;
    expect(value, Classification::IdentityMismatch, "taskbar provider process must agree");
    value = baseline;
    value.startWindow.rootHandle = 3;
    expect(value, Classification::IdentityMismatch, "foreign root window is rejected");
    value = baseline;
    value.startWindowIsTaskbarChild = false;
    expect(value, Classification::IdentityMismatch,
           "matching geometry without child identity is rejected");
    value = baseline;
    value.taskbarVerifiedExplorer = false;
    expect(value, Classification::Unavailable,
           "unverified taskbar process cannot create a candidate");
    value = baseline;
    value.startWindow.exists = false;
    expect(value, Classification::Unavailable, "destroyed start window is unavailable");
    value = baseline;
    value.startWindow.visible = false;
    expect(value, Classification::Unavailable, "hidden start window is unavailable");
    value = baseline;
    value.taskbar.visible = false;
    expect(value, Classification::Unavailable, "auto-hidden taskbar is not presumed clickable");
    value = baseline;
    value.observationsStable = false;
    expect(value, Classification::ChangedDuringQuery,
           "movement or replacement during sampling is rejected");
    for (const unsigned int count : {0U, 4U, 6U}) {
        value = baseline;
        value.sampleCount = count;
        expect(value, Classification::Unavailable,
               "only the complete fixed five-point sample is accepted");
    }
    for (std::size_t index = 0; index < baseline.samples.size(); ++index) {
        value = baseline;
        value.samples[index].withinStartWindow = false;
        expect(value, Classification::Occluded,
               "each corner and center must hit the button window");
        value = baseline;
        value.samples[index].withinTaskbar = false;
        expect(value, Classification::Occluded, "every sample must remain within the same taskbar");
        value = baseline;
        value.samples[index].window.processId = 502;
        expect(value, Classification::Occluded, "overlay from another process is not a button hit");
    }
    value = baseline;
    value.samples[1].physicalPoint = value.samples[0].physicalPoint;
    expect(value, Classification::Occluded,
           "duplicating center samples cannot replace corner coverage");
    value = baseline;
    value.samples[0].window.handle = 0;
    expect(value, Classification::Occluded, "missing hit HWND is rejected");
    value = baseline;
    value.samples[0].window.visible = false;
    expect(value, Classification::Occluded, "invisible hit window is not accepted");
    value = baseline;
    value.samples[0].window.rootHandle = 3;
    expect(value, Classification::Occluded, "sample root identity must agree with taskbar");
    value = baseline;
    value.samples[0].window.handle = 3;
    value.samples[0].window.className = QStringLiteral("Child");
    expect(value, Classification::IndependentCandidate,
           "verified descendants of an independent button window can match a sample");
    value = baseline;
    value.anchor.monitorPhysicalRect = QRect(0, 0, 1920, 1080);
    expect(value, Classification::GeometryMismatch, "button on another monitor is not a target");
    value = baseline;
    value.anchor.startButtonPhysicalRect =
        QRect(QPoint(std::numeric_limits<int>::min(), 0),
               QPoint(std::numeric_limits<int>::max(), 39));
    value.startWindow.physicalRect = value.anchor.startButtonPhysicalRect;
    expect(value, Classification::GeometryMismatch,
           "overflowing endpoint spans are rejected safely");
    for (const int edge : {2, 3, 4, 5}) {
        value = baseline;
        const QRect button(-1912, 1040, edge, edge);
        value.anchor.startButtonPhysicalRect = button;
        value.startWindow.physicalRect = button;
        const std::array<QPoint, 5> points{
            button.center(), QPoint(button.left() + 1, button.top() + 1),
            QPoint(button.right() - 1, button.top() + 1),
            QPoint(button.left() + 1, button.bottom() - 1),
            QPoint(button.right() - 1, button.bottom() - 1)};
        for (std::size_t index = 0; index < points.size(); ++index) {
            value.samples[index].physicalPoint = points[index];
            value.samples[index].window = value.startWindow;
        }
        expect(value, edge < 5 ? Classification::GeometryMismatch
                              : Classification::IndependentCandidate,
               "tiny sets are rejected; five-pixel bounds keep five distinct points");
    }
    test.expect(xdstarter::startButtonHitClassificationName(Classification::IndependentCandidate) ==
                    QStringLiteral("independent-candidate") &&
                    xdstarter::startButtonHitClassificationName(Classification::NoNativeWindow) ==
                    QStringLiteral("no-native-window"),
                "diagnostic classification names are stable values rather than localized UI names");
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

void testDockingMatrix(TestContext& test) {
    int cases = 0;
    for (int edge = 0; edge < 4; ++edge) {
        for (const bool centered : {false, true}) {
            for (const double dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
                for (const QPoint origin : {QPoint(0, 0), QPoint(-2560, -1440)}) {
                    xdstarter::DesktopAnchorResult anchor;
                    anchor.monitorPhysicalRect = QRect(origin, QSize(2560, 1440));
                    const int bar = qRound(48 * dpr);
                    const auto monitor = anchor.monitorPhysicalRect;
                    switch (edge) {
                    case 0:
                        anchor.taskbarPhysicalRect =
                            QRect(origin.x(), monitor.bottom() - bar + 1, 2560, bar);
                        anchor.workAreaPhysicalRect = monitor.adjusted(0, 0, 0, -bar);
                        break;
                    case 1:
                        anchor.taskbarPhysicalRect = QRect(origin, QSize(2560, bar));
                        anchor.workAreaPhysicalRect = monitor.adjusted(0, bar, 0, 0);
                        break;
                    case 2:
                        anchor.taskbarPhysicalRect = QRect(origin, QSize(bar, 1440));
                        anchor.workAreaPhysicalRect = monitor.adjusted(bar, 0, 0, 0);
                        break;
                    case 3:
                        anchor.taskbarPhysicalRect =
                            QRect(monitor.right() - bar + 1, origin.y(), bar, 1440);
                        anchor.workAreaPhysicalRect = monitor.adjusted(0, 0, -bar, 0);
                        break;
                    }
                    anchor.centeredStart = centered;
                    const auto edges = xdstarter::desktopResizeEdges(anchor);
                    test.expect(!edges.testFlag(edge == 0   ? Qt::BottomEdge
                                                : edge == 1 ? Qt::TopEdge
                                                : edge == 2 ? Qt::LeftEdge
                                                            : Qt::RightEdge),
                                "docked border never resizes into the taskbar");
                    for (const QSize dip : {QSize(480, 480), QSize(980, 680), QSize(2200, 1800)}) {
                        const QSize size(qRound(dip.width() * dpr), qRound(dip.height() * dpr));
                        const int gap = qRound(8 * dpr);
                        const QRect panel = xdstarter::desktopPopupPhysicalRect(size, anchor, gap);
                        test.expect(panel.isValid() && anchor.workAreaPhysicalRect.contains(panel),
                                    "all sizes/scales remain in native work area");
                        test.expect(panel.size() ==
                                        QSize(qMin(size.width(),
                                                   anchor.workAreaPhysicalRect.width() - 2 * gap),
                                              qMin(size.height(),
                                                   anchor.workAreaPhysicalRect.height() - 2 * gap)),
                                    "oversized panels are clamped per axis");
                        test.expect(xdstarter::desktopMenuPanelBounds(anchor, gap).contains(panel),
                                    "all four configured margins survive oversized requests");
                        const auto area = anchor.workAreaPhysicalRect;
                        if (centered && edge >= 2)
                            test.expect(qAbs(qint64(panel.top()) + panel.bottom() - area.top() -
                                             area.bottom()) <= 1,
                                        "vertical centered menus stay centered at every height");
                        else if (panel.height() + 2 * gap <= area.height())
                            test.expect(edge == 0 ? panel.bottom() == area.bottom() - gap
                                                  : panel.top() == area.top() + gap,
                                        "height resize preserves the docking margin");
                        if (centered && edge < 2)
                            test.expect(qAbs(qint64(panel.left()) + panel.right() - area.left() -
                                             area.right()) <= 1,
                                        "centered menus stay centered at every width and scale");
                        else if (panel.width() + 2 * gap <= area.width())
                            test.expect(edge == 3 ? panel.right() == area.right() - gap
                                                  : panel.left() == area.left() + gap,
                                        "width resize preserves the docking margin");
                        ++cases;
                    }
                }
            }
        }
    }
    xdstarter::DesktopAnchorResult autoHide;
    autoHide.monitorPhysicalRect = QRect(0, 0, 1920, 1080);
    autoHide.workAreaPhysicalRect = autoHide.monitorPhysicalRect;
    for (const QRect bar : {QRect(0, 1040, 1920, 40), QRect(0, 0, 1920, 40), QRect(0, 0, 48, 1080),
                            QRect(1872, 0, 48, 1080)}) {
        autoHide.taskbarPhysicalRect = bar;
        const QRect area = xdstarter::desktopMenuWorkArea(autoHide);
        const QRect popup = xdstarter::desktopPopupPhysicalRect(QSize(980, 680), autoHide);
        test.expect(area.isValid() && !area.intersects(bar) && area.contains(popup),
                    "revealed auto-hide taskbars reserve space on all four edges");
        auto reserved = autoHide;
        reserved.workAreaPhysicalRect = area;
        test.expect(xdstarter::desktopMenuWorkArea(reserved) == area,
                    "already reserved space is never subtracted twice");
    }
    std::cout << "Taskbar matrix: " << cases << " parameter combinations\n";
}

void testMenuTaskbarInputRegion(TestContext& test) {
    int cases = 0;
    for (double dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
        for (QPoint origin : {QPoint(0, 0), QPoint(-2560, -1440)}) {
            for (int edge = 0; edge < 4; ++edge) {
                xdstarter::DesktopAnchorResult anchor;
                anchor.monitorPhysicalRect = QRect(origin, QSize(2560, 1440));
                const auto screen = anchor.monitorPhysicalRect;
                const int thickness = qRound(48 * dpr);
                if (edge == 0) {
                    anchor.taskbarPhysicalRect = QRect(
                        screen.left(), screen.bottom() - thickness + 1, screen.width(), thickness);
                    anchor.workAreaPhysicalRect = screen.adjusted(0, 0, 0, -thickness);
                } else if (edge == 1) {
                    anchor.taskbarPhysicalRect =
                        QRect(screen.topLeft(), QSize(screen.width(), thickness));
                    anchor.workAreaPhysicalRect = screen.adjusted(0, thickness, 0, 0);
                } else if (edge == 2) {
                    anchor.taskbarPhysicalRect =
                        QRect(screen.topLeft(), QSize(thickness, screen.height()));
                    anchor.workAreaPhysicalRect = screen.adjusted(thickness, 0, 0, 0);
                } else {
                    anchor.taskbarPhysicalRect = QRect(screen.right() - thickness + 1, screen.top(),
                                                       thickness, screen.height());
                    anchor.workAreaPhysicalRect = screen.adjusted(0, 0, -thickness, 0);
                }
                const int inset = qRound(24 * dpr);
                const auto panel = xdstarter::desktopPopupPhysicalRect(
                    QSize(qRound(980 * dpr), qRound(680 * dpr)), anchor, qRound(8 * dpr));
                const QRect outer = panel.adjusted(-inset, -inset, inset, inset);
                const QRegion region =
                    xdstarter::desktopMenuInputRegion(outer, anchor.taskbarPhysicalRect);
                const QRegion full(0, 0, outer.width(), outer.height());
                const QRect overlap = outer.intersected(anchor.taskbarPhysicalRect);
                const QRegion cut(overlap.translated(-outer.topLeft()));
                test.expect(overlap.isValid() && !cut.isEmpty(),
                            "fixture shadow genuinely overlaps taskbar");
                test.expect(region.intersected(cut).isEmpty(),
                            "no taskbar pixel belongs to the input region");
                test.expect(region.united(cut) == full,
                            "only taskbar pixels are clipped; other shadow retained");
                test.expect(
                    QRegion(panel.translated(-outer.topLeft())).subtracted(region).isEmpty(),
                    "visible panel and four-side margin geometry are unchanged");
                ++cases;
            }
        }
    }
    const QRect outer(-100, -200, 400, 300);
    const QRegion full(0, 0, 400, 300);
    test.expect(xdstarter::desktopMenuInputRegion(outer, {}) == full,
                "missing taskbar does not clip arbitrary screen-edge shadow");
    test.expect(xdstarter::desktopMenuInputRegion(outer, QRect(900, 900, 10, 10)) == full,
                "non-overlapping taskbar leaves the complete outer window");
    test.expect(xdstarter::desktopMenuInputRegion(outer, outer).isEmpty(),
                "a completely covered fixture has an empty region rather than full pass-through");
    test.expect(xdstarter::desktopMenuInputRegion({}, outer).isEmpty(),
                "invalid outer bounds rejected");
    const int low = std::numeric_limits<int>::min();
    const int high = std::numeric_limits<int>::max();
    const QRect extreme(low, low, 100, 100);
    test.expect(xdstarter::desktopMenuInputRegion(extreme, QRect(low, low, 100, 10)) ==
                    QRegion(0, 10, 100, 90),
                "local region conversion does not negate INT_MIN desktop coordinates");
    const QRect upper(QPoint(high - 99, high - 99), QPoint(high, high));
    test.expect(xdstarter::desktopMenuInputRegion(
                    upper, QRect(QPoint(high - 9, high - 99), QPoint(high, high))) ==
                    QRegion(0, 0, 90, 100),
                "local region conversion does not increment INT_MAX desktop coordinates");
    test.expect(
        xdstarter::desktopMenuInputRegion(QRect(QPoint(low, 0), QPoint(high, 100)), {}).isEmpty(),
        "unrepresentable outer span rejected before region allocation");
    std::cout << "Taskbar input-region matrix: " << cases << " combinations\n";
}

void testResizeAndMissingTaskbar(TestContext& test) {
    using xdstarter::desktopResizePhysicalRect;
    xdstarter::DesktopAnchorResult anchor;
    anchor.monitorPhysicalRect = QRect(-1600, -900, 1600, 900);
    const auto monitor = anchor.monitorPhysicalRect;
    const std::array<QRect, 4> areas{monitor.adjusted(0, 0, 0, -72), monitor.adjusted(0, 72, 0, 0),
                                     monitor.adjusted(72, 0, 0, 0), monitor.adjusted(0, 0, -72, 0)};
    for (int edge = 0; edge < 4; ++edge) {
        anchor.workAreaPhysicalRect = areas[edge];
        test.expect(xdstarter::desktopTaskbarEdge(anchor) ==
                        static_cast<xdstarter::TaskbarEdge>(edge),
                    "missing taskbar retains the unique reserved edge");
    }
    anchor.workAreaPhysicalRect = monitor;
    test.expect(xdstarter::desktopTaskbarEdge(anchor) == xdstarter::TaskbarEdge::Bottom,
                "a full work area does not invent a taskbar direction");
    anchor.workAreaPhysicalRect = monitor.adjusted(72, 72, 0, 0);
    test.expect(xdstarter::desktopTaskbarEdge(anchor) == xdstarter::TaskbarEdge::Bottom,
                "multiple reserved edges remain an ambiguous fallback");

    const std::array<Qt::Edges, 8> edges{Qt::TopEdge,
                                         Qt::BottomEdge,
                                         Qt::LeftEdge,
                                         Qt::RightEdge,
                                         Qt::TopEdge | Qt::LeftEdge,
                                         Qt::TopEdge | Qt::RightEdge,
                                         Qt::BottomEdge | Qt::LeftEdge,
                                         Qt::BottomEdge | Qt::RightEdge};
    int cases = 0;
    for (qreal scale : {1.0, 1.25, 1.5, 1.75, 2.0}) {
        for (int offset : {0, -2400}) {
            const QRect initial(offset - qRound(16 * scale), qRound(32 * scale),
                                qRound(1028 * scale), qRound(728 * scale));
            const QSize minimum(qRound(528 * scale), qRound(528 * scale));
            const QRect proposed = initial.adjusted(24, 24, 120, 120);
            for (auto edge : edges) {
                const QRect actual = desktopResizePhysicalRect(initial, proposed, edge, minimum);
                test.expect(edge.testFlag(Qt::LeftEdge) || actual.left() == initial.left(),
                            "non-dragged left edge never snaps inside the work area");
                test.expect(edge.testFlag(Qt::RightEdge) || actual.right() == initial.right(),
                            "non-dragged right edge remains fixed");
                test.expect(edge.testFlag(Qt::TopEdge) || actual.top() == initial.top(),
                            "non-dragged top edge preserves transparent shadow space");
                test.expect(edge.testFlag(Qt::BottomEdge) || actual.bottom() == initial.bottom(),
                            "non-dragged bottom edge remains fixed");
                const QRect tiny(initial.center(), QSize(1, 1));
                const QRect constrained = desktopResizePhysicalRect(initial, tiny, edge, minimum);
                test.expect(constrained.width() >= minimum.width() &&
                                constrained.height() >= minimum.height(),
                            "minimum dimensions remain valid after fixing the opposite edges");
                const QRect allowed = initial.adjusted(-30, -40, 80, 90);
                const QRect huge = allowed.adjusted(-1000, -1000, 1000, 1000);
                const QRect bounded =
                    desktopResizePhysicalRect(initial, huge, edge, minimum, allowed);
                test.expect(allowed.contains(bounded), "sizing cannot escape any work-area edge");
                test.expect(edge.testFlag(Qt::LeftEdge) || bounded.left() == initial.left(),
                            "maximum sizing keeps the opposite left edge fixed");
                test.expect(edge.testFlag(Qt::TopEdge) || bounded.top() == initial.top(),
                            "maximum sizing keeps the opposite top edge fixed");
                test.expect(edge.testFlag(Qt::RightEdge) || bounded.right() == initial.right(),
                            "maximum sizing keeps the opposite right edge fixed");
                test.expect(edge.testFlag(Qt::BottomEdge) || bounded.bottom() == initial.bottom(),
                            "maximum sizing keeps the opposite bottom edge fixed");
                ++cases;
            }
        }
    }
    const QRect origin(0, 0, 1000, 1000);
    const QRect reduced(50, 60, 300, 200);
    const QRect shrunk = desktopResizePhysicalRect(origin, origin, Qt::BottomEdge | Qt::RightEdge,
                                                   QSize(528, 528), reduced);
    test.expect(shrunk == reduced,
                "work-area shrink takes priority over an unavailable minimum size");
    const QRect oversized(QPoint(std::numeric_limits<int>::min() + 1, 0), QPoint(1000, 1000));
    test.expect(desktopResizePhysicalRect(origin, oversized, Qt::LeftEdge, QSize(1, 1)) == origin,
                "a representable endpoint must not overflow the resulting QSize span");
    test.expect(desktopResizePhysicalRect(origin, origin, {}, QSize(1, 1)) == origin,
                "no sizing edge never moves the window");
    test.expect(desktopResizePhysicalRect(origin, origin, Qt::LeftEdge | Qt::RightEdge,
                                          QSize(1, 1)) == origin,
                "contradictory edges do not resize");
    test.expect(!desktopResizePhysicalRect({}, origin, Qt::TopEdge, QSize(1, 1)).isValid(),
                "invalid initial bounds are rejected");
    const QRect extreme(std::numeric_limits<int>::min() + 100, 0, 100, 100);
    test.expect(desktopResizePhysicalRect(extreme, extreme, Qt::LeftEdge,
                                          QSize(std::numeric_limits<int>::max(), 100)) == extreme,
                "minimum size arithmetic cannot overflow coordinates");
    std::cout << "Sizing matrix: " << cases << " edge/scale/origin combinations\n";
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    auto platform = xdstarter::createDesktopPlatform();
    if (application.arguments().contains(QStringLiteral("--probe-start-hit"))) {
        // The sole entry point for this native read-only research query. Normal
        // tests and the product never enumerate the live Shell for hit testing.
        QElapsedTimer timer;
        timer.start();
        const auto hit = platform->queryStartButtonHitTarget();
        const qint64 elapsed = timer.elapsed();
        QJsonArray samples;
        for (unsigned int index = 0; index < hit.sampleCount && index < hit.samples.size();
             ++index) {
            const auto& sample = hit.samples[index];
            samples.append(QJsonObject{
                {QStringLiteral("x"), sample.physicalPoint.x()},
                {QStringLiteral("y"), sample.physicalPoint.y()},
                {QStringLiteral("window"), jsonNativeWindow(sample.window)},
                {QStringLiteral("withinStartWindow"), sample.withinStartWindow},
                {QStringLiteral("withinTaskbar"), sample.withinTaskbar}});
        }
        const QJsonObject value{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("query"), QStringLiteral("start-button-native-hit")},
            {QStringLiteral("readOnly"), true},
            {QStringLiteral("captureReady"), false},
            {QStringLiteral("observedAtUtc"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("elapsedMs"), elapsed},
            {QStringLiteral("environment"), probeEnvironment()},
            {QStringLiteral("classification"),
             xdstarter::startButtonHitClassificationName(hit.classification)},
            {QStringLiteral("uiaIdentified"), hit.anchor.trusted},
            {QStringLiteral("monitorName"), hit.anchor.monitorName},
            {QStringLiteral("monitorPhysicalRect"), jsonRect(hit.anchor.monitorPhysicalRect)},
            {QStringLiteral("workAreaPhysicalRect"), jsonRect(hit.anchor.workAreaPhysicalRect)},
            {QStringLiteral("startButtonPhysicalRect"),
             jsonRect(hit.anchor.startButtonPhysicalRect)},
            {QStringLiteral("dpi"), static_cast<int>(hit.anchor.dpi)},
            {QStringLiteral("taskbar"), jsonNativeWindow(hit.taskbar)},
            {QStringLiteral("taskbarVerifiedExplorer"), hit.taskbarVerifiedExplorer},
            {QStringLiteral("uiaProviderProcessId"), static_cast<qint64>(hit.uiaProviderProcessId)},
            {QStringLiteral("nativeHandleRead"), hit.nativeHandleRead},
            {QStringLiteral("startWindow"), jsonNativeWindow(hit.startWindow)},
            {QStringLiteral("startWindowIsTaskbarChild"), hit.startWindowIsTaskbarChild},
            {QStringLiteral("observationsStable"), hit.observationsStable},
            {QStringLiteral("sampleCount"), static_cast<int>(hit.sampleCount)},
            {QStringLiteral("samples"), samples},
            {QStringLiteral("message"), hit.message},
            {QStringLiteral("errorCode"), hit.errorCode}};
        std::cout << QJsonDocument(value).toJson(QJsonDocument::Indented).constData();
        return 0;
    }
    const bool probeMenu = application.arguments().contains(QStringLiteral("--probe-menu-anchor"));
    if (probeMenu || application.arguments().contains(QStringLiteral("--probe-anchor"))) {
        // Explicit read-only native probe, never part of unattended CTest. A
        // trustworthy fallback is a valid observation, not a fabricated pass.
        const auto anchor = probeMenu ? platform->queryMenuAnchor()
                                      : platform->queryStartButtonAnchor();
        const QJsonObject value{
            {QStringLiteral("trusted"), anchor.trusted},
            {QStringLiteral("query"),
             probeMenu ? QStringLiteral("menu-work-area") : QStringLiteral("start-button-uia")},
            {QStringLiteral("monitorName"), anchor.monitorName},
            {QStringLiteral("startButtonPhysicalRect"), jsonRect(anchor.startButtonPhysicalRect)},
            {QStringLiteral("taskbarPhysicalRect"), jsonRect(anchor.taskbarPhysicalRect)},
            {QStringLiteral("taskbarEdge"),
             static_cast<int>(xdstarter::desktopTaskbarEdge(anchor))},
            {QStringLiteral("centered"), anchor.centeredStart},
            {QStringLiteral("monitorPhysicalRect"), jsonRect(anchor.monitorPhysicalRect)},
            {QStringLiteral("workAreaPhysicalRect"), jsonRect(anchor.workAreaPhysicalRect)},
            {QStringLiteral("dpi"), static_cast<int>(anchor.dpi)},
            {QStringLiteral("message"), anchor.message},
            {QStringLiteral("errorCode"), anchor.errorCode}};
        std::cout << QJsonDocument(value).toJson(QJsonDocument::Indented).constData();
        return 0;
    }

    TestContext test;
    testDockingMatrix(test);
    testMenuTaskbarInputRegion(test);
    testResizeAndMissingTaskbar(test);
    testStartButtonHitClassification(test);
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

    {
        auto dock = anchor;
        dock.monitorPhysicalRect = QRect(0, 0, 1920, 1080);
        dock.workAreaPhysicalRect = QRect(0, 0, 1920, 1040);
        dock.taskbarPhysicalRect = QRect(0, 1040, 1920, 40);
        dock.centeredStart = true;
        test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), dock) ==
                        QRect(480, 432, 960, 600),
                    "centered taskbar aligns menu to the work-area center");
        test.expect(xdstarter::desktopResizeEdges(dock).testFlag(Qt::TopEdge) &&
                        xdstarter::desktopResizeEdges(dock).testFlag(Qt::LeftEdge),
                    "centered bottom menu exposes top and both side resize handles");
        dock.centeredStart = false;
        dock.workAreaPhysicalRect = QRect(0, 40, 1920, 1040);
        dock.taskbarPhysicalRect = QRect(0, 0, 1920, 40);
        test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), dock) ==
                            QRect(8, 48, 960, 600) &&
                        xdstarter::desktopResizeEdges(dock) == (Qt::BottomEdge | Qt::RightEdge),
                    "top taskbar anchors at the top and resizes away from the dock");
        dock.workAreaPhysicalRect = QRect(48, 0, 1872, 1080);
        dock.taskbarPhysicalRect = QRect(0, 0, 48, 1080);
        test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), dock) ==
                            QRect(56, 8, 960, 600) &&
                        xdstarter::desktopResizeEdges(dock) == (Qt::BottomEdge | Qt::RightEdge),
                    "left taskbar uses its adjacent top corner and outward edges");
        dock.workAreaPhysicalRect = QRect(0, 0, 1872, 1080);
        dock.taskbarPhysicalRect = QRect(1872, 0, 48, 1080);
        test.expect(xdstarter::desktopPopupPhysicalRect(QSize(960, 600), dock) ==
                            QRect(904, 8, 960, 600) &&
                        xdstarter::desktopResizeEdges(dock) == (Qt::BottomEdge | Qt::LeftEdge),
                    "right taskbar mirrors the anchor and width resize handle");
    }

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
                    anchor.workAreaPhysicalRect.adjusted(8, 8, -8, -8),
                "oversized popup retains all four work-area margins");
    test.expect(xdstarter::desktopPopupPhysicalRect(QSize(), anchor).isEmpty(),
                "invalid window dimensions are rejected");
    constexpr int maximum = std::numeric_limits<int>::max();
    constexpr int minimum = std::numeric_limits<int>::min();
    anchor.trusted = false;
    // Construct by endpoints: QRect(x, y, width, height) itself evaluates x +
    // width before subtracting 1, which overflows for this valid endpoint span.
    anchor.workAreaPhysicalRect =
        QRect(QPoint(maximum - 99, maximum - 99), QPoint(maximum, maximum));
    test.expect(!xdstarter::desktopPopupPhysicalRect(QSize(100, 100), anchor, maximum).isValid(),
                "a work area too small for the requested gap is rejected without overflow");
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
    const auto unsupportedHit = nativeFree.queryStartButtonHitTarget();
    test.expect(unsupportedHit.classification ==
                        xdstarter::DesktopStartButtonHitClassification::Unsupported &&
                    unsupportedHit.errorCode == QStringLiteral("unsupported-platform") &&
                    nativeFree.anchorQueries == 0 && unsupportedHit.sampleCount == 0,
                "base hit probe is unsupported without triggering Shell or legacy anchor queries");
    const auto delegatedAnchor = nativeFree.queryMenuAnchor();
    test.expect(nativeFree.anchorQueries == 1 && delegatedAnchor.dpi == 144 &&
                    delegatedAnchor.monitorName == QStringLiteral("native-free-fixture") &&
                    delegatedAnchor.workAreaPhysicalRect == QRect(-1920, 0, 1920, 1040),
                "base menu query preserves injected platform work-area and scale behavior");
    const auto unsupportedAppearance = nativeFree.configureMenuWindow(1);
    test.expect(!unsupportedAppearance.success && !unsupportedAppearance.nativeDecorationsDisabled &&
                    unsupportedAppearance.errorCode == QStringLiteral("unsupported-platform"),
                "base platform explicitly falls back without touching a native handle");
    const auto unsupportedMetrics = nativeFree.queryWindowMetrics(1);
    test.expect(!unsupportedMetrics.supported && !unsupportedMetrics.success,
                "base native metrics are explicitly unsupported without touching a handle");
    test.expect(!nativeFree.hasOwnedForegroundWindow(1) &&
                    !platform->hasOwnedForegroundWindow(0),
                "unsupported or invalid menu handles never claim owned native foreground");
    test.expect(nativeFree.screenForAnchor(delegatedAnchor) == nullptr &&
                    platform->screenForAnchor(delegatedAnchor) == nullptr,
                "screen mapping is unavailable without a GUI application, including Windows");
#ifdef Q_OS_WIN
    const auto invalidMetrics = platform->queryWindowMetrics(0);
    test.expect(invalidMetrics.supported && !invalidMetrics.success,
                "native metrics reject an invalid handle before reading another window");
    const DPI_AWARENESS_CONTEXT previousDpi =
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    test.expect(previousDpi != nullptr, "hidden fixture uses explicit physical coordinates");
    // These windows stay hidden throughout the test. The test checks boundary
    // behavior and accepted requests, never infers a visible shadow from S_OK.
    HiddenNativeWindow hidden;
    test.expect(hidden.handle() != nullptr, "isolated hidden native fixture is created");
    if (hidden.handle()) {
        test.expect(!platform->hasOwnedForegroundWindow(reinterpret_cast<quintptr>(hidden.handle())),
                    "a hidden menu cannot claim an unrelated foreground window");
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
        const auto metrics =
            platform->queryWindowMetrics(reinterpret_cast<quintptr>(hidden.handle()));
        test.expect(metrics.supported && metrics.success &&
                        metrics.physicalSize == QSize(boundsBefore.right - boundsBefore.left,
                                                       boundsBefore.bottom - boundsBefore.top),
                    "native metrics report the hidden owned GUI-thread surface physical size");
        bool crossThreadAccepted = true;
        std::thread otherThread([&] {
            crossThreadAccepted = platform->queryWindowMetrics(
                reinterpret_cast<quintptr>(hidden.handle())).success;
        });
        otherThread.join();
        test.expect(!crossThreadAccepted,
                    "native metrics reject an owned surface from another GUI thread");
        const auto appearance =
            platform->configureMenuWindow(reinterpret_cast<quintptr>(hidden.handle()));
        const bool topmostDenied =
            appearance.errorCode == QStringLiteral("menu-topmost-state-denied");
        test.expect(
            appearance.success == (appearance.nativeDecorationsDisabled && !topmostDenied),
            "appearance succeeds only when decorations and measured topmost policy succeed");
        test.expect(appearance.success ? appearance.errorCode.isEmpty()
                                       : !appearance.errorCode.isEmpty(),
                    "unsupported compositor requests carry an explicit fallback reason");
        test.expect(!appearance.message.isEmpty(), "appearance always describes the actual request result");
        const auto extendedAfter = GetWindowLongPtrW(hidden.handle(), GWL_EXSTYLE);
        const LONG_PTR menuStyles = WS_EX_TOOLWINDOW | WS_EX_APPWINDOW | WS_EX_TOPMOST;
        const auto styleAfter = GetWindowLongPtrW(hidden.handle(), GWL_STYLE);
        const bool menuPolicy =
            styleAfter == styleBefore &&
            (extendedAfter & ~menuStyles) == (extendedBefore & ~menuStyles) &&
            (extendedAfter & WS_EX_TOOLWINDOW) && !(extendedAfter & WS_EX_APPWINDOW) &&
            ((extendedAfter & WS_EX_TOPMOST) ? !topmostDenied
                                             : (!appearance.success && topmostDenied));
        if (!menuPolicy)
            std::cerr << "Native menu policy diagnostic: styleBefore=0x" << std::hex << styleBefore
                      << " styleAfter=0x" << styleAfter << " extendedBefore=0x" << extendedBefore
                      << " extendedAfter=0x" << extendedAfter << std::dec
                      << " visible=" << IsWindowVisible(hidden.handle())
                      << " iconic=" << IsIconic(hidden.handle())
                      << " appearance=" << appearance.errorCode.toStdString() << '\n';
        test.expect(menuPolicy, "hidden menu excludes taskbar buttons and measures or explicitly "
                                "denies topmost without mutating alpha styles");
        if (topmostDenied)
            std::cout << "INFO: hidden background STATIC fixture did not acquire TOPMOST; "
                         "configuration explicitly reported menu-topmost-state-denied\n";
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
            test.expect(!platform->hasOwnedForegroundWindow(reinterpret_cast<quintptr>(child.handle())),
                        "native focus ownership rejects child HWNDs as menu roots");
            test.expect(!platform->queryWindowMetrics(reinterpret_cast<quintptr>(child.handle())).success,
                        "native metrics reject an owned child rather than a top-level surface");
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
        const bool topmostDenied =
            layeredAppearance.errorCode == QStringLiteral("menu-topmost-state-denied");
        test.expect(layeredAppearance.success ==
                            (layeredAppearance.nativeDecorationsDisabled && !topmostDenied) &&
                        ((GetWindowLongPtrW(layered.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST)
                             ? !topmostDenied
                             : (!layeredAppearance.success && topmostDenied)) &&
                        layeredAppearance.errorCode !=
                            QStringLiteral("layered-window-unsupported") &&
                        (GetWindowLongPtrW(layered.handle(), GWL_EXSTYLE) & WS_EX_LAYERED) ==
                            (styleBefore & WS_EX_LAYERED) &&
                        !IsWindowVisible(layered.handle()),
                    "Qt alpha surfaces are configured without rejecting or mutating layered style");
    }
    const auto desktopAppearance =
        platform->configureMenuWindow(reinterpret_cast<quintptr>(GetDesktopWindow()));
    test.expect(!desktopAppearance.success &&
                    desktopAppearance.errorCode == QStringLiteral("invalid-owned-window"),
                "appearance refuses the foreign system desktop HWND before DWM calls");
    test.expect(!platform->queryWindowMetrics(reinterpret_cast<quintptr>(GetDesktopWindow())).success,
                "native metrics reject a foreign process window");
    test.expect(!platform->hasOwnedForegroundWindow(reinterpret_cast<quintptr>(GetDesktopWindow())),
                "native focus ownership rejects a foreign process menu root");
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
