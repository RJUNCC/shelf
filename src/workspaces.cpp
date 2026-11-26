#include "workspaces.h"

#include "filepicker.h"
#include "library.h"
#include "slides.h"
#include "zipreader.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8); }

// LibreOffice turns a .pptx into a PDF beside it, quietly and at idle
// priority, with a profile of its own so a LibreOffice the user has open
// doesn't get in the way. Returns the PDF, or empty.
QString convertToPdf(const QString &soffice, const QString &file, const QString &profile) {
    QProcess process;
#if defined(Q_OS_UNIX)
    process.setChildProcessModifier([] { (void)::nice(19); });
#elif defined(Q_OS_WIN)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= IDLE_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
    const QString dir = QFileInfo(file).absolutePath();
    process.start(soffice, {"-env:UserInstallation=" + QUrl::fromLocalFile(profile).toString(), "--headless",
                            "--norestore", "--convert-to", "pdf", "--outdir", dir, file});
    if (!process.waitForFinished(5 * 60 * 1000)) {
        process.kill();
        process.waitForFinished(2000);
        return {};
    }
    const QString pdf = dir + "/" + QFileInfo(file).completeBaseName() + ".pdf";
    return process.exitCode() == 0 && QFileInfo::exists(pdf) ? pdf : QString();
}
} // namespace

Workspaces::Workspaces(Library *library, FilePicker *picker, const QString &directory, QObject *parent)
    : QObject(parent), m_library(library), m_picker(picker),
      m_directory(directory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf/workspaces"
                      : directory) {
    QDir().mkpath(m_directory);
    if (m_picker) {
        m_picker->kind = "slides";
        connect(m_picker, &FilePicker::picked, this, [this](const QStringList &paths) {
            if (m_pickingFor.startsWith("review:")) {
                // A study review rather than slides.
                const QString id = m_pickingFor.mid(7);
                m_picker->kind = "slides";
                m_pickingFor.clear();
                if (paths.isEmpty())
                    return;
                const QString text = documentText(paths.first());
                if (text.trimmed().isEmpty())
                    emit message(tr("No text could be read from %1.").arg(QFileInfo(paths.first()).fileName()));
                else
                    emit reviewLoaded(id, QFileInfo(paths.first()).fileName(), text);
                return;
            }
            if (!m_pickingFor.isEmpty())
                addDecks(m_pickingFor, paths);
        });
        connect(m_picker, &FilePicker::failed, this, &Workspaces::message);
    }
}

bool Workspaces::hasLibreOffice() const { return !Slides::findLibreOffice().isEmpty(); }

QString Workspaces::folder(const QString &id) const { return m_directory + "/" + id; }

QVariantMap Workspaces::load(const QString &id) const {
    if (id.isEmpty() || id.contains('/') || id.contains(".."))
        return {};
    QFile file(folder(id) + "/workspace.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QVariantMap w = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    w["id"] = id;
    return w;
}

bool Workspaces::save(const QString &id, QVariantMap workspace) {
    workspace.remove("id");
    workspace["updated"] = QDateTime::currentMSecsSinceEpoch();
    QDir().mkpath(folder(id));
    QSaveFile file(folder(id) + "/workspace.json");
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject::fromVariantMap(workspace)).toJson()) < 0 || !file.commit()) {
        emit message(tr("Could not save the workspace: %1").arg(file.errorString()));
        return false;
    }
    emit changed(id);
    emit listChanged();
    return true;
}

QVariantList Workspaces::list() const {
    QList<QVariantMap> all;
    for (const QString &id : QDir(m_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QVariantMap w = load(id);
        if (w.isEmpty())
            continue;
        int slides = 0;
        const QVariantList decks = w.value("decks").toList();
        for (const QVariant &d : decks)
            slides += d.toMap().value("slides").toList().size();
        all << QVariantMap{{"id", id}, {"name", w.value("name")}, {"decks", decks.size()}, {"slides", slides},
                           {"updated", w.value("updated")}};
    }
    std::sort(all.begin(), all.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("updated").toLongLong() > b.value("updated").toLongLong();
    });
    QVariantList out;
    for (const QVariantMap &w : all)
        out << w;
    return out;
}

QString Workspaces::create(const QString &name) {
    const QString id = newId();
    QVariantMap w{{"name", name.trimmed().isEmpty() ? tr("New workspace") : name.trimmed()},
                  {"decks", QVariantList{}}, {"books", QVariantList{}}};
    return save(id, w) ? id : QString();
}

