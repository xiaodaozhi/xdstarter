#pragma once

#include <QLocale>
#include <QString>

class QTranslator;

namespace xdstarter {
// SDK catalogs and windeployqt's merged catalogs have different base names.
bool loadQtTranslation(QTranslator& translator, const QLocale& locale, const QString& directory);
void initializeQtTranslation();
} // namespace xdstarter
