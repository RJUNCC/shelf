#include "slides.h"

#include "zipreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace Slides {

QString Slide::text() const {
    QStringList lines;
    if (!title.isEmpty())
        lines << title;
    for (const Paragraph &p : paragraphs)
        lines << QString(p.level * 2, ' ') + p.text;
    return lines.join('\n');
}

QVariantMap Slide::toMap() const {
    QVariantList paras;
    for (const Paragraph &p : paragraphs)
        paras << QVariantMap{{"text", p.text}, {"level", p.level}};
    return {{"title", title}, {"paragraphs", paras}, {"images", images}, {"notes", notes}};
}

Slide Slide::fromMap(const QVariantMap &map) {
    Slide s;
    s.title = map.value("title").toString();
    for (const QVariant &v : map.value("paragraphs").toList())
        s.paragraphs << Paragraph{v.toMap().value("text").toString(), v.toMap().value("level").toInt()};
    s.images = map.value("images").toStringList();
    s.notes = map.value("notes").toString();
    return s;
}

namespace {

// A part's relationships (rId → target), with targets resolved against the
// part's folder: "../media/image1.png" from ppt/slides/ is ppt/media/image1.png.
QHash<QString, QPair<QString, QString>> relationships(ZipReader &zip, const QString &part) {
    QHash<QString, QPair<QString, QString>> out;   // id → (type, target)
    const QString folder = part.section('/', 0, -2);
    const QString rels = folder + "/_rels/" + part.section('/', -1) + ".rels";
    QXmlStreamReader xml(zip.read(rels));
    while (!xml.atEnd())
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"Relationship") {
            const auto a = xml.attributes();
            QString target = a.value("Target").toString();
            if (a.value("TargetMode") != u"External")
                target = target.startsWith('/') ? target.mid(1) : QDir::cleanPath(folder + "/" + target);
            out.insert(a.value("Id").toString(), {a.value("Type").toString(), target});
        }
    return out;
}

// The text of a slide or notes part: each shape's paragraphs, with the
// shape's placeholder type ("title", "ctrTitle", "body", "sldNum", …).
struct Shape {
    QString placeholder;
    QList<Paragraph> paragraphs;
};
struct Part {
    QList<Shape> shapes;
    QStringList pictures;   // relationship ids, in order
};

Part readPart(const QByteArray &data) {
    Part part;
    QXmlStreamReader xml(data);
    Shape shape;
    bool inShape = false;
    Paragraph para;
    bool inPara = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        const auto name = xml.name();
        if (token == QXmlStreamReader::StartElement) {
            if (name == u"sp" || name == u"graphicFrame") {
                shape = Shape{};
                inShape = true;
            } else if (name == u"ph" && inShape) {
                shape.placeholder = xml.attributes().value("type").toString();
                if (shape.placeholder.isEmpty())
                    shape.placeholder = "body";
            } else if (name == u"p" && xml.namespaceUri().endsWith(u"drawingml/2006/main")) {
                para = Paragraph{};
                inPara = true;
            } else if (name == u"pPr" && inPara) {
                para.level = xml.attributes().value("lvl").toInt();
            } else if (name == u"t" && inPara) {
                para.text += xml.readElementText();
            } else if (name == u"br" && inPara) {
                para.text += ' ';
            } else if (name == u"blip") {
                const QString id = xml.attributes().value(
                    "http://schemas.openxmlformats.org/officeDocument/2006/relationships", "embed").toString();
                if (!id.isEmpty())
                    part.pictures << id;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (name == u"p" && inPara) {
                inPara = false;
                para.text = para.text.simplified();
                if (!para.text.isEmpty())
                    shape.paragraphs << para;
            } else if ((name == u"sp" || name == u"graphicFrame") && inShape) {
                inShape = false;
                if (!shape.paragraphs.isEmpty())
                    part.shapes << shape;
            }
        }
    }
    return part;
}