void Workspaces::rename(const QString &id, const QString &name) {
    QVariantMap w = load(id);
    if (w.isEmpty() || name.trimmed().isEmpty())
        return;
    w["name"] = name.trimmed();
    save(id, w);
}

void Workspaces::remove(const QString &id) {
    if (load(id).isEmpty())
        return;
    // Only Shelf's copies are in here; the user's own slides stay put.
    QDir(folder(id)).removeRecursively();
    emit listChanged();
}

QVariantMap Workspaces::workspace(const QString &id) const {
    QVariantMap w = load(id);
    // Paths in the file are relative to the workspace's folder.
    QVariantList decks;
    for (const QVariant &v : w.value("decks").toList()) {
        QVariantMap d = v.toMap();
        if (!d.value("pdf").toString().isEmpty()) {
            // The local path too, for selecting and finding text on it.
            d["pdfPath"] = folder(id) + "/" + d.value("pdf").toString();
            d["pdf"] = QUrl::fromLocalFile(d.value("pdfPath").toString()).toString();
        }
        QVariantList slides;
        for (const QVariant &s : d.value("slides").toList()) {
            QVariantMap slide = s.toMap();
            QStringList images;
            for (const QString &image : slide.value("images").toStringList())
                images << QUrl::fromLocalFile(folder(id) + "/" + image).toString();
            slide["images"] = images;
            slides << slide;
        }
        d["slides"] = slides;
        decks << d;
    }
    if (!w.isEmpty())
        w["decks"] = decks;
    return w;
}

void Workspaces::pickReviewDocument(const QString &id) {
    if (!m_picker)
        return;
    m_pickingFor = "review:" + id;
    m_picker->kind = "documents";
    m_picker->pickBooks(tr("The study review"));
}

