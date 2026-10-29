#include "windows_motion_preference.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <windows.h>

#include <utility>

namespace xdstarter {

WindowsMotionPreference::WindowsMotionPreference(QObject* parent, Reader reader)
    : MotionPreference(false, parent), reader_(std::move(reader)) {
    if (!reader_) {
        reader_ = []() -> std::optional<bool> {
            BOOL enabled = FALSE;
            if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
                return std::nullopt;
            return enabled != FALSE;
        };
    }
    refresh();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowsMotionPreference::~WindowsMotionPreference() {
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void WindowsMotionPreference::refresh() {
    // If Windows cannot answer, prefer a stationary interface over motion the
    // user may have disabled. A later setting notification retries the read.
    publishAnimationsEnabled(reader_().value_or(false));
}

bool WindowsMotionPreference::nativeEventFilter(const QByteArray& eventType, void* message,
                                                 qintptr*) {
    if ((eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") ||
        !message || static_cast<MSG*>(message)->message != WM_SETTINGCHANGE)
        return false;
    if (!refreshPending_) {
        refreshPending_ = true;
        QMetaObject::invokeMethod(this, [this] {
            refreshPending_ = false;
            refresh();
        }, Qt::QueuedConnection);
    }
    return false; // Do not consume or broadcast native messages.
}

} // namespace xdstarter
