#include "reflow.h"

#include "booktext.h"
#include "cleanpageprovider.h"
#include "zipwriter.h"

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Reflow {

namespace {

QString key(const QString &text) {
    QString out;
    for (const QChar c : text.toLower())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

// Text for XHTML: escaped, without the characters XML forbids (scans leave
// U+FFFE and control characters in their text, and one breaks the book).
QString escape(const QString &text) {
    QString clean;
    clean.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        const ushort u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            clean += c;
            clean += text[++i];
            continue;
        }
        if (c.isSurrogate() || u == 0xFFFE || u == 0xFFFF || (u < 0x20 && u != '\t' && u != '\n' && u != '\r'))
            continue;
        clean += c;
    }
    return clean.toHtmlEscaped();
}

int letters(const QString &text) {
    int n = 0;
    for (const QChar c : text)
        n += c.isLetter();
    return n;
}

// Lines that start an item of their own: question numbers, answer
// choices, roman numerals, bullets.
const QRegularExpression &itemStart() {
    static const QRegularExpression re(R"(^(\d{1,3}[.)]\s|[A-H][.)]\s|[IVX]{1,4}[.)]\s|[•●▪■\-–]\s))");
    return re;
}
const QRegularExpression &choiceStart() {
    static const QRegularExpression re(R"(^[A-H][.)]\s)");
    return re;
}
// A page number alone, or with the running footer beside it.
const QRegularExpression &pageNumber() {
    static const QRegularExpression re(R"(^(\d{1,4}|\d{1,4}\s*[|•·].*|.*[|•·]\s*\d{1,4})$)");
    return re;
}

// The page's lines with their boxes. The page's text runs, joined into
// lines, usually match its text lines one for one; when they do not, each
// line is measured on its own (slower).
QList<Line> pageLines(QPdfDocument &doc, int page, const QPdfSelection &all) {
    const QString text = all.text();
    struct Raw { QString text; int start; };
    QList<Raw> raw;
    int pos = 0;
    for (const QString &line : text.split('\n')) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            raw.append({t, pos + int(line.indexOf(t))});
        pos += int(line.size()) + 1;
    }
    QList<QRectF> joined;
    for (const QPolygonF &run : all.bounds()) {
        const QRectF box = run.boundingRect();
        if (box.isEmpty())
            continue;
        if (!joined.isEmpty()) {
            QRectF &line = joined.last();
            const qreal overlap = std::min(line.bottom(), box.bottom()) - std::max(line.top(), box.top());
            const qreal gap = box.left() - line.right();
            if (overlap > std::min(line.height(), box.height()) * 0.5 && gap < box.height() * 2.5 && gap > -box.height()) {
                line = line.united(box);
                continue;
            }
        }
        joined << box;
    }
    QList<Line> out;
    const bool matched = joined.size() == raw.size();
    for (int i = 0; i < raw.size(); ++i) {
        const QRectF box = matched ? joined[i]
                                   : doc.getSelectionAtIndex(page, raw[i].start, int(raw[i].text.size())).boundingRectangle();
        out.append({raw[i].text, box});
    }
    return out;
}

// Headings are in capitals or title case throughout. A large line that
// mixes a code in capitals with ordinary words ("SOR pH 6.90…", "Neutrophilic
// endocytosis SOP …") is a table or label read large, not a heading.
bool looksLikeHeading(const QString &text) {
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    // Mostly letters: "SOR pH 6.90, Pco, 60 mm Hg" is a table row.
    int nonSpace = 0;
    for (const QChar c : text)
        nonSpace += !c.isSpace();
    if (letters(text) < nonSpace * 0.7)
        return false;
    if (words.isEmpty() || (words[0].size() == 1 && words[0] != "A" && words[0] != "I" && !words[0][0].isDigit()))
        return false;
    bool caps = false, lower = false;
    for (const QString &w : words) {
        if (letters(w) < 2)
            continue;
        if (w == w.toUpper())
            caps = true;
        else if (w[0].isLower() && w.size() > 3)
            lower = true;
    }
    return !(caps && lower);
}

} // namespace