QString Workspaces::documentText(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "pdf") {
        QStringList pages;
        for (const Slides::Slide &s : Slides::readPdf(path))
            pages << s.text();
        return pages.join("\n\n");
    }
    // Word documents as Markdown: headings from their styles, list items
    // as bullets (indented by level), bold and italic runs kept.
    if (suffix == "docx") {
        ZipReader zip(path);
        if (!zip.isValid())
            return {};
        QStringList out;
        QXmlStreamReader xml(zip.read("word/document.xml"));
        QString line, style;
        int listLevel = -1;
        bool bold = false, italic = false;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            const QStringView name = xml.qualifiedName();
            if (token == QXmlStreamReader::StartElement) {
                if (name == u"w:p") {
                    line.clear();
                    style.clear();
                    listLevel = -1;
                } else if (name == u"w:pStyle") {
                    style = xml.attributes().value("w:val").toString();
                } else if (name == u"w:ilvl") {
                    listLevel = xml.attributes().value("w:val").toInt();
                } else if (name == u"w:numPr" && listLevel < 0) {
                    listLevel = 0;
                } else if (name == u"w:r") {
                    bold = italic = false;
                } else if (name == u"w:b") {
                    bold = xml.attributes().value("w:val") != u"0";
                } else if (name == u"w:i") {
                    italic = xml.attributes().value("w:val") != u"0";
                } else if (name == u"w:tab") {
                    line += ' ';
                } else if (name == u"w:t") {
                    const QString text = xml.readElementText();
                    const QString core = text.trimmed();
                    if (core.isEmpty() || (!bold && !italic)) {
                        line += text;
                    } else {
                        const QString mark = bold ? QStringLiteral("**") : QStringLiteral("*");
                        const QString lead = text.left(text.indexOf(core)), trail = text.mid(text.indexOf(core) + core.size());
                        line += lead + mark + core + mark + trail;
                    }
                }
            } else if (token == QXmlStreamReader::EndElement && name == u"w:p") {
                const QString text = line.trimmed();
                if (text.isEmpty())
                    continue;
                static const QRegularExpression heading(QStringLiteral("^(?:Heading|heading )(\\d)$"));
                const auto m = heading.match(style);
                if (style == "Title")
                    out << "# " + text;
                else if (m.hasMatch())
                    out << QString(qBound(1, m.captured(1).toInt() + (out.isEmpty() ? 0 : 0), 6), '#') + " " + text;
                else if (listLevel >= 0)
                    out << QString(listLevel * 2, ' ') + "- " + text;
                else
                    out << text;
            }
        }
        return out.join("\n\n").replace(QRegularExpression(QStringLiteral("\n\n(\\s*- )")), QStringLiteral("\n\\1"));
    }
    // LibreOffice documents: headings by outline level, list items as bullets.
    if (suffix == "odt") {
        ZipReader zip(path);
        if (!zip.isValid())
            return {};
        QStringList out;
        int listDepth = 0;
        QXmlStreamReader reader(zip.read("content.xml"));
        while (!reader.atEnd()) {
            const auto token = reader.readNext();
            const QStringView name = reader.qualifiedName();
            if (token == QXmlStreamReader::StartElement && name == u"text:list") {
                ++listDepth;
            } else if (token == QXmlStreamReader::EndElement && name == u"text:list") {
                --listDepth;
            } else if (token == QXmlStreamReader::StartElement && (name == u"text:p" || name == u"text:h")) {
                const bool isHeading = name == u"text:h";
                const int level = qBound(1, reader.attributes().value("text:outline-level").toInt(), 6);
                const QString line = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
                if (line.isEmpty())
                    continue;
                if (isHeading)
                    out << QString(level, '#') + " " + line;
                else if (listDepth > 0)
                    out << QString((listDepth - 1) * 2, ' ') + "- " + line;
                else
                    out << line;
            }
        }
        return out.join('\n');
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString text = QString::fromUtf8(file.read(4 * 1024 * 1024));
    if (suffix == "html" || suffix == "htm") {
        // A saved web page (a Canvas page, say) as Markdown: headings,
        // bullets, bold and italic kept; everything else as plain text.
        QString html = text;
        const auto re = [](const char *pattern) {
            return QRegularExpression(QString::fromLatin1(pattern),
                                      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        };
        html.remove(re("<(script|style|head)[^>]*>.*?</\\1>"));
        for (int level = 1; level <= 6; ++level)
            html.replace(re(QStringLiteral("<h%1[^>]*>(.*?)</h%1>").arg(level).toLatin1().constData()),
                         "\n" + QString(level, '#') + " \\1\n");
        html.replace(re("<(strong|b)(\\s[^>]*)?>(.*?)</\\1>"), "**\\3**");
        html.replace(re("<(em|i)(\\s[^>]*)?>(.*?)</\\1>"), "*\\3*");
        html.replace(re("<li[^>]*>"), "\n- ");
        html.replace(re("<(br|/p|/div|/li|/tr|/ul|/ol)[^>]*>"), "\n");
        html.remove(re("<[^>]+>"));
        html.replace("&nbsp;", " ").replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
            .replace("&#39;", "'");
        QStringList lines;
        for (const QString &l : html.split('\n')) {
            const QString t = l.simplified();
            if (t.isEmpty())
                continue;
            // A blank line before each heading, as Markdown reads best.
            if (t.startsWith('#') && !lines.isEmpty())
                lines << QString();
            lines << t;
        }
        return lines.join('\n');
    }
    return text;
}

void Workspaces::pickDecks(const QString &id) {
    if (!m_picker)
        return;
    m_picker->kind = "slides";
    m_pickingFor = id;
    m_picker->pickBooks(tr("Add slides"));
}

void Workspaces::addDecks(const QString &id, const QStringList &paths) {
    for (const QString &p : paths) {
        const QString path = p.startsWith("file:") ? QUrl(p).toLocalFile() : p;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix != "pdf" && suffix != "pptx") {
            emit message(tr("%1 isn't a PDF or PowerPoint file.").arg(QFileInfo(path).fileName()));
            continue;
        }
        m_queue << qMakePair(id, path);
    }
    importNext();
}

