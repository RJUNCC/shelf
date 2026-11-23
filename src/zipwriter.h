#pragma once

#include <QByteArray>
#include <QString>

// Writes a ZIP archive in memory, for the EPUBs Shelf makes (the text
// version of a scanned book). Entries are deflated unless told otherwise;
// an EPUB's "mimetype" must come first and stay uncompressed.
class ZipWriter {
public:
    void add(const QString &name, const QByteArray &data, bool compress = true);
    QByteArray finish();

private:
    QByteArray m_out;
    QByteArray m_directory;
    int m_count = 0;
};
