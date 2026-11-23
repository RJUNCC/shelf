#pragma once

#include <QByteArray>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <atomic>
#include <functional>

// Turns a PDF (a scan especially) into a clean, reflowable EPUB: paragraphs
// rebuilt from the page's lines, headings found by their size and named by
// the outline, figures and tables kept as pictures cut from the cleaned
// page, running heads and page numbers left out. Opened in the EPUB reader,
// it takes any theme and text size, and highlights and notes work as usual.
namespace Reflow {

// One line of a page: its text and where it sits, in PDF points.
struct Line {
    QString text;
    QRectF box;
};

// A piece of the flowing text: a paragraph, a heading, an answer choice,
// or a figure cut from the page.
struct Block {
    enum Kind { Paragraph, Heading, Choice, Figure, PageMark };
    Kind kind = Paragraph;
    QString text;
    int level = 3;        // headings: 1 chapter, 2 section, 3 subsection
    int column = 0;
    qreal top = 0;        // where it starts on the page, for placing figures
    QRectF figure;        // figures: the region of the page, in points
    int anchor = -1;      // headings named by the outline: the entry's index
};

// The blocks of one page from its lines, in reading order. `bodyHeight` is
// the height of an ordinary line; `skip` holds lines to leave out (running
// heads), normalized.
QList<Block> blocks(const QList<Line> &lines, qreal bodyHeight, qreal pageWidth, const QStringList &skip = {});

// Regions of a page with no text that may hold a figure, per column, in
// points: gaps between lines taller than a few lines of text.
QList<QRectF> figureRegions(const QList<Line> &lines, qreal bodyHeight, const QSizeF &page);

struct Options {
    QString title;
    QString author;
    // The book's numbered outline: [{label, depth, target, number, title}].
    QVariantList outline;
};

using Progress = std::function<void(int done, int total)>;

// The whole EPUB, or empty if the PDF cannot be read or `cancel` was set.
QByteArray build(const QString &pdfPath, const Options &options, const Progress &progress,
                 const std::atomic_bool &cancel);

} // namespace Reflow
