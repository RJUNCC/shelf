#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Reading lecture slides: the text of each slide (for linking it to books
// and explaining it), the speaker notes, and, for PowerPoint files when
// LibreOffice isn't there to turn them into PDFs, enough to draw each slide
// as a clean card: its title, its paragraphs with their indent, and its
// pictures.
namespace Slides {

struct Paragraph {
    QString text;
    int level = 0;   // indent: 0 for top-level bullets
};

struct Slide {
    QString title;
    QList<Paragraph> paragraphs;
    QStringList images;   // files written to the media folder, in order
    QString notes;

    // Everything on the slide as plain text, title first.
    QString text() const;
    QVariantMap toMap() const;
    static Slide fromMap(const QVariantMap &map);
};

// The slides of a .pptx, in presentation order. Pictures are written into
// `mediaDirectory` (when not empty) and listed by file path.
QList<Slide> readPptx(const QString &path, const QString &mediaDirectory = {});

// The text of every page of a PDF deck, and its title as the first line.
QList<Slide> readPdf(const QString &path);

// Speaker notes of a .pptx alone, slide by slide (for decks shown from the
// PDF LibreOffice made, whose notes the PDF doesn't carry).
QStringList pptxNotes(const QString &path);

// LibreOffice's soffice, or empty when it isn't installed.
QString findLibreOffice();

} // namespace Slides
