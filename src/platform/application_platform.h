#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace xdstarter {

// Describes a Shell item, never an executable command line. Arguments and the
// working directory remain owned by the shortcut itself.
struct LaunchTarget {
    QString id;
    QString type;
    QString name;
    QString symbol;
    QString shortcutPath;
    QString aumid;
    QString iconKey;
    // Catalog-only hierarchy. It is never persisted as part of a launch target.
    QString parentFolderId;
};

struct CatalogSnapshot {
    QVector<LaunchTarget> applications;
    QStringList warnings;
    bool success = false;
    // Transient proof from this scan only: suppressed shortcut id -> displayed original id.
    // Neither stable target identities nor persisted layouts are rewritten.
    QHash<QString, QString> equivalentTargetIds;
};

struct LaunchResult {
    bool success = false;
    QString message;
    QString errorCode;
};

// Stable identity includes the complete shortcut path. Two shortcuts to the
// same executable may have different arguments and must remain distinguishable.
[[nodiscard]] QString applicationTargetId(const QString& type, const QString& identity);
[[nodiscard]] bool isValidApplicationTarget(const LaunchTarget& target);
[[nodiscard]] bool isValidCatalogEntry(const LaunchTarget& target);
[[nodiscard]] QVector<LaunchTarget>
normalizedApplications(const QVector<LaunchTarget>& applications);

class ApplicationPlatform {
  public:
    virtual ~ApplicationPlatform() = default;

    // Potentially blocking Shell/IO operations. The caller runs these on its
    // dedicated worker; returned values and QImages have no GUI ownership.
    [[nodiscard]] virtual CatalogSnapshot scanApplications() = 0;
    [[nodiscard]] virtual QImage loadIcon(const LaunchTarget& target, int size) = 0;
    [[nodiscard]] virtual LaunchResult launch(const LaunchTarget& target) = 0;
    [[nodiscard]] virtual LaunchResult checkAvailability(const LaunchTarget& target) = 0;
};

[[nodiscard]] std::unique_ptr<ApplicationPlatform> createApplicationPlatform();

} // namespace xdstarter
