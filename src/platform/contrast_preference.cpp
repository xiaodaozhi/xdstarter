#include "contrast_preference.h"

#include <QColor>

#ifdef Q_OS_WIN
#include "windows/windows_contrast_preference.h"
#endif

#include <utility>

namespace xdstarter {

void ContrastPreference::publish(ContrastState state) {
    if (state.enabled) {
        for (const auto* key : {"window", "windowText", "highlight", "highlightText", "grayText"}) {
            if (!state.colors.value(QLatin1String(key)).value<QColor>().isValid())
                return;
        }
    } else {
        // Keep the last complete palette while QML switches back to the
        // regular theme; separate bindings can be reevaluated in either order.
        state.colors = state_.colors;
    }
    if (state_.enabled == state.enabled && state_.colors == state.colors)
        return;
    state_ = std::move(state);
    emit changed();
}

std::unique_ptr<ContrastPreference> createContrastPreference() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsContrastPreference>();
#else
    return std::make_unique<ContrastPreference>();
#endif
}

} // namespace xdstarter