void Workspaces::importNext() {
    if (m_busy || m_queue.isEmpty())
        return;
    // Plain copies: lambdas can't capture structured bindings before C++20.
    const QPair<QString, QString> next = m_queue.takeFirst();
    const QString id = next.first, path = next.second;
    if (load(id).isEmpty()) {
        importNext();
        return;
    }
    m_busy = true;
    m_importing = tr("Reading %1…").arg(QFileInfo(path).completeBaseName());
    emit importingChanged();

    // Copying, converting and reading happen off the interface's thread.
    const QString deckId = newId();
    const QString dir = folder(id);
    const QString soffice = Slides::findLibreOffice();
    const QString profile = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/libreoffice";
    QPointer<Workspaces> self(this);
    QThread *worker = QThread::create([=]() {
        QVariantMap deck{{"id", deckId}, {"title", QFileInfo(path).completeBaseName()}};
        QString error;
        const QString suffix = QFileInfo(path).suffix().toLower();
        const QString copy = "decks/" + deckId + "." + suffix;
        QDir().mkpath(dir + "/decks");
        if (!QFile::copy(path, dir + "/" + copy)) {
            error = tr("Could not copy %1 into the workspace.").arg(QFileInfo(path).fileName());
        } else if (suffix == "pdf") {
            deck["kind"] = "pdf";
            deck["file"] = copy;
            deck["pdf"] = copy;
            QVariantList slides;
            for (const Slides::Slide &s : Slides::readPdf(dir + "/" + copy))
                slides << s.toMap();
            deck["slides"] = slides;
            if (slides.isEmpty())
                error = tr("%1 has no pages Shelf can read.").arg(QFileInfo(path).fileName());
        } else {
            deck["kind"] = "pptx";
            deck["file"] = copy;
            // Cards are read either way: their text links slides to books
            // and feeds the explanations.
            const QString media = "media/" + deckId;
            const QList<Slides::Slide> read = Slides::readPptx(dir + "/" + copy, dir + "/" + media);
            QVariantList slides;
            for (Slides::Slide s : read) {
                for (QString &image : s.images)
                    image = QDir(dir).relativeFilePath(image);
                slides << s.toMap();
            }
            deck["slides"] = slides;
            if (slides.isEmpty())
                error = tr("%1 has no slides Shelf can read.").arg(QFileInfo(path).fileName());
            else if (!soffice.isEmpty()) {
                const QString pdf = convertToPdf(soffice, dir + "/" + copy, profile);
                if (!pdf.isEmpty())
                    deck["pdf"] = QDir(dir).relativeFilePath(pdf);
            }
        }
        QMetaObject::invokeMethod(qApp, [self, id, deck, error]() {
            if (!self)
                return;
            self->m_busy = false;
            self->m_importing.clear();
            emit self->importingChanged();
            if (!error.isEmpty()) {
                emit self->message(error);
            } else {
                QVariantMap w = self->load(id);
                if (!w.isEmpty()) {
                    QVariantList decks = w.value("decks").toList();
                    decks << deck;
                    w["decks"] = decks;
                    self->save(id, w);
                }
            }
            self->importNext();
        });
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start(QThread::LowPriority);
}

void Workspaces::removeDeck(const QString &id, const QString &deckId) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (int i = 0; i < decks.size(); ++i) {
        const QVariantMap d = decks[i].toMap();
        if (d.value("id").toString() != deckId)
            continue;
        QFile::remove(folder(id) + "/" + d.value("file").toString());
        if (d.value("pdf").toString() != d.value("file").toString())
            QFile::remove(folder(id) + "/" + d.value("pdf").toString());
        QDir(folder(id) + "/media/" + deckId).removeRecursively();
        decks.removeAt(i);
        w["decks"] = decks;
        save(id, w);
        return;
    }
}

void Workspaces::moveDeck(const QString &id, const QString &deckId, int step) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (int i = 0; i < decks.size(); ++i)
        if (decks[i].toMap().value("id").toString() == deckId) {
            const int to = i + step;
            if (to < 0 || to >= decks.size())
                return;
            decks.move(i, to);
            w["decks"] = decks;
            save(id, w);
            return;
        }
}

void Workspaces::sortDecks(const QString &id) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(decks.begin(), decks.end(), [&](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value("title").toString(), b.toMap().value("title").toString()) < 0;
    });
    w["decks"] = decks;
    save(id, w);
}

void Workspaces::setDeckFolder(const QString &id, const QString &deckId, const QString &folder) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (QVariant &v : decks) {
        QVariantMap d = v.toMap();
        if (d.value("id").toString() != deckId)
            continue;
        d["folder"] = folder.trimmed();
        v = d;
        w["decks"] = decks;
        save(id, w);
        return;
    }
}

void Workspaces::renameFolder(const QString &id, const QString &from, const QString &to) {
    if (!to.trimmed().isEmpty())
        renameFolderTo(id, from, to.trimmed());
}

void Workspaces::removeFolder(const QString &id, const QString &folder) { renameFolderTo(id, folder, {}); }

