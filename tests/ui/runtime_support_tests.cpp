#include "app/runtime_support.h"

#include <QCoreApplication>
#include <QTranslator>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QTranslator translator;
    if (!xdstarter::loadQtTranslation(translator, QLocale(QStringLiteral("zh_CN")),
                                      QString::fromLocal8Bit(argv[1]))) {
        std::cerr << "Cannot load the SDK or deployed Qt translation catalog\n";
        return 1;
    }
    if (translator.translate("QPlatformTheme", "Cancel") != QStringLiteral("取消")) {
        std::cerr << "Loaded catalog does not translate the Qt Cancel action\n";
        return 1;
    }
    std::cout << "Qt translation catalog loaded and action text verified\n";
    return 0;
}