QStringList slideParts(ZipReader &zip) {
    // presentation.xml lists the slides in order by relationship id.
    QStringList out;
    const auto rels = relationships(zip, "ppt/presentation.xml");
    QXmlStreamReader xml(zip.read("ppt/presentation.xml"));
    while (!xml.atEnd())
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"sldId") {
            const QString id = xml.attributes().value(
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships", "id").toString();
            if (rels.contains(id))
                out << rels.value(id).second;
        }
    return out;
}

QString notesOf(ZipReader &zip, const QHash<QString, QPair<QString, QString>> &rels) {
    for (const auto &r : rels)
        if (r.first.endsWith("/notesSlide")) {
            QStringList lines;
            // The notes placeholder, or a plain text box (LibreOffice
            // writes notes that way); never the slide number or header.
            for (const Shape &s : readPart(zip.read(r.second)).shapes)
                if (s.placeholder == "body" || s.placeholder.isEmpty())
                    for (const Paragraph &p : s.paragraphs)
                        lines << p.text;
            return lines.join('\n');
        }
    return {};
}

} // namespace

QList<Slide> readPptx(const QString &path, const QString &mediaDirectory) {
    QList<Slide> out;
    ZipReader zip(path);
    if (!zip.isValid())
        return out;
    if (!mediaDirectory.isEmpty())
        QDir().mkpath(mediaDirectory);
    int index = 0;
    for (const QString &partName : slideParts(zip)) {
        ++index;
        const auto rels = relationships(zip, partName);
        const Part part = readPart(zip.read(partName));
        Slide slide;
        for (const Shape &shape : part.shapes) {
            if (shape.placeholder == "sldNum" || shape.placeholder == "dt" || shape.placeholder == "ftr")
                continue;
            if ((shape.placeholder == "title" || shape.placeholder == "ctrTitle") && slide.title.isEmpty()) {
                QStringList words;
                for (const Paragraph &p : shape.paragraphs)
                    words << p.text;
                slide.title = words.join(' ');
                continue;
            }
            slide.paragraphs << shape.paragraphs;
        }
        // A slide without a title placeholder takes its first line.
        if (slide.title.isEmpty() && !slide.paragraphs.isEmpty() && slide.paragraphs.first().text.size() < 90)
            slide.title = slide.paragraphs.takeFirst().text;
        if (!mediaDirectory.isEmpty()) {
            int n = 0;
            for (const QString &id : part.pictures) {
                const QString target = rels.value(id).second;
                const QString suffix = QFileInfo(target).suffix().toLower();
                // Only pictures a browser and Qt can draw.
                if (!QStringList{"png", "jpg", "jpeg", "gif", "bmp", "svg", "webp"}.contains(suffix))
                    continue;
                const QByteArray data = zip.read(target);
                if (data.isEmpty())
                    continue;
                const QString file = QStringLiteral("%1/slide%2-%3.%4").arg(mediaDirectory).arg(index).arg(++n).arg(suffix);
                QFile f(file);
                if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size())
                    slide.images << file;
            }
        }
        slide.notes = notesOf(zip, rels);
        out << slide;
    }
    return out;
}

QStringList pptxNotes(const QString &path) {
    QStringList out;
    ZipReader zip(path);
    if (!zip.isValid())
        return out;
    for (const QString &partName : slideParts(zip))
        out << notesOf(zip, relationships(zip, partName));
    return out;
}

QList<Slide> readPdf(const QString &path) {
    QList<Slide> out;
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return out;
    for (int i = 0; i < pdf.pageCount(); ++i) {
        Slide slide;
        const QStringList lines = pdf.getAllText(i).text().split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        for (const QString &raw : lines) {
            const QString line = raw.simplified();
            if (line.isEmpty())
                continue;
            if (slide.title.isEmpty())
                slide.title = line;
            else
                slide.paragraphs << Paragraph{line, 0};
        }
        out << slide;
    }
    return out;
}

QString findLibreOffice() {
    for (const char *name : {"soffice", "libreoffice"}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
#ifdef Q_OS_WIN
    for (const QString &dir : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")}) {
        const QString exe = dir + "/LibreOffice/program/soffice.exe";
        if (!dir.isEmpty() && QFileInfo::exists(exe))
            return exe;
    }
#endif
    return {};
}

} // namespace Slides
