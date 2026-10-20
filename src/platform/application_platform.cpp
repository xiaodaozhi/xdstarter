#include "application_platform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>

#ifdef Q_OS_WIN
#include "windows/windows_application_platform.h"
#else
#include "stub/stub_application_platform.h"
#endif

namespace xdstarter {
namespace {

bool safeText(const QString& text, int maximumLength) {
    return !text.isEmpty() && text.size() <= maximumLength &&
           std::none_of(text.cbegin(), text.cend(),
                        [](QChar value) { return value.category() == QChar::Other_Control; });
}

QString canonicalIdentity(const QString& type, const QString& identity) {
    return type == QStringLiteral("shortcut")
               ? QDir::cleanPath(QString(identity).replace(QLatin1Char('\\'), QLatin1Char('/')))
                     .toCaseFolded()
               : identity.toCaseFolded();
}

} // namespace

QString applicationTargetId(const QString& type, const QString& identity) {
    const QByteArray digest = QCryptographicHash::hash(canonicalIdentity(type, identity).toUtf8(),
                                                       QCryptographicHash::Sha256)
                                  .toHex();
    return type + QLatin1Char('-') + QString::fromLatin1(digest);
}

bool isValidApplicationTarget(const LaunchTarget& target) {
    if (!safeText(target.name, 512) || target.symbol.size() > 8) {
        return false;
    }
    if (target.type == QStringLiteral("shortcut")) {
        static const QRegularExpression absolutePath(
            QStringLiteral("^(?:[A-Za-z]:/|//[^/?*<>|]+/[^/?*<>|]+/)"));
        const QString path =
            QString(target.shortcutPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
        static const QRegularExpression forbiddenPathCharacters(QStringLiteral("[<>\"|?*:]"));
        const QString pathBody = path.mid(path.startsWith(QStringLiteral("//")) ? 2 : 3);
        return safeText(path, 32760) && absolutePath.match(path).hasMatch() &&
               !forbiddenPathCharacters.match(pathBody).hasMatch() &&
               path.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive) &&
               target.aumid.isEmpty() &&
               target.id == applicationTargetId(target.type, target.shortcutPath);
    }
    if (target.type == QStringLiteral("packaged")) {
        static const QRegularExpression aumidPattern(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,199}![A-Za-z0-9][A-Za-z0-9._-]{0,127}\\z"));
        return target.aumid.size() <= 256 && aumidPattern.match(target.aumid).hasMatch() &&
               target.shortcutPath.isEmpty() &&
               target.id == applicationTargetId(target.type, target.aumid);
    }
    return false;
}

QVector<LaunchTarget> normalizedApplications(const QVector<LaunchTarget>& applications) {
    QVector<LaunchTarget> result;
    result.reserve(applications.size());
    QSet<QString> seen;
    for (LaunchTarget target : applications) {
        if (!isValidApplicationTarget(target) || seen.contains(target.id)) {
            continue;
        }
        target.iconKey = target.id;
        if (target.symbol.isEmpty()) {
            target.symbol = target.name.first(1);
        }
        seen.insert(target.id);
        result.append(std::move(target));
    }
    std::sort(result.begin(), result.end(),
              [](const LaunchTarget& left, const LaunchTarget& right) {
                  const int order = QString::localeAwareCompare(left.name, right.name);
                  return order == 0 ? left.id < right.id : order < 0;
              });
    return result;
}

std::unique_ptr<ApplicationPlatform> createApplicationPlatform() {
#ifdef Q_OS_WIN
    return std::make_unique<WindowsApplicationPlatform>();
#else
    return std::make_unique<StubApplicationPlatform>();
#endif
}

} // namespace xdstarter
