#include "zipwriter.h"

#include <QtEndian>
#include <zlib.h>

namespace {
void le16(QByteArray &b, quint16 v) {
    char c[2];
    qToLittleEndian(v, c);
    b.append(c, 2);
}
void le32(QByteArray &b, quint32 v) {
    char c[4];
    qToLittleEndian(v, c);
    b.append(c, 4);
}
} // namespace

void ZipWriter::add(const QString &name, const QByteArray &data, bool compress) {
    QByteArray body = data;
    if (compress && !data.isEmpty()) {
        body.resize(qsizetype(compressBound(uLong(data.size()))) + 64);
        z_stream s{};
        deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
        s.avail_in = uInt(data.size());
        s.next_out = reinterpret_cast<Bytef *>(body.data());
        s.avail_out = uInt(body.size());
        ::deflate(&s, Z_FINISH);
        body.resize(qsizetype(s.total_out));
        deflateEnd(&s);
        // Already-compressed data (JPEGs) can come out bigger; store it.
        if (body.size() >= data.size()) {
            body = data;
            compress = false;
        }
    } else {
        compress = false;
    }
    const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
    const quint32 offset = quint32(m_out.size());
    const QByteArray n = name.toUtf8();
    const quint16 method = compress ? 8 : 0;
    le32(m_out, 0x04034b50); le16(m_out, 20); le16(m_out, 0x0800); le16(m_out, method);
    le16(m_out, 0); le16(m_out, 0x21); le32(m_out, crc); le32(m_out, quint32(body.size())); le32(m_out, quint32(data.size()));
    le16(m_out, quint16(n.size())); le16(m_out, 0);
    m_out.append(n);
    m_out.append(body);
    le32(m_directory, 0x02014b50); le16(m_directory, 20); le16(m_directory, 20); le16(m_directory, 0x0800);
    le16(m_directory, method); le16(m_directory, 0); le16(m_directory, 0x21); le32(m_directory, crc);
    le32(m_directory, quint32(body.size())); le32(m_directory, quint32(data.size())); le16(m_directory, quint16(n.size()));
    le16(m_directory, 0); le16(m_directory, 0); le16(m_directory, 0); le16(m_directory, 0); le32(m_directory, 0);
    le32(m_directory, offset);
    m_directory.append(n);
    ++m_count;
}

QByteArray ZipWriter::finish() {
    QByteArray out = m_out;
    const quint32 directoryOffset = quint32(out.size());
    out.append(m_directory);
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, quint16(m_count));
    le16(out, quint16(m_count)); le32(out, quint32(m_directory.size())); le32(out, directoryOffset);
    le16(out, 0);
    return out;
}
