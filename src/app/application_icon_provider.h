#pragma once

#include "services/application_catalog.h"

#include <QPointer>
#include <QQuickImageProvider>

namespace xdstarter {

class ApplicationIconProvider final : public QQuickImageProvider {
  public:
    explicit ApplicationIconProvider(ApplicationCatalog* catalog);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

  private:
    QPointer<ApplicationCatalog> catalog_;
};

} // namespace xdstarter