QList<Block> blocks(const QList<Line> &lines, qreal bodyHeight, qreal pageWidth, const QStringList &skip) {
    QList<Block> out;
    if (lines.isEmpty() || bodyHeight <= 0)
        return out;
    const qreal middle = pageWidth / 2;
    // Two columns when a good share of lines start right of the middle.
    int rightStarts = 0;
    for (const Line &l : lines)
        rightStarts += l.box.left() > middle - bodyHeight;
    const bool twoColumns = rightStarts > lines.size() / 5 && rightStarts < lines.size() * 4 / 5;
    auto columnOf = [&](const Line &l) { return twoColumns && l.box.left() > middle - bodyHeight ? 1 : 0; };

    const Line *previous = nullptr;
    qreal paragraphLeft = 0;
    for (const Line &line : lines) {
        const QString text = line.text.simplified();
        if (text.isEmpty() || skip.contains(key(text)) || pageNumber().match(text).hasMatch())
            continue;
        const qreal ratio = line.box.height() / bodyHeight;
        const int column = columnOf(line);
        const bool heading = ratio >= 1.15 && text.size() <= 120 && letters(text) >= 3 && !text.endsWith('.')
                          && looksLikeHeading(text);
        Block *last = out.isEmpty() ? nullptr : &out.last();

        if (heading) {
            // A heading that wraps continues on the next line at about the
            // same size.
            if (last && last->kind == Block::Heading && previous && previous->box.bottom() + bodyHeight * 1.2 > line.box.top()
                && last->column == column) {
                last->text += " " + text;
            } else {
                Block b;
                b.kind = Block::Heading;
                b.text = text;
                b.level = ratio >= 1.7 ? 2 : 3;
                b.column = column;
                b.top = line.box.top();
                out << b;
            }
            previous = &line;
            continue;
        }

        // Does this line continue the paragraph above?
        bool continues = last && last->kind != Block::Heading && last->kind != Block::Figure && previous
                      && last->column == column;
        if (continues) {
            const qreal gap = line.box.top() - previous->box.bottom();
            // A wide gap, a jump back up (a new column), an indented first
            // line, or the start of a numbered item begins something new.
            if (gap > bodyHeight * 0.75 || line.box.top() < previous->box.top() - bodyHeight * 0.5
                || itemStart().match(text).hasMatch()
                || (line.box.left() - paragraphLeft > bodyHeight * 1.2 && previous->text.trimmed().endsWith('.')))
                continues = false;
        }
        if (continues) {
            // Words broken across lines are joined back up.
            if (last->text.endsWith('-') && text[0].isLower())
                last->text = last->text.left(last->text.size() - 1) + text;
            else
                last->text += " " + text;
        } else {
            Block b;
            b.kind = choiceStart().match(text).hasMatch() ? Block::Choice : Block::Paragraph;
            b.text = text;
            b.column = column;
            b.top = line.box.top();
            out << b;
            paragraphLeft = line.box.left();
        }
        previous = &line;
    }
    // In reading order: a whole column, then the next.
    if (twoColumns)
        std::stable_sort(out.begin(), out.end(), [](const Block &a, const Block &b) { return a.column < b.column; });
    // A question number on a line of its own ("10.") starts the next block;
    // stray underscores after a choice letter ("B. _ Its") are scan noise.
    static const QRegularExpression loneNumber(R"(^\d{1,3}[.)]$)");
    static const QRegularExpression underscored(R"(^([A-H][.)])\s*_+\s*)");
    QList<Block> tidy;
    QString pending;
    for (Block b : std::as_const(out)) {
        if (b.kind == Block::Paragraph && loneNumber.match(b.text).hasMatch()) {
            pending = b.text;
            continue;
        }
        b.text.replace(underscored, "\\1 ");
        if (!pending.isEmpty() && b.kind != Block::Heading) {
            b.text = pending + " " + b.text;
            pending.clear();
        }
        tidy << b;
    }
    return tidy;
}

