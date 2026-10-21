#include "desktop_tray.h"

#include <QMetaObject>

#include <utility>

#ifdef Q_OS_WIN
#include "windows/windows_desktop_tray.h"
#else
#include "stub/stub_desktop_tray.h"
#endif

namespace xdstarter {

void DesktopTray::setWinKeyCaptureState(bool, bool, const QString&) {}

void DesktopTray::beginEventSession() {
    ++eventGeneration_;
    sessionActive_ = true;
}

void DesktopTray::endEventSession() {
    sessionActive_ = false;
    ++eventGeneration_;
}

bool DesktopTray::isCurrentEventSession(quint64 generation) const {
    return sessionActive_ && generation == eventGeneration_;
}

void DesktopTray::queueSessionCallback(std::function<void()> callback) {
    const quint64 generation = eventGeneration_;
    if (!isCurrentEventSession(generation)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, callback = std::move(callback)] {
            if (isCurrentEventSession(generation)) {
                callback();
            }
        },
        Qt::QueuedConnection);
}

std::unique_ptr<DesktopTray> createDesktopTray() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsDesktopTray>();
#else
    return std::make_unique<StubDesktopTray>();
#endif
}

} // namespace xdstarter
