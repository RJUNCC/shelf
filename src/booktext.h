#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <atomic>
#include <climits>
#include <functional>

// Reading a book's text, and the clues in it that help find its chapters:
// printed page numbers, and the book's own table of contents. Everything
// here is plain functions so the outline builder can run them on a worker
// thread and the tests can run them without a model.
namespace BookText {

// The text of every page of a PDF. Pages with almost no text (scans whose
// text layer missed a chapter title, say) are read again with tesseract
// when `ocr` is set and tesseract is installed. `progress` is called with
// (page done, pages, true while OCR runs); setting `cancel` stops early.
//
// With `headings`, each page also gets the lines printed larger than its
// body text, as "ratio<TAB>text" lines (ratio = line height ÷ body height),
// which tell a model where the real headings are.
using Progress = std::function<void(int done, int total, bool ocr)>;
QStringList extractPdf(const QString &path, bool ocr, const Progress &progress, const std::atomic_bool &cancel,
                       QStringList *headings = nullptr);

// Whether a page has so little text that OCR is worth trying.
bool isSparse(const QString &text);

// How far the PDF's page indexes are ahead of the numbers printed on the
// pages (index − printed number), found by a vote over page headers and
// footers; INT_MIN when the pages carry no numbers to go by.
int pageNumberOffset(const QStringList &pages);

// Indexes of the pages holding the book's table of contents, if any.
QList<int> contentsPages(const QStringList &pages);

// Lines of a table of contents that end in a page number.
struct ContentsLine {
    QString title;
    int printedPage = 0;
};
QList<ContentsLine> contentsLines(const QString &contentsText, int pageCount);

// One line per page from `first` to `last` (indexes): "[page N] start of
// the page's text", N counting from 1 as a reader would.
// Pages listed in `contents` are shown only as the table of contents, so a
// model does not take its entries for chapters starting there.
// With `headings` (from extractPdf), each page's larger lines are listed
// first, labelled by size.
QString digest(const QStringList &pages, int first, int last, int chars = 240, const QList<int> &contents = {},
               const QStringList &headings = {});

// Outline entries ({label, depth, target}, target a page index) from a
// model's JSON answer {"entries": [{title, page, level}]}, page counted
// from 1. Entries with no title or a page out of range are dropped.
QVariantList parseEntries(const QByteArray &json, int pageCount);

// Two lists of entries as one, in page order, without repeats.
QVariantList mergeEntries(const QVariantList &a, const QVariantList &b);

// Lines that head most pages ("MCAT WORKOUT"), which are not chapters.
QStringList runningHeads(const QStringList &pages);

// Cleans up what a model found: strips running heads from titles and drops
// entries that were only a running head, keeps the first of a title that
// repeats on following pages, turns ALL-CAPS titles into Title Case (keeping
// acronyms the book uses, like MCAT), and nests entries under the parts
// and chapters they belong to.
// Entries whose title names one of `parts` become that part, at the top
// level, with the chapters after it inside. With `pages`, a section whose
// title is not printed on its page (a model describing the page rather than
// copying a heading) is dropped; so are sentences, question numbers, and
// captions. Chapters out of numerical order become sections of the chapter
// they fall in.
QVariantList tidyEntries(const QVariantList &entries, const QStringList &runningHeads, const QString &bookTitle,
                         const QStringList &parts = {}, const QStringList &pages = {});

// Chapters the pages announce themselves: a page whose whole text is
// "Chapter 15 The Excretory and Digestive Systems". Added to what a model
// found (which sometimes skips one) by tidyEntries' callers.
QVariantList titlePages(const QStringList &pages);

// The share of measured headings that read as gibberish (0–1). Scans whose
// text was machine-read have many; their headings are not used.
double headingNoise(const QStringList &headings);

// Headings that repeat on many pages ("Drill", a running chapter title),
// normalized, which are page furniture rather than sections.
QSet<QString> repeatedHeadings(const QStringList &headings);

// Section and subsection entries straight from the detected headings of
// pages first…last, in the order they are printed, skipping `repeated`,
// question and answer lines, and chapter labels (a model or title page
// names chapters). Each has an "order" within its page.
//
// With a `vocabulary` (see below), as for scans whose text is noisy, a
// heading must also be clearly larger than the text, free of OCR debris,
// and made of words the book uses elsewhere.
QVariantList headingEntries(const QStringList &headings, int first, int last, const QSet<QString> &repeated,
                            const QHash<QString, int> &vocabulary = {});

// How often each word (lowercase, three letters or more) appears in the
// book, to tell real words from OCR misreads.
QHash<QString, int> vocabulary(const QStringList &pages);

// The outline a book prints, for books whose pages are numbered by
// chapter ("1-5", "12-3"), as many reference manuals are: chapters where
// their page labels begin, titled by their running heads (voted across the
// chapter, which outvotes OCR slips) or their opening page; numbered
// sections from the headings printed in the text ("3. CONIC SECTIONS"),
// named after the chapter's own contents list where it has one; and named
// equation blocks ("Equation 1.83: Circular Sectors") as subsections. Empty
// when the book is not numbered that way. Entries are labelled
// "Chapter 1: …", "1.3 …", ready for the outline.
QVariantList printedOutline(const QStringList &pages);

// Whether two headings are the same one, one perhaps missing its label
// ("Biochemistry" and "Chapter 4 Biochemistry").
bool sameHeading(const QString &a, const QString &b);

// The parts a printed table of contents names ("PART 3: MCAT BIOLOGY"), as
// "Part 3: MCAT Biology".
QStringList contentsParts(const QString &contentsText, const QString &bookTitle);

} // namespace BookText