QList<QRectF> figureRegions(const QList<Line> &lines, qreal bodyHeight, const QSizeF &page) {
    QList<QRectF> out;
    if (bodyHeight <= 0)
        return out;
    const qreal middle = page.width() / 2;
    int rightStarts = 0;
    for (const Line &l : lines)
        rightStarts += l.box.left() > middle - bodyHeight;
    const bool twoColumns = rightStarts > lines.size() / 5 && rightStarts < lines.size() * 4 / 5;
    const int columns = twoColumns ? 2 : 1;
    // The text area: from the first to the last line on the page.
    qreal top = page.height(), bottom = 0;
    for (const Line &l : lines) {
        top = std::min(top, l.box.top());
        bottom = std::max(bottom, l.box.bottom());
    }
    if (lines.isEmpty())
        return out;
    for (int c = 0; c < columns; ++c) {
        const qreal left = twoColumns ? (c == 0 ? 0 : middle) : 0;
        const qreal right = twoColumns ? (c == 0 ? middle : page.width()) : page.width();
        QList<QPair<qreal, qreal>> spans;
        for (const Line &l : lines) {
            const bool inColumn = !twoColumns || ((l.box.left() > middle - bodyHeight) == (c == 1))
                               || l.box.width() > page.width() * 0.6;   // full-width lines block both columns
            if (inColumn)
                spans.append({l.box.top(), l.box.bottom()});
        }
        std::sort(spans.begin(), spans.end());
        qreal reach = top;
        for (const auto &[from, to] : spans) {
            if (from - reach > bodyHeight * 4)
                out << QRectF(left, reach + 2, right - left, from - reach - 4);
            reach = std::max(reach, to);
        }
        if (bottom - reach > bodyHeight * 4)
            out << QRectF(left, reach + 2, right - left, bottom - reach - 4);
    }
    return out;
}

