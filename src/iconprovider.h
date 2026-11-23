#pragma once

#include <QQuickImageProvider>

// Serves the line icons in icons/ tinted to any color, as
// image://icon/<name>?color=<#rrggbb>. Tinting the SVG itself (rather than a
// shader effect) works on every renderer, including software rendering.
class IconProvider : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
