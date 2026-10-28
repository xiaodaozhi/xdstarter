#include "runtime_support.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QTranslator>

namespace xdstarter {
bool loadQtTranslation(QTranslator& translator, const QLocale& locale, const QString& directory) {
    return translator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), directory) ||
           translator.load(locale, QStringLiteral("qt"), QStringLiteral("_"), directory);
}

void initializeQtTranslation() {
    auto* translator = new QTranslator(QCoreApplication::instance());
    if (loadQtTranslation(*translator, QLocale::system(),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(translator);
    else
        delete translator;
}
} // namespace xdstarter
