#include "windows_contrast_preference.h"

#include <QColor>
#include <QCoreApplication>
#include <QMetaObject>

#include <windows.h>

#include <utility>

namespace xdstarter {
namespace {

QColor systemColor(int role) {
    const COLORREF value = GetSysColor(role);
    return QColor(GetRValue(value), GetGValue(value), GetBValue(value));
}

std::optional<ContrastState> readSystemContrast() {
    HIGHCONTRASTW settings{};
    settings.cbSize = sizeof(settings);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0))
        return std::nullopt;
    if (!(settings.dwFlags & HCF_HIGHCONTRASTON))
        return ContrastState{};
    return ContrastState{true, {{QStringLiteral("window"), systemColor(COLOR_WINDOW)},
                                {QStringLiteral("windowText"), systemColor(COLOR_WINDOWTEXT)},
                                {QStringLiteral("highlight"), systemColor(COLOR_HIGHLIGHT)},
                                {QStringLiteral("highlightText"), systemColor(COLOR_HIGHLIGHTTEXT)},
                                {QStringLiteral("grayText"), systemColor(COLOR_GRAYTEXT)}}};
}

} // namespace

WindowsContrastPreference::WindowsContrastPreference(QObject* parent, Reader reader)
    : ContrastPreference(parent), reader_(std::move(reader)) {
    if (!reader_)
        reader_ = readSystemContrast;
    refresh();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowsContrastPreference::~WindowsContrastPreference() {
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void WindowsContrastPreference::refresh() {
    // Keep the last known state when the OS read fails. In particular, a
    // transient failure must not silently leave an already active scheme.
    if (auto next = reader_())
        publish(std::move(*next));
}

bool WindowsContrastPreference::nativeEventFilter(const QByteArray& eventType, void* message,
                                                   qintptr*) {
    if ((eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") || !message)
        return false;
    const auto kind = static_cast<MSG*>(message)->message;
    if (kind != WM_SETTINGCHANGE && kind != WM_SYSCOLORCHANGE && kind != WM_THEMECHANGED)
        return false;
    if (!refreshPending_) {
        refreshPending_ = true;
        QMetaObject::invokeMethod(this, [this] {
            refreshPending_ = false;
            refresh();
        }, Qt::QueuedConnection);
    }
    return false;
}

} // namespace xdstarter
