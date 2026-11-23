#include "pdfgeometry.h"

#include "cleanpageprovider.h"

#include <QPdfDocument>
#include <QPdfSelection>
#include <QRectF>
#include <algorithm>
#include <cmath>

QVariantList PdfGeometry::lineRects(const QList<QPolygonF> &polygons) const {
    QList<QRectF> boxes;
    for (const QPolygonF &polygon : polygons) {
        const QRectF box = polygon.boundingRect();
        if (box.width() > 0 && box.height() > 0)
            boxes << box;
    }
    // Reading order is what the PDF gives; keep it, and grow a line while
    // each box sits beside the last one at the same height. A box on
    // another line, or across a column gap, starts a new rectangle.
    QList<QRectF> lines;
    for (const QRectF &box : std::as_const(boxes)) {
        if (!lines.isEmpty()) {
            QRectF &line = lines.last();
            const qreal overlap = std::min(line.bottom(), box.bottom()) - std::max(line.top(), box.top());
            const qreal height = std::min(line.height(), box.height());
            const qreal gap = box.left() - line.right();
            if (overlap > height * 0.5 && gap < height * 2.5 && box.right() > line.left() - height) {
                line = line.united(box);
                continue;
            }
        }
        lines << box;
    }
    QVariantList out;
    for (const QRectF &r : std::as_const(lines)) {
        auto round = [](qreal v) { return std::round(v * 100) / 100; };
        out << QVariant(QVariantList{round(r.left()), round(r.top()), round(r.right()), round(r.top()),
                                     round(r.right()), round(r.bottom()), round(r.left()), round(r.bottom())});
    }
    return out;
}

PdfGeometry::~PdfGeometry() = default;

bool PdfGeometry::open(const QString &path) {
    // One document stays open, since the reader asks about the same book.
    if (m_path == path && m_document)
        return true;
    m_document = std::make_unique<QPdfDocument>();
    m_path = path;
    m_textPage = -1;
    if (m_document->load(path) != QPdfDocument::Error::None) {
        m_document.reset();
        return false;
    }
    return true;
}

QString PdfGeometry::pageText(const QString &path, int page) {
    if (!open(path) || page < 0 || page >= m_document->pageCount())
        return {};
    if (m_textPage != page) {
        m_text = m_document->getAllText(page).text();
        m_textPage = page;
    }
    return m_text;
}

bool PdfGeometry::hasTextLayer(const QString &path) {
    if (!open(path))
        return false;
    const int pages = m_document->pageCount();
    int samples = 0, withText = 0;
    for (int k = 1; k <= 9 && pages > 0; ++k) {
        const int page = std::min(pages - 1, pages * k / 10);
        int letters = 0;
        for (const QChar c : m_document->getAllText(page).text())
            letters += c.isLetter();
        ++samples;
        withText += letters >= 300;
    }
    return samples > 0 && withText * 2 >= samples;
}

int PdfGeometry::indexAt(int page, QPointF point) {
    // Qt returns nothing for a selection that starts and ends on the same
    // letter, so ask for a span reaching past it: its first letter is the
    // one under the point. At the end of a line, span leftwards and take
    // the last letter instead. Either must actually contain the point (or
    // come close), or the click was not on text.
    auto isNear = [&](int index) {
        const QRectF box = m_document->getSelectionAtIndex(page, index, 1).boundingRectangle();
        return !box.isEmpty() && box.adjusted(-3, -3, 3, 3).contains(point);
    };
    const QPdfSelection right = m_document->getSelection(page, point, point + QPointF(12, 0));
    if (right.isValid() && !right.text().isEmpty() && isNear(right.startIndex()))
        return right.startIndex();
    const QPdfSelection left = m_document->getSelection(page, point - QPointF(12, 0), point);
    if (left.isValid() && !left.text().isEmpty() && isNear(left.endIndex()))
        return left.endIndex();
    return -1;
}

QVariantMap PdfGeometry::select(const QString &path, int page, QPointF from, QPointF to, const QString &unit) {
    const QString text = pageText(path, page);
    if (text.isEmpty())
        return {};
    auto isWord = [&](int i) { return i >= 0 && i < text.size() && (text[i].isLetterOrNumber() || text[i] == '\''
                                                                     || text[i] == QChar(0x2019) || text[i] == '-'); };
    int start = -1, end = -1;   // end is one past the last character
    if (unit == "drag") {
        const QPdfSelection s = m_document->getSelection(page, from, to);
        if (!s.isValid() || s.text().trimmed().isEmpty())
            return {};
        start = s.startIndex();
        end = s.endIndex() + 1;
        // Whole words at both ends.
        while (isWord(start - 1))
            --start;
        while (isWord(end))
            ++end;
    } else {
        const int at = indexAt(page, from);
        if (at < 0)
            return {};
        start = at;
        end = at + 1;
        if (unit == "sentence") {
            // Back to just after the previous sentence's end, forward to
            // this one's, staying within the paragraph.
            auto endsSentence = [&](int i) {
                return i >= 0 && i < text.size() && (text[i] == '.' || text[i] == '!' || text[i] == '?')
                    && (i + 1 >= text.size() || text[i + 1].isSpace());
            };
            auto paragraphBreak = [&](int i) { return text.mid(i, 2) == "\n\n" || text.mid(i, 4) == "\r\n\r\n"; };
            while (start > 0 && !endsSentence(start - 1) && !paragraphBreak(start - 1))
                --start;
            while (start < at && text[start].isSpace())
                ++start;
            while (end < text.size() && !endsSentence(end - 1) && !paragraphBreak(end))
                ++end;
        } else {
            while (isWord(start - 1))
                --start;
            while (isWord(end))
                ++end;
        }
    }
    start = std::max(0, start);
    end = std::min(int(text.size()), end);
    if (end <= start)
        return {};
    const QPdfSelection chosen = m_document->getSelectionAtIndex(page, start, end - start);
    const QString chosenText = chosen.text().simplified();
    if (chosenText.isEmpty())
        return {};
    return {{"text", chosenText}, {"rects", lineRects(chosen.bounds())}};
}

void PdfGeometry::cleanAhead(const QString &path, int first, int last, const QString &mode, int width) {
    CleanPageProvider::cleanAhead(path, first, last, mode, width);
}
