#pragma once

#include <QObject>
#include <QPolygonF>
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>
#include <QPdfDocument>
#include <memory>

// Turns a PDF text selection's shape into something QML can keep: the
// selection arrives as a polygon per character, which JavaScript cannot
// read; this merges them into one rectangle per line, each as a flat list
// of corner coordinates in PDF points.
class PdfGeometry : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~PdfGeometry() override;

    Q_INVOKABLE QVariantList lineRects(const QList<QPolygonF> &polygons) const;

    // The text of one page of a PDF, for context around a selection.
    Q_INVOKABLE QString pageText(const QString &path, int page);
    // Whether the PDF carries real text (not just a watermark): sampled
    // from pages through the book. Scans without it have nothing to reflow.
    Q_INVOKABLE bool hasTextLayer(const QString &path);
    // The page's text a line at a time, in reading order: [{text, rects}],
    // rects as select() gives them (page points).
    Q_INVOKABLE QVariantList lines(const QString &path, int page);

    // Selects text on a page, between two points in PDF points, snapped out
    // to whole words so a drag that starts just before a word's first letter
    // still takes it. `unit` is "drag" (from → to), "word" (the word at
    // `from`, for a double click), or "sentence" (triple click). Returns
    // {text, rects} with rects as lineRects() gives them, or an empty map
    // when the points are not on text.
    // Cleans the pages around the reader's position ahead of time (see
    // CleanPageProvider::cleanAhead).
    Q_INVOKABLE void cleanAhead(const QString &path, int first, int last, const QString &mode, int width);

    Q_INVOKABLE QVariantMap select(const QString &path, int page, QPointF from, QPointF to, const QString &unit);

private:
    bool open(const QString &path);
    // The character index at a point, or −1.
    int indexAt(int page, QPointF point);

    std::unique_ptr<QPdfDocument> m_document;
    QString m_path;
    int m_textPage = -1;
    QString m_text;
};
