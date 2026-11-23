#pragma once

// Builders for test books: a tiny ZIP writer for EPUBs and QPdfWriter for PDFs.
// Kept out of shelf_tests.cpp because moc cannot parse the raw strings.

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QtEndian>
#include <QtTest>
#include <zlib.h>

#include "filepicker.h"

namespace {

// A minimal ZIP writer for building test EPUBs: stored or raw-deflated entries.
QByteArray makeZip(const QList<QPair<QString, QByteArray>> &files, bool useDeflate) {
    QByteArray out, directory;
    auto le16 = [](QByteArray &b, quint16 v) { char c[2]; qToLittleEndian(v, c); b.append(c, 2); };
    auto le32 = [](QByteArray &b, quint32 v) { char c[4]; qToLittleEndian(v, c); b.append(c, 4); };
    for (const auto &[name, data] : files) {
        // The EPUB mimetype entry must be stored, so only deflate the rest.
        const bool compress = useDeflate && name != "mimetype";
        QByteArray body = data;
        if (compress) {
            body.resize(qsizetype(compressBound(uLong(data.size()))) + 64);
            z_stream s{};
            deflateInit2(&s, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
            s.avail_in = uInt(data.size());
            s.next_out = reinterpret_cast<Bytef *>(body.data());
            s.avail_out = uInt(body.size());
            ::deflate(&s, Z_FINISH);
            body.resize(qsizetype(s.total_out));
            deflateEnd(&s);
        }
        const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
        const quint32 offset = quint32(out.size());
        const QByteArray n = name.toUtf8();
        le32(out, 0x04034b50); le16(out, 20); le16(out, 0); le16(out, compress ? 8 : 0);
        le16(out, 0); le16(out, 0); le32(out, crc); le32(out, quint32(body.size())); le32(out, quint32(data.size()));
        le16(out, quint16(n.size())); le16(out, 3); out.append(n); out.append("\0\0\0", 3);  // odd extra field
        out.append(body);
        le32(directory, 0x02014b50); le16(directory, 20); le16(directory, 20); le16(directory, 0);
        le16(directory, compress ? 8 : 0); le16(directory, 0); le16(directory, 0); le32(directory, crc);
        le32(directory, quint32(body.size())); le32(directory, quint32(data.size())); le16(directory, quint16(n.size()));
        le16(directory, 0); le16(directory, 0); le16(directory, 0); le16(directory, 0); le32(directory, 0);
        le32(directory, offset); directory.append(n);
    }
    const quint32 directoryOffset = quint32(out.size());
    out.append(directory);
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, quint16(files.size()));
    le16(out, quint16(files.size())); le32(out, quint32(directory.size())); le32(out, directoryOffset);
    le16(out, 7); out.append("comment");
    return out;
}

QByteArray pngBytes(QColor color) {
    QImage image(60, 90, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString writeEpub(const QString &path, const QString &title, const QString &author, bool epub3Cover = true,
                  bool deflate = true) {
    const QByteArray container = R"(<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>)";
    const QByteArray opf = QString(R"(<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>  %1 </dc:title>
    <dc:creator>%2</dc:creator>
    %3
  </metadata>
  <manifest>
    <item id="img" href="images/front%20cover.png" media-type="image/png" %4/>
    <item id="c1" href="chapter.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="c1"/></spine>
</package>)")
                               .arg(title, author, epub3Cover ? "" : R"(<meta name="cover" content="img"/>)",
                                    epub3Cover ? R"(properties="cover-image")" : "")
                               .toUtf8();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(makeZip({{"mimetype", "application/epub+zip"},
                        {"META-INF/container.xml", container},
                        {"OEBPS/content.opf", opf},
                        {"OEBPS/chapter.xhtml", "<html><body><p>Hello</p></body></html>"},
                        {"OEBPS/images/front cover.png", pngBytes(Qt::red)}},
                       deflate));
    return path;
}

QString writePdf(const QString &path, const QString &title, const QString &author) {
    QPdfWriter writer(path);
    writer.setTitle(title);
    writer.setCreator(author);
    // QPdfWriter has no author setter; Qt writes the creator as the PDF's
    // Creator, so the test reads the title and checks the cover only.
    QPainter painter(&writer);
    painter.fillRect(QRect(0, 0, 400, 400), Qt::blue);
    painter.drawText(100, 100, title);
    painter.end();
    return path;
}

// A PDF with one paragraph of text on each page.
QString writeBookPdf(const QString &path, const QStringList &pages) {
    QPdfWriter writer(path);
    QPainter painter(&writer);
    for (int i = 0; i < pages.size(); ++i) {
        if (i > 0)
            writer.newPage();
        painter.drawText(QRect(200, 200, 8000, 8000), Qt::TextWordWrap, pages[i]);
    }
    painter.end();
    return path;
}

class FakePicker : public FilePicker {
public:
    QStringList answer;
    void pickBooks(const QString &) override { emit picked(answer); }
};

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

} // namespace
