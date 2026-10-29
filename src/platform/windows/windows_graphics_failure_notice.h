#pragma once

#include <cstdint>

namespace xdstarter {

struct GraphicsFailureNoticeApi {
    bool (*startWatchdog)();
    bool (*sendSessionMessage)();
    bool (*showFallbackMessage)();
};

// Exposed to the platform fixture to exercise failed API calls without showing UI.
[[nodiscard]] int runGraphicsFailureNoticeWith(const GraphicsFailureNoticeApi& api);
[[nodiscard]] bool startGraphicsFailureNoticeWatchdog(std::uint32_t timeoutMilliseconds);

} // namespace xdstarter
