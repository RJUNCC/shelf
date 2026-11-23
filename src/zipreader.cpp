#include "zipreader.h"

#include <QtEndian>
#include <zlib.h>

namespace {

quint16 u16(const char *p) { return qFromLittleEndian<quint16>(p); }
quint32 u32(const char *p) { return qFromLittleEndian<quint32>(p); }

} // namespace

ZipReader::ZipReader(const QString &path) : m_file(path) {
    if (!m_file.open(QIODevice::ReadOnly))
        return;

    // The end-of-central-directory record sits in the last 22 bytes plus an
    // optional comment of up to 64 KiB, so search backwards through that tail.
    const qint64 size = m_file.size();
    const qint64 tailSize = qMin<qint64>(size, 22 + 65535);
    if (tailSize < 22 || !m_file.seek(size - tailSize))
        return;
    const QByteArray tail = m_file.read(tailSize);
    qsizetype eocd = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail.constData() + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return;

    const char *e = tail.constData() + eocd;
    const quint16 count = u16(e + 10);
    const quint32 directorySize = u32(e + 12);
    const quint32 directoryOffset = u32(e + 16);
    if (qint64(directoryOffset) + directorySize > size || !m_file.seek(directoryOffset))
        return;
    const QByteArray directory = m_file.read(directorySize);
    if (directory.size() != qsizetype(directorySize))
        return;

    qsizetype pos = 0;
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > directory.size() || u32(directory.constData() + pos) != 0x02014b50)
            return;
        const char *h = directory.constData() + pos;
        Entry entry;
        entry.method = u16(h + 10);
        entry.compressedSize = u32(h + 20);
        entry.uncompressedSize = u32(h + 24);
        const quint16 nameLength = u16(h + 28), extraLength = u16(h + 30), commentLength = u16(h + 32);
        entry.localHeaderOffset = u32(h + 42);
        if (pos + 46 + nameLength > directory.size())
            return;
        m_entries.insert(QString::fromUtf8(h + 46, nameLength), entry);
        pos += 46 + nameLength + extraLength + commentLength;
    }
    m_valid = true;
}

QByteArray ZipReader::read(const QString &name, qint64 maxSize) {
    const auto it = m_entries.constFind(name);
    if (!m_valid || it == m_entries.constEnd())
        return {};
    const Entry &entry = *it;
    if (entry.uncompressedSize > maxSize || entry.compressedSize > maxSize)
        return {};

    // The local header repeats the name and has its own extra field, whose
    // length can differ from the central directory's, so read it here.
    if (!m_file.seek(entry.localHeaderOffset))
        return {};
    const QByteArray header = m_file.read(30);
    if (header.size() != 30 || u32(header.constData()) != 0x04034b50)
        return {};
    const qint64 dataOffset = qint64(entry.localHeaderOffset) + 30 + u16(header.constData() + 26)
                              + u16(header.constData() + 28);
    if (!m_file.seek(dataOffset))
        return {};
    const QByteArray compressed = m_file.read(entry.compressedSize);
    if (compressed.size() != qsizetype(entry.compressedSize))
        return {};

    if (entry.method == 0)
        return compressed;
    if (entry.method != 8)
        return {};

    // Raw deflate: negative window bits tell zlib there is no zlib header.
    QByteArray out(qsizetype(entry.uncompressedSize), Qt::Uninitialized);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = uInt(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = inflate(&stream, Z_FINISH);
    const bool complete = result == Z_STREAM_END && stream.total_out == entry.uncompressedSize;
    inflateEnd(&stream);
    return complete ? out : QByteArray();
}
