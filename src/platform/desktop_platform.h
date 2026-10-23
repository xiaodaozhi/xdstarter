#pragma once

#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>

#include <array>
#include <memory>

class QScreen;

namespace xdstarter {

struct DesktopActionResult {
    bool success = false;
    QString message;
    QString errorCode;
};

// All rectangles are native physical pixels. Conversion to Qt logical screen
// coordinates belongs to the GUI boundary, not this platform adapter.
struct DesktopAnchorResult {
    bool trusted = false;
    QRect startButtonPhysicalRect;
    QRect monitorPhysicalRect;
    QRect workAreaPhysicalRect;
    unsigned int dpi = 96; // A native query uses 0 when monitor scale is unknown.
    QString message;
    QString errorCode;
    QString monitorName;
    QRect taskbarPhysicalRect;
    bool centeredStart = false;
};

enum class TaskbarEdge { Bottom, Top, Left, Right };
[[nodiscard]] TaskbarEdge desktopTaskbarEdge(const DesktopAnchorResult& anchor);
[[nodiscard]] Qt::Edges desktopResizeEdges(const DesktopAnchorResult& anchor);
// Keep the non-dragged outer edges fixed, including transparent shadow space.
// Native sizing may otherwise snap that space inside the monitor work area.
[[nodiscard]] QRect desktopResizePhysicalRect(QRect initial, QRect proposed, Qt::Edges edges,
                                              QSize minimumOuterSize,
                                              QRect allowedOuterBounds = {});
// Excludes a revealed auto-hide taskbar even if Windows' rcWork still covers
// the whole monitor. Already reserved appbar areas are not subtracted twice.
[[nodiscard]] QRect desktopMenuWorkArea(const DesktopAnchorResult& anchor);
// Visible panel bounds after reserving the configured gap on all four sides.
[[nodiscard]] QRect desktopMenuPanelBounds(const DesktopAnchorResult& anchor,
                                           int gapPhysicalPixels = 8);
// Local physical-pixel window region. Only the taskbar overlap is excluded;
// all remaining shadow pixels, including screen-edge space, are retained.
[[nodiscard]] QRegion desktopMenuInputRegion(QRect outerPhysical, QRect taskbarPhysical);

// These handles are diagnostic integer values, not persistent identities or
// permission to consume input. No native types cross the platform boundary.
struct DesktopNativeWindowObservation {
    quintptr handle = 0;
    quintptr rootHandle = 0;
    quint32 processId = 0;
    QString className;
    QRect physicalRect;
    bool exists = false;
    bool visible = false;
};

struct DesktopStartButtonHitSample {
    QPoint physicalPoint;
    DesktopNativeWindowObservation window;
    bool withinStartWindow = false;
    bool withinTaskbar = false;
};

enum class DesktopStartButtonHitClassification {
    Unsupported,
    Unidentified,
    Unavailable,
    NoNativeWindow,
    SharedHost,
    IdentityMismatch,
    GeometryMismatch,
    Occluded,
    ChangedDuringQuery,
    IndependentCandidate
};

struct DesktopStartButtonHitResult {
    DesktopAnchorResult anchor;
    DesktopNativeWindowObservation taskbar;
    DesktopNativeWindowObservation startWindow;
    quint32 uiaProviderProcessId = 0;
    bool taskbarVerifiedExplorer = false;
    bool nativeHandleRead = false;
    bool startWindowIsTaskbarChild = false;
    bool observationsStable = false;
    std::array<DesktopStartButtonHitSample, 5> samples{};
    unsigned int sampleCount = 0;
    DesktopStartButtonHitClassification classification =
        DesktopStartButtonHitClassification::Unsupported;
    QString message;
    QString errorCode;
};

// Native-free classification of an explicit, short-lived diagnostic sample.
// IndependentCandidate is only a point-in-time observation, never capture-ready.
[[nodiscard]] DesktopStartButtonHitClassification
classifyStartButtonHitTarget(const DesktopStartButtonHitResult& result);
[[nodiscard]] QString startButtonHitClassificationName(DesktopStartButtonHitClassification value);

struct DesktopPlacementResult {
    bool success = false;
    QRect physicalRect;
    QString message;
    QString errorCode;
};

struct DesktopAppearanceResult {
    // Success includes actual window policy (Tool/no AppWindow/Topmost) as well
    // as decoration suppression. DWM requests alone are not visual evidence.
    bool success = false;
    // Reports DWM suppression independently, including when Topmost is denied.
    bool nativeDecorationsDisabled = false;
    QString message;
    QString errorCode;
};

struct DesktopWindowMetricsResult {
    // Unsupported adapters may leave this false. A supported adapter returning
    // success=false must not be treated as a settled native surface.
    bool supported = false;
    bool success = false;
    QSize physicalSize;
};

struct DesktopPresentationResult {
    bool supported = false;
    bool visible = false;
    bool minimized = false;
    bool foreground = false;
    QString errorCode;
};

[[nodiscard]] bool isSupportedDesktopAction(const QString& actionId);
[[nodiscard]] QRect desktopPopupPhysicalRect(const QSize& windowPhysicalSize,
                                             const DesktopAnchorResult& anchor,
                                             int gapPhysicalPixels = 8);

class DesktopPlatform {
  public:
    virtual ~DesktopPlatform() = default;

