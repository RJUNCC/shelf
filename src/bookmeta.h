#pragma once

#include <QImage>
#include <QString>

// What the library shows for a book before it is opened.
struct BookMeta {
    QString format;  // "epub" or "pdf"; empty when the file is neither
    QString title;   // empty when the book does not name itself
    QString author;
    QImage cover;    // null when the book has no usable cover
    QString error;   // why the file could not be read, for the window
};

namespace BookMetaReader {

// The format from the file's contents, not its name, so a mislabelled file
// still opens in the right reader.
QString detectFormat(const QString &path);

BookMeta read(const QString &path);
BookMeta readEpub(const QString &path);
BookMeta readPdf(const QString &path);

} // namespace BookMetaReader
