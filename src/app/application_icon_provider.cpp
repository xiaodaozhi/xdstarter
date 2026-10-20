#include "application_icon_provider.h"

#include <QPainter>

#include <algorithm>

namespace xdstarter {

ApplicationIconProvider::ApplicationIconProvider(ApplicationCatalog* catalog)
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQmlImageProviderBase::ForceAsynchronousImageLoading),
      catalog_(catalog) {}

QImage ApplicationIconProvider::requestImage(const QString& id, QSize* size,
                                             const QSize& requestedSize) {
    const auto key = id.section(QLatin1Char('?'), 0, 0);
    QImage image;
    if (catalog_) {
        image = catalog_->cachedIcon(key);
        if (image.isNull())
            catalog_->queueIconRequest(key);
    }
    if (image.isNull() || image.size() == QSize(1, 1)) {
        image = QImage(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#368BFF")));
        painter.drawRoundedRect(QRectF(6, 6, 52, 52), 10, 10);
        painter.setBrush(QColor(QStringLiteral("#F4F7FB")));
        for (const int y : {18, 36})
            for (const int x : {18, 36})
                painter.drawRoundedRect(QRectF(x, y, 10, 10), 2, 2);
    }
    if (size)
        *size = image.size();
    if (requestedSize.isValid())
        image = image.scaled(std::clamp(requestedSize.width(), 1, 256),
                             std::clamp(requestedSize.height(), 1, 256), Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    return image;
}

} // namespace xdstarter