void Workspaces::renameFolderTo(const QString &id, const QString &from, const QString &to) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (QVariant &v : decks) {
        QVariantMap d = v.toMap();
        if (d.value("folder").toString() == from) {
            d["folder"] = to;
            v = d;
        }
    }
    w["decks"] = decks;
    save(id, w);
}

void Workspaces::setSlideNote(const QString &id, const QString &deckId, int slide, const QString &note) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (QVariant &v : decks) {
        QVariantMap d = v.toMap();
        if (d.value("id").toString() != deckId)
            continue;
        QVariantList slides = d.value("slides").toList();
        if (slide < 0 || slide >= slides.size())
            return;
        QVariantMap s = slides[slide].toMap();
        if (s.value("myNote").toString() == note)
            return;
        s["myNote"] = note;
        slides[slide] = s;
        d["slides"] = slides;
        v = d;
        w["decks"] = decks;
        save(id, w);
        return;
    }
}

void Workspaces::addHistory(const QString &id, const QString &deckId, int slide, const QString &question) {
    QVariantMap w = load(id);
    if (w.isEmpty())
        return;
    QVariantList history = w.value("history").toList();
    for (int i = history.size() - 1; i >= 0; --i) {
        const QVariantMap h = history[i].toMap();
        if (h.value("deckId") == deckId && h.value("slide").toInt() == slide && h.value("question").toString() == question)
            history.removeAt(i);
    }
    history << QVariantMap{{"deckId", deckId}, {"slide", slide}, {"question", question},
                           {"time", QDateTime::currentMSecsSinceEpoch()}};
    while (history.size() > 500)
        history.removeFirst();
    w["history"] = history;
    save(id, w);
}

void Workspaces::setPlan(const QString &id, const QVariantMap &plan) {
    QVariantMap w = load(id);
    if (w.isEmpty())
        return;
    if (plan.isEmpty())
        w.remove("plan");
    else
        w["plan"] = plan;
    save(id, w);
}

void Workspaces::addLink(const QString &id, const QString &deckId, int slide, const QVariantMap &link) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (QVariant &v : decks) {
        QVariantMap d = v.toMap();
        if (d.value("id").toString() != deckId)
            continue;
        QVariantList slides = d.value("slides").toList();
        if (slide < 0 || slide >= slides.size())
            return;
        QVariantMap s = slides[slide].toMap();
        QVariantList links = s.value("links").toList();
        // The same place once only.
        for (const QVariant &l : links)
            if (l.toMap().value("bookId") == link.value("bookId") && l.toMap().value("target") == link.value("target"))
                return;
        links << link;
        s["links"] = links;
        slides[slide] = s;
        d["slides"] = slides;
        v = d;
        // A linked book is one of the workspace's books.
        QStringList books = w.value("books").toStringList();
        if (!books.contains(link.value("bookId").toString()))
            books << link.value("bookId").toString();
        w["books"] = books;
        w["decks"] = decks;
        save(id, w);
        return;
    }
}

void Workspaces::removeLink(const QString &id, const QString &deckId, int slide, int index) {
    QVariantMap w = load(id);
    QVariantList decks = w.value("decks").toList();
    for (QVariant &v : decks) {
        QVariantMap d = v.toMap();
        if (d.value("id").toString() != deckId)
            continue;
        QVariantList slides = d.value("slides").toList();
        if (slide < 0 || slide >= slides.size())
            return;
        QVariantMap s = slides[slide].toMap();
        QVariantList links = s.value("links").toList();
        if (index < 0 || index >= links.size())
            return;
        links.removeAt(index);
        s["links"] = links;
        slides[slide] = s;
        d["slides"] = slides;
        v = d;
        w["decks"] = decks;
        save(id, w);
        return;
    }
}

void Workspaces::setBooks(const QString &id, const QStringList &bookIds) {
    QVariantMap w = load(id);
    if (w.isEmpty())
        return;
    w["books"] = bookIds;
    save(id, w);
}

QVariantList Workspaces::sections(const QString &bookId) const {
    if (!m_library)
        return {};
    const QVariantMap book = m_library->book(bookId);
    const QString path = book.value("path").toString();
    return path.endsWith(".epub", Qt::CaseInsensitive) ? epubSections(path) : pdfSections(path);
}

