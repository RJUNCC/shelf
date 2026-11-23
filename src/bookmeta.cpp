#include "bookmeta.h"

#include "zipreader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPdfDocument>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

QString tr(const char *text) { return QCoreApplication::translate("BookMeta", text); }

// Hrefs in the OPF are relative to the OPF itself and may be percent-encoded.
QString resolveHref(const QString &opfPath, const QString &href) {
    const QString base = opfPath.contains('/') ? opfPath.section('/', 0, -2) + '/' : QString();
    const QString decoded = QUrl::fromPercentEncoding(href.section('#', 0, 0).toUtf8());
    return QDir::cleanPath(base + decoded);
}

QString collapseSpace(const QString &text) { return text.simplified(); }

} // namespace

QString BookMetaReader::detectFormat(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray head = file.read(1024);
    if (head.startsWith("%PDF-"))
        return "pdf";
    // An EPUB is a ZIP whose container lists a package document; checking the
    // container rather than the "mimetype" entry also accepts sloppy writers.
    if (head.startsWith("PK\x03\x04")) {
        file.close();
        ZipReader zip(path);
        if (zip.isValid() && zip.contains("META-INF/container.xml"))
            return "epub";
    }
    return {};
}

BookMeta BookMetaReader::read(const QString &path) {
    if (!QFile::exists(path)) {
        BookMeta meta;
        meta.error = tr("The file no longer exists.");
        return meta;
    }
    const QString format = detectFormat(path);
    if (format == "epub")
        return readEpub(path);
    if (format == "pdf")
        return readPdf(path);
    BookMeta meta;
    meta.error = tr("Not an EPUB or PDF file.");
    return meta;
}

BookMeta BookMetaReader::readEpub(const QString &path) {
    BookMeta meta;
    meta.format = "epub";
    ZipReader zip(path);
    if (!zip.isValid()) {
        meta.error = tr("The EPUB file is damaged.");
        return meta;
    }

    QString opfPath;
    {
        QXmlStreamReader xml(zip.read("META-INF/container.xml"));
        while (!xml.atEnd() && opfPath.isEmpty())
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"rootfile")
                opfPath = xml.attributes().value("full-path").toString();
    }
    const QByteArray opf = opfPath.isEmpty() ? QByteArray() : zip.read(opfPath);
    if (opf.isEmpty()) {
        meta.error = tr("The EPUB file has no package document.");
        return meta;
    }

    struct Item { QString href, mediaType, properties; };
    QHash<QString, Item> manifest;
    QString coverId;
    QXmlStreamReader xml(opf);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto name = xml.name();
        const auto attrs = xml.attributes();
        // Dublin Core elements are matched by local name, so any prefix works.
        if (name == u"title" && meta.title.isEmpty())
            meta.title = collapseSpace(xml.readElementText(QXmlStreamReader::IncludeChildElements));
        else if (name == u"creator" && meta.author.isEmpty())
            meta.author = collapseSpace(xml.readElementText(QXmlStreamReader::IncludeChildElements));
        else if (name == u"meta" && attrs.value("name") == u"cover")
            coverId = attrs.value("content").toString();
        else if (name == u"item")
            manifest.insert(attrs.value("id").toString(),
                            {attrs.value("href").toString(), attrs.value("media-type").toString(),
                             attrs.value("properties").toString()});
    }

    // EPUB 3 marks the cover in the manifest, EPUB 2 names it in a <meta>,
    // and older books only hint at it in the item's id or file name.
    QString coverHref;
    for (auto it = manifest.cbegin(); it != manifest.cend() && coverHref.isEmpty(); ++it)
        if (it->properties.split(' ').contains("cover-image"))
            coverHref = it->href;
    if (coverHref.isEmpty() && manifest.contains(coverId) && manifest[coverId].mediaType.startsWith("image/"))
        coverHref = manifest[coverId].href;
    for (auto it = manifest.cbegin(); it != manifest.cend() && coverHref.isEmpty(); ++it)
        if (it->mediaType.startsWith("image/")
            && (it.key().contains("cover", Qt::CaseInsensitive) || it->href.contains("cover", Qt::CaseInsensitive)))
            coverHref = it->href;

    if (!coverHref.isEmpty())
        meta.cover = QImage::fromData(zip.read(resolveHref(opfPath, coverHref)));
    return meta;
}

BookMeta BookMetaReader::readPdf(const QString &path) {
    BookMeta meta;
    meta.format = "pdf";
    QPdfDocument document;
    if (document.load(path) != QPdfDocument::Error::None) {
        meta.error = document.status() == QPdfDocument::Status::Error
                         && document.error() == QPdfDocument::Error::IncorrectPassword
                         ? tr("The PDF is password protected.")
                         : tr("The PDF file is damaged.");
        return meta;
    }
    meta.title = collapseSpace(document.metaData(QPdfDocument::MetaDataField::Title).toString());
    meta.author = collapseSpace(document.metaData(QPdfDocument::MetaDataField::Author).toString());
    if (document.pageCount() > 0) {
        // The first page is the cover; render it at shelf size, not print size.
        const QSizeF points = document.pagePointSize(0);
        if (points.height() > 0) {
            const double scale = 480.0 / points.height();
            // Pages render onto transparency; put them on paper so the cover
            // does not turn black when saved without an alpha channel.
            const QImage page = document.render(0, (points * scale).toSize());
            QImage cover(page.size(), QImage::Format_RGB32);
            cover.fill(Qt::white);
            QPainter painter(&cover);
            painter.drawImage(0, 0, page);
            painter.end();
            meta.cover = cover;
        }
    }
    return meta;
}
