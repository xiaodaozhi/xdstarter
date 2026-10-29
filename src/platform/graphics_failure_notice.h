#pragma once

namespace xdstarter {

inline constexpr char graphicsFailureNoticeArgument[] = "--internal-graphics-failure-notice";

// Runs only in a separate helper process before Qt's GUI and desktop services exist.
[[nodiscard]] int runGraphicsFailureNotice();

} // namespace xdstarter