    // Potentially blocking Shell/UI Automation operations. The caller must run
    // these on a worker. An operation that needs COM owns its own apartment.
    [[nodiscard]] virtual DesktopActionResult openDesktopAction(const QString& actionId) = 0;
    [[nodiscard]] virtual DesktopAnchorResult queryStartButtonAnchor() = 0;
    // Explicit read-only research only. Never called by normal product startup
    // or menu placement; may block on another process's UI Automation provider.
    [[nodiscard]] virtual DesktopStartButtonHitResult queryStartButtonHitTarget();
    // Product placement does not require StartButton UIA identification. The
    // default preserves injected-platform behavior; Windows overrides this
    // with a work-area-only query that performs no COM or UIA operation.
    [[nodiscard]] virtual DesktopAnchorResult queryMenuAnchor();
    // A short native GUI operation for this thread's own top-level window.
    // Suppresses native decoration and configures hidden menu taskbar/topmost
    // behavior, preserving alpha styles. No hit testing, regions or hooks.
    [[nodiscard]] virtual DesktopAppearanceResult configureMenuWindow(quintptr nativeHandle);
    // Short GUI-thread mapping from the native anchor identity to exactly one
    // live Qt screen. Workers must not call this or access returned QScreen.
    [[nodiscard]] virtual QScreen *screenForAnchor(const DesktopAnchorResult &anchor);
    // Short GUI-thread observation of this process's own top-level surface.
    // It does not move, show or activate the window, or query another process.
    [[nodiscard]] virtual DesktopWindowMetricsResult queryWindowMetrics(quintptr nativeHandle);
    // Short GUI-thread observation for native dialogs without a QWindow. True
    // only when the current foreground is this process's menu or its owned
    // window. Unsupported adapters return false; no activation or caching.
    [[nodiscard]] virtual bool hasOwnedForegroundWindow(quintptr menuHandle);
    // Same ownership boundary, excluding the menu itself. Used only to avoid
    // raising a repeated reveal over an actual owned dialog.
    [[nodiscard]] virtual bool hasOwnedForegroundDialog(quintptr menuHandle);
    // User-requested reveal only, after Qt has made its surface visible. Restore
    // this thread's own HWND even when a launcher supplied SW_HIDE/minimized.
    // Exact owned foreground dialogs are preserved rather than covered.
    [[nodiscard]] virtual DesktopPresentationResult presentMenuWindow(quintptr nativeHandle);
    // A short native GUI operation, with no Shell/COM/IO. Only a top-level window
    // owned by this process may be placed. The window's real physical size is
    // read after Qt has selected its screen, avoiding logical/native mixing.
    // The symmetric shadow inset belongs to the transparent Qt surface. Only
    // the inner panel is aligned and reported in physicalRect.
    [[nodiscard]] virtual DesktopPlacementResult placeWindow(quintptr nativeHandle,
                                                             const DesktopAnchorResult& anchor,
                                                             int gapPhysicalPixels = 8,
                                                             bool activate = true,
                                                             int shadowInsetPhysicalPixels = 0);
};

[[nodiscard]] std::unique_ptr<DesktopPlatform> createDesktopPlatform();

} // namespace xdstarter