QVariantList Workspaces::pdfSections(const QString &path) {
    QVariantList out;
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return out;
    QPdfBookmarkModel model;
    model.setDocument(&pdf);
    std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &parent, int depth) {
        for (int r = 0; r < model.rowCount(parent); ++r) {
            const QModelIndex i = model.index(r, 0, parent);
            out << QVariantMap{{"label", i.data(int(QPdfBookmarkModel::Role::Title)).toString().simplified()},
                               {"depth", depth}, {"target", i.data(int(QPdfBookmarkModel::Role::Page)).toInt()}};
            walk(i, depth + 1);
        }
    };
    walk({}, 0);
    return out;
}

QVariantList Workspaces::epubSections(const QString &path) {
    QVariantList out;
    ZipReader zip(path);
    if (!zip.isValid())
        return out;
    QString opfPath;
    {
        QXmlStreamReader xml(zip.read("META-INF/container.xml"));
        while (!xml.atEnd() && opfPath.isEmpty())
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"rootfile")
                opfPath = xml.attributes().value("full-path").toString();
    }
    if (opfPath.isEmpty())
        return out;
    const QString base = opfPath.contains('/') ? opfPath.section('/', 0, -2) + '/' : QString();
    QString nav, ncx;
    {
        QXmlStreamReader xml(zip.read(opfPath));
        while (!xml.atEnd())
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"item") {
                const auto a = xml.attributes();
                const QString href = QUrl::fromPercentEncoding(a.value("href").toUtf8());
                if (a.value("properties").toString().split(' ').contains("nav"))
                    nav = QDir::cleanPath(base + href);
                else if (a.value("media-type") == u"application/x-dtbncx+xml")
                    ncx = QDir::cleanPath(base + href);
            }
    }
    // Links are made whole (relative to the book's root), as the reader
    // names its chapters.
    const auto resolve = [](const QString &doc, const QString &href) {
        const QString dir = doc.contains('/') ? doc.section('/', 0, -2) + '/' : QString();
        const QString file = href.section('#', 0, 0), anchor = href.section('#', 1);
        const QString whole = file.isEmpty() ? doc : QDir::cleanPath(dir + QUrl::fromPercentEncoding(file.toUtf8()));
        return anchor.isEmpty() ? whole : whole + "#" + anchor;
    };
    if (!nav.isEmpty()) {
        QXmlStreamReader xml(zip.read(nav));
        int depth = -1;
        bool inToc = false, inLink = false;
        int tocDepth = 0;
        QString label, href;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                if (xml.name() == u"nav") {
                    const QString type = xml.attributes().value("http://www.idpf.org/2007/ops", "type").toString();
                    inToc = type.isEmpty() || type == "toc";
                    tocDepth = 0;
                    depth = -1;
                } else if (inToc && xml.name() == u"ol") {
                    ++depth;
                    ++tocDepth;
                } else if (inToc && xml.name() == u"a") {
                    inLink = true;
                    label.clear();
                    href = xml.attributes().value("href").toString();
                }
            } else if (token == QXmlStreamReader::Characters && inLink) {
                label += xml.text();
            } else if (token == QXmlStreamReader::EndElement) {
                if (xml.name() == u"a" && inLink) {
                    inLink = false;
                    if (!label.simplified().isEmpty())
                        out << QVariantMap{{"label", label.simplified()}, {"depth", qMax(0, depth)},
                                           {"target", resolve(nav, href)}};
                } else if (inToc && xml.name() == u"ol") {
                    --depth;
                    if (--tocDepth == 0)
                        inToc = false;
                } else if (xml.name() == u"nav") {
                    if (!out.isEmpty())
                        return out;   // the table of contents is the first nav with entries
                    inToc = false;
                }
            }
        }
        if (!out.isEmpty())
            return out;
    }
    if (!ncx.isEmpty()) {
        QXmlStreamReader xml(zip.read(ncx));
        int depth = -1;
        QString label;
        bool inText = false;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                if (xml.name() == u"navPoint") {
                    ++depth;
                    label.clear();
                } else if (xml.name() == u"text") {
                    inText = true;
                } else if (xml.name() == u"content" && !label.simplified().isEmpty()) {
                    out << QVariantMap{{"label", label.simplified()}, {"depth", qMax(0, depth)},
                                       {"target", resolve(ncx, xml.attributes().value("src").toString())}};
                }
            } else if (token == QXmlStreamReader::Characters && inText) {
                label += xml.text();
            } else if (token == QXmlStreamReader::EndElement) {
                if (xml.name() == u"text")
                    inText = false;
                else if (xml.name() == u"navPoint")
                    --depth;
            }
        }
    }
    return out;
}
