#include "interface_font.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QDebug>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    xdstarter::configureInterfaceFont();
    const auto arguments = application.arguments();
    if (arguments.size() != 3 && arguments.size() != 5) {
        qCritical() << "Usage: xdstarter_render_svg input.svg output.png [width height]";
        return 1;
    }
    QSvgRenderer renderer(arguments.at(1));
    if (!renderer.isValid()) {
        qCritical() << "Invalid SVG:" << arguments.at(1);
        return 1;
    }
    QSize size = renderer.defaultSize();
    if (arguments.size() == 5) {
        bool widthOk = false;
        bool heightOk = false;
        const int width = arguments.at(3).toInt(&widthOk);
        const int height = arguments.at(4).toInt(&heightOk);
        if (!widthOk || !heightOk) {
            qCritical() << "Invalid pixel dimensions";
            return 1;
        }
        size = QSize(width, height);
    }
    if (size.isEmpty() || size.width() > 8192 || size.height() > 8192) {
        qCritical() << "Pixel dimensions must be within 1..8192";
        return 1;
    }
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                          QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(QPointF(0, 0), size));
    painter.end();
    if (!image.save(arguments.at(2), "PNG")) {
        qCritical() << "Cannot save PNG:" << arguments.at(2);
        return 1;
    }
    return 0;
}
