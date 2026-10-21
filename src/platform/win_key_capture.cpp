// src/platform/win_key_capture.cpp

#include "win_key_capture.h"

#include <QMetaObject>

#include <utility>

#ifdef Q_OS_WIN
#include "windows/windows_win_key_capture.h"
#else
#include "stub/stub_win_key_capture.h"
#endif

namespace xdstarter {

quint64 WinKeyCapture::beginEventSession() {
    const auto generation = generation_.fetch_add(1) + 1;
    sessionEnabled_.store(true);
    return generation;
}

quint64 WinKeyCapture::endEventSession() {
    sessionEnabled_.store(false);
    return generation_.fetch_add(1) + 1;
}

quint64 WinKeyCapture::eventGeneration() const {
    return generation_.load();
}

bool WinKeyCapture::isCurrentEventSession(quint64 generation) const {
    return sessionEnabled_.load() && generation_.load() == generation;
}

void WinKeyCapture::queueToggleRequested(quint64 generation) {
    QMetaObject::invokeMethod(
        this,
        [this, generation] {
            if (isCurrentEventSession(generation) && isActive()) {
                emit toggleRequested();
            }
        },
        Qt::QueuedConnection);
}

void WinKeyCapture::queueStatusChanged(quint64 generation, bool active, QString message,
                                     QString errorCode) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, active, message = std::move(message), errorCode = std::move(errorCode)] {
            if (eventGeneration() == generation && (!active || isActive())) {
                emit statusChanged(active, message, errorCode);
            }
        },
        Qt::QueuedConnection);
}

std::unique_ptr<WinKeyCapture> createWinKeyCapture() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsWinKeyCapture>();
#else
    return std::make_unique<StubWinKeyCapture>();
#endif
}

} // namespace xdstarter