QByteArray build(const QString &pdfPath, const Options &options, const Progress &progress,
                 const std::atomic_bool &cancel) {
    QPdfDocument doc;
    if (doc.load(pdfPath) != QPdfDocument::Error::None)
        return {};
    const int count = doc.pageCount();

    // Running heads ("MCAT WORKOUT") come out of every page's text.
    QStringList texts;
    for (int i = 0; i < count && !cancel; ++i)
        texts << doc.getAllText(i).text();
    if (cancel)
        return {};
    QStringList skip;
    for (const QString &head : BookText::runningHeads(texts))
        skip << key(head);
    // Any line on a tenth of the pages or more (a footer such as "For more
    // free content…") is page furniture too; numbers are ignored so "146 |
    // footer" and "147 | footer" count as one.
    QHash<QString, int> seen;
    for (const QString &page : texts) {
        QSet<QString> onPage;
        for (const QString &line : page.split('\n')) {
            QString k = key(line);
            k.remove(QRegularExpression("\\d"));
            if (k.size() >= 8)
                onPage.insert(k);
        }
        for (const QString &k : std::as_const(onPage))
            ++seen[k];
    }
    QSet<QString> furniture;
    for (auto it = seen.cbegin(); it != seen.cend(); ++it)
        if (it.value() >= std::max(5, count / 10))
            furniture.insert(it.key());

    // Chapters from the outline: where each starts, and its sections by page.
    struct Entry { QString label; QString title; int depth; int page; int index; };
    QList<Entry> entries;
    int chapterDepth = 100;
    static const QRegularExpression chapterLabel(R"(^chapter\b)", QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < options.outline.size(); ++i) {
        const QVariantMap e = options.outline[i].toMap();
        Entry entry{e["label"].toString(), e.value("title", e["label"]).toString(), e["depth"].toInt(),
                    e["target"].toInt(), i};
        if (entry.page < 0 || entry.page >= count)
            continue;
        entries << entry;
        if (chapterLabel.match(entry.label).hasMatch())
            chapterDepth = std::min(chapterDepth, entry.depth);
    }
    if (chapterDepth == 100)
        for (const Entry &e : std::as_const(entries))
            chapterDepth = std::min(chapterDepth, e.depth);
    QList<int> chapterStarts;   // first page of each chapter file
    QHash<int, QList<Entry>> entriesOnPage;
    for (const Entry &e : std::as_const(entries)) {
        entriesOnPage[e.page] << e;
        if (e.depth <= chapterDepth && (chapterStarts.isEmpty() || chapterStarts.last() != e.page))
            chapterStarts << e.page;
    }
    std::sort(chapterStarts.begin(), chapterStarts.end());
    // Without an outline, a file every 30 pages keeps them a sensible size.
    if (chapterStarts.isEmpty())
        for (int p = 0; p < count; p += 30)
            chapterStarts << p;
    if (chapterStarts.first() != 0)
        chapterStarts.prepend(0);

    ZipWriter zip;
    zip.add("mimetype", "application/epub+zip", false);
    zip.add("META-INF/container.xml", R"(<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>)");
    zip.add("OEBPS/style.css", R"(body { margin: 0 4%; line-height: 1.5; }
h1 { font-size: 1.6em; margin: 1.2em 0 0.8em; }
h2 { font-size: 1.3em; margin: 1.4em 0 0.6em; }
h3 { font-size: 1.1em; margin: 1.2em 0 0.4em; }
p { margin: 0 0 0.8em; }
p.choice { margin: 0 0 0.3em 1.5em; text-indent: -1.5em; }
figure { margin: 1em 0; text-align: center; }
figure img { max-width: 100%; height: auto; }
.page { display: block; height: 0; })");

    QStringList manifest, spine, files;
    QStringList fileTitles;
    QHash<int, QString> pageFile;     // page → the file it went into
    QHash<int, QString> entryAnchor;  // outline entry → "file#anchor"
    int images = 0, pagesDone = 0;

    for (int c = 0; c < chapterStarts.size() && !cancel; ++c) {
        const int first = chapterStarts[c];
        const int last = c + 1 < chapterStarts.size() ? chapterStarts[c + 1] - 1 : count - 1;
        const QString file = QStringLiteral("part%1.xhtml").arg(c + 1, 3, 10, QChar('0'));
        QString title = options.title;
        for (const Entry &e : entriesOnPage.value(first))
            if (e.depth <= chapterDepth) {
                title = e.label;
                break;
            }
        QList<Block> chapter;
        for (int page = first; page <= last && !cancel; ++page) {
            pageFile[page] = file;
            Block mark;
            mark.kind = Block::PageMark;
            mark.anchor = page + 1;
            chapter << mark;
            const QPdfSelection all = doc.getAllText(page);
            const QList<Line> lines = pageLines(doc, page, all);
            const QSizeF size = doc.pagePointSize(page);
            QList<qreal> heights;
            for (const Line &l : lines)
                if (l.box.width() > size.width() * 0.25)
                    heights << l.box.height();
            std::sort(heights.begin(), heights.end());
            const qreal bodyHeight = heights.isEmpty() ? 0 : heights[heights.size() / 2];

            QList<Line> kept;
            for (const Line &l : lines) {
                QString k = key(l.text);
                k.remove(QRegularExpression("\\d"));
                if (!furniture.contains(k))
                    kept << l;
            }
            QList<Block> pageBlocks = blocks(kept, bodyHeight, size.width(), skip);
            // Name headings after the outline: each entry listed on this page
            // finds its printed line (a heading, or a paragraph if it was
            // printed barely larger, or the start of one if it runs into its
            // text), which becomes the heading with the outline's numbered
            // label and level.
            const QList<Entry> here = entriesOnPage.value(page);
            QSet<int> placed;
            int from = 0;
            for (const Entry &e : here) {
                const QString title = key(e.title);
                if (title.isEmpty())
                    continue;
                for (int i = from; i < pageBlocks.size(); ++i) {
                    Block &b = pageBlocks[i];
                    if (b.kind == Block::Figure || b.kind == Block::Choice || b.anchor >= 0)
                        continue;
                    const bool whole = BookText::sameHeading(e.title, b.text);
                    // Run in: the paragraph starts with the heading's words.
                    int cut = -1;
                    if (!whole && b.kind == Block::Paragraph && key(b.text).startsWith(title)) {
                        int matched = 0;
                        for (int c = 0; c < b.text.size() && matched < title.size(); ++c) {
                            if (b.text[c].isLetterOrNumber())
                                ++matched;
                            cut = c + 1;
                        }
                    }
                    if (!whole && cut < 0)
                        continue;
                    const int level = e.depth <= chapterDepth ? 1 : e.depth == chapterDepth + 1 ? 2 : 3;
                    if (cut > 0 && cut < b.text.size()) {
                        Block rest = b;
                        rest.text = b.text.mid(cut).trimmed();
                        if (!rest.text.isEmpty() && !rest.text[0].isLetterOrNumber())
                            rest.text = rest.text.mid(1).trimmed();
                        pageBlocks.insert(i + 1, rest);
                    }
                    Block &heading = pageBlocks[i];
                    heading.kind = Block::Heading;
                    heading.text = e.label;
                    heading.level = level;
                    heading.anchor = e.index;
                    placed.insert(e.index);
                    entryAnchor[e.index] = QStringLiteral("%1#s%2").arg(file).arg(e.index);
                    from = i + 1;
                    break;
                }
            }
            // Outline entries without a printed heading here (a chapter on
            // a title page that is a picture) still get their heading.
            bool lead = false;
            for (const Entry &e : here) {
                if (placed.contains(e.index))
                    continue;
                Block h;
                h.kind = Block::Heading;
                h.text = e.label;
                h.level = e.depth <= chapterDepth ? 1 : e.depth == chapterDepth + 1 ? 2 : 3;
                h.anchor = e.index;
                chapter << h;
                lead = true;
                entryAnchor[e.index] = QStringLiteral("%1#s%2").arg(file).arg(e.index);
            }

            // Figures: regions without text that have ink in them, cut from
            // the cleaned page. A page with no text at all is one picture,
            // unless it is only a chapter title already given above.
            QList<QRectF> regions = figureRegions(lines, bodyHeight, size);
            if (lines.isEmpty() && !lead)
                regions << QRectF(QPointF(0, 0), size);
            QImage pageImage;
            if (!regions.isEmpty())
                pageImage = CleanPageProvider::cleanedPage(pdfPath, page, "clean", 1100, &cancel);
            if (!pageImage.isNull()) {
                const qreal scale = pageImage.width() / size.width();
                for (const QRectF &r : std::as_const(regions)) {
                    const QRect px = QRectF(r.x() * scale, r.y() * scale, r.width() * scale, r.height() * scale)
                                         .toAlignedRect().intersected(pageImage.rect());
                    if (px.width() < 40 || px.height() < 30)
                        continue;
                    QImage crop = pageImage.copy(px);
                    // Enough ink to be a picture rather than empty margin.
                    int dark = 0, total = 0;
                    for (int y = 0; y < crop.height(); y += 3)
                        for (int x = 0; x < crop.width(); x += 3, ++total)
                            dark += qGray(crop.pixel(x, y)) < 180;
                    if (total == 0 || dark < total * 0.01)
                        continue;
                    // Trim the empty margins around it.
                    QRect ink;
                    for (int y = 0; y < crop.height(); y += 2)
                        for (int x = 0; x < crop.width(); x += 2)
                            if (qGray(crop.pixel(x, y)) < 200)
                                ink = ink.united(QRect(x, y, 2, 2));
                    if (ink.isValid())
                        crop = crop.copy(ink.adjusted(-8, -8, 8, 8).intersected(crop.rect()));
                    QByteArray jpeg;
                    QBuffer buffer(&jpeg);
                    buffer.open(QIODevice::WriteOnly);
                    crop.save(&buffer, "JPG", 85);
                    const QString name = QStringLiteral("images/fig%1.jpg").arg(++images, 5, 10, QChar('0'));
                    zip.add("OEBPS/" + name, jpeg, false);
                    manifest << QStringLiteral("<item id=\"img%1\" href=\"%2\" media-type=\"image/jpeg\"/>").arg(images).arg(name);
                    Block f;
                    f.kind = Block::Figure;
                    f.text = name;
                    f.column = r.x() > size.width() / 2 - 1 ? 1 : 0;
                    f.top = r.top();
                    // Placed after the last block above it in its column.
                    int at = 0;
                    for (int i = 0; i < pageBlocks.size(); ++i)
                        if (pageBlocks[i].column == f.column && pageBlocks[i].top < f.top)
                            at = i + 1;
                        else if (pageBlocks[i].column > f.column && at == 0)
                            at = i;
                    pageBlocks.insert(std::min<int>(at, int(pageBlocks.size())), f);
                }
            }

            chapter += pageBlocks;
            if (progress)
                progress(++pagesDone, count);
        }
        // A paragraph broken by a column or page (it ends mid-sentence and
        // the next starts in lowercase) is joined back together.
        static const QRegularExpression sentenceEnd(R"re([.?!:;)"”’\]]$)re");
        QList<Block> joined;
        int lastParagraph = -1;
        for (const Block &b : std::as_const(chapter)) {
            if (b.kind == Block::Paragraph && lastParagraph >= 0 && !b.text.isEmpty() && b.text[0].isLower()
                && !sentenceEnd.match(joined[lastParagraph].text).hasMatch()) {
                QString &text = joined[lastParagraph].text;
                text = text.endsWith('-') ? text.left(text.size() - 1) + b.text : text + " " + b.text;
                continue;
            }
            joined << b;
            if (b.kind == Block::Paragraph)
                lastParagraph = int(joined.size()) - 1;
            else if (b.kind != Block::PageMark)
                lastParagraph = -1;
        }
        QString body;
        for (const Block &b : std::as_const(joined)) {
            switch (b.kind) {
            case Block::PageMark:
                body += QStringLiteral("<span class=\"page\" id=\"p%1\"></span>\n").arg(b.anchor);
                break;
            case Block::Heading: {
                const QString id = b.anchor >= 0 ? QStringLiteral(" id=\"s%1\"").arg(b.anchor) : QString();
                body += QStringLiteral("<h%1%2>%3</h%1>\n").arg(b.level).arg(id, escape(b.text));
                break;
            }
            case Block::Choice:
                body += QStringLiteral("<p class=\"choice\">%1</p>\n").arg(escape(b.text));
                break;
            case Block::Figure:
                body += QStringLiteral("<figure><img src=\"%1\" alt=\"\"/></figure>\n").arg(b.text);
                break;
            default:
                body += QStringLiteral("<p>%1</p>\n").arg(escape(b.text));
            }
        }
        const QString xhtml = QStringLiteral(R"(<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>%1</title><link rel="stylesheet" href="style.css"/></head>
<body>
%2</body></html>)").arg(escape(title), body);
        zip.add("OEBPS/" + file, xhtml.toUtf8());
        manifest << QStringLiteral("<item id=\"x%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>").arg(c + 1).arg(file);
        spine << QStringLiteral("<itemref idref=\"x%1\"/>").arg(c + 1);
        files << file;
        fileTitles << title;
    }
    if (cancel)
        return {};

    // The contents: the outline, nested, pointing at the headings (or the
    // page an entry is on, if its heading was not found).
    QString nav;
    if (!entries.isEmpty()) {
        // Nested lists, one level at a time: an entry can only go one level
        // deeper than the one before it, or the lists would not nest.
        int base = entries.first().depth;
        for (const Entry &e : std::as_const(entries))
            base = std::min(base, e.depth);
        int depth = -1;
        for (const Entry &e : std::as_const(entries)) {
            const int d = std::clamp(e.depth - base, 0, depth + 1);
            const QString href = entryAnchor.value(e.index, QStringLiteral("%1#p%2").arg(pageFile.value(e.page)).arg(e.page + 1));
            if (d > depth) {
                nav += "<ol>";
            } else {
                nav += "</li>";
                for (int k = d; k < depth; ++k)
                    nav += "</ol></li>";
            }
            nav += QStringLiteral("<li><a href=\"%1\">%2</a>").arg(href, escape(e.label));
            depth = d;
        }
        nav += "</li>";
        for (int k = 0; k < depth; ++k)
            nav += "</ol></li>";
        nav += "</ol>";
    } else {
        nav += "<ol>";
        for (int i = 0; i < files.size(); ++i)
            nav += QStringLiteral("<li><a href=\"%1\">%2</a></li>").arg(files[i], escape(fileTitles[i]));
        nav += "</ol>";
    }
    zip.add("OEBPS/nav.xhtml", QStringLiteral(R"(<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head>
<body><nav epub:type="toc">%1</nav></body></html>)").arg(nav).toUtf8());
    zip.add("OEBPS/book.opf", QStringLiteral(R"(<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">shelf-text-%1</dc:identifier>
<dc:title>%2</dc:title><dc:creator>%3</dc:creator><dc:language>en</dc:language></metadata>
<manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
<item id="css" href="style.css" media-type="text/css"/>
%4</manifest>
<spine>%5</spine></package>)")
                                     .arg(QString::number(qHash(pdfPath), 16), escape(options.title + " (text version)"),
                                          escape(options.author), manifest.join('\n'), spine.join('\n'))
                                     .toUtf8());
    return zip.finish();
}

} // namespace Reflow
