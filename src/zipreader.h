#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>

// Reads single entries out of a ZIP archive, enough to pull the metadata and
// cover out of an EPUB without unpacking it. Supports stored and deflated
// entries, which is all the EPUB specification allows.
class ZipReader {
public:
    explicit ZipReader(const QString &path);

    bool isValid() const { return m_valid; }
    QStringList names() const { return m_entries.keys(); }
    bool contains(const QString &name) const { return m_entries.contains(name); }

    // An empty array means the entry is missing, unsupported, or corrupt.
    QByteArray read(const QString &name, qint64 maxSize = 64 * 1024 * 1024);

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    QFile m_file;
    QHash<QString, Entry> m_entries;
    bool m_valid = false;
};
