#include "iconprovider.h"

#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QUrlQuery>

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString name = id.section('?', 0, 0);
    const QString color = QUrlQuery(id.section('?', 1)).queryItemValue("color", QUrl::FullyDecoded);

    static const QRegularExpression validName("^[a-z-]+$");
    static const QRegularExpression validColor("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$");
    QFile file(":/icons/" + name + ".svg");
    if (!validName.match(name).hasMatch() || !file.open(QIODevice::ReadOnly))
        return {};
    QByteArray svg = file.readAll();
    if (validColor.match(color).hasMatch())
        svg.replace("#ffffff", color.left(7).toLatin1());

    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(48, 48);
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    if (size)
        *size = target;
    return image;
}
