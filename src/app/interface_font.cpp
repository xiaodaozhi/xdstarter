// src/app/interface_font.cpp

#include "interface_font.h"

#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>

namespace xdstarter {

void configureInterfaceFont() {
#ifdef Q_OS_WIN
    // Qt's offscreen font scanner omits Windows .ttc collections. Register the
    // installed YaHei faces so captures use the same glyphs as the native QPA.
    // Font files stay on the host and are never copied into the application.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen") &&
        !QFontDatabase::families().contains(QStringLiteral("Microsoft YaHei UI"))) {
        const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")));
        for (const auto* file : {"msyh.ttc", "msyhbd.ttc"})
            QFontDatabase::addApplicationFont(fonts.filePath(QLatin1String(file)));
    }
#endif
    const auto families = QFontDatabase::families();
    for (const auto* candidate : {"Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI",
                                  "Noto Sans CJK SC", "WenQuanYi Micro Hei"}) {
        const auto family = QString::fromLatin1(candidate);
        if (!families.contains(family, Qt::CaseInsensitive))
            continue;
        auto font = QGuiApplication::font();
        font.setFamily(family);
        // Store the resolved name, rather than an unavailable alias that can
        // silently fall back to the first font in the database.
        font.setFamily(QFontInfo(font).family());
        QGuiApplication::setFont(font);
        return;
    }
}

} // namespace xdstarter
