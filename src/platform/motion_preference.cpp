#include "motion_preference.h"

#ifdef Q_OS_WIN
#include "windows/windows_motion_preference.h"
#endif

namespace xdstarter {

void MotionPreference::publishAnimationsEnabled(bool enabled) {
    if (animationsEnabled_ == enabled)
        return;
    animationsEnabled_ = enabled;
    emit animationsEnabledChanged();
}

std::unique_ptr<MotionPreference> createMotionPreference() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsMotionPreference>();
#else
    return std::make_unique<MotionPreference>();
#endif
}

} // namespace xdstarter
