#include "outlines.h"

#include "booktext.h"
#include "ollama.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <cstdlib>

namespace {

bool validId(const QString &id) {
    static const QRegularExpression hex("^[0-9a-f]{1,64}$");
    return hex.match(id).hasMatch();
}

// One worker for page text, so reading a second book waits its turn
// rather than fighting the first for the disk and the PDF engine.
QThreadPool &workers() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1);
        // Reading a book's pages is background work: at idle priority it
        // only runs on time nothing else wants.
        p->setThreadPriority(QThread::IdlePriority);
        return p;
    }();
    return *pool;
}

const QString systemPrompt = QStringLiteral(
    "You build the table of contents of a book from the text of its pages. The text was read from scanned "
    "pages and may contain OCR errors. Answer only with JSON in the requested shape.");

QJsonObject entriesSchema() {
    const QJsonObject entry{
        {"type", "object"},
        {"properties", QJsonObject{{"title", QJsonObject{{"type", "string"}}},
                                   {"page", QJsonObject{{"type", "integer"}}},
                                   {"level", QJsonObject{{"type", "integer"}}}}},
        {"required", QJsonArray{"title", "page", "level"}},
    };
    return {
        {"type", "object"},
        {"properties", QJsonObject{{"entries", QJsonObject{{"type", "array"}, {"items", entry}}}}},
        {"required", QJsonArray{"entries"}},
    };
}

} // namespace

Outlines::Outlines(Ollama *ollama, const QString &dataDirectory, const QString &cacheDirectory, QObject *parent)
    : QObject(parent),
      m_ollama(ollama),
      m_dataDirectory(!dataDirectory.isEmpty()
                          ? dataDirectory
                          : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf/outlines"),
      m_cacheDirectory(!cacheDirectory.isEmpty()
                           ? cacheDirectory
                           : QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/text") {
    QDir().mkpath(m_dataDirectory);
    QDir().mkpath(m_cacheDirectory);
}

Outlines::~Outlines() {
    for (const auto &job : std::as_const(m_jobs))
        *job->cancelled = true;
    workers().waitForDone();
}

void Outlines::touch(const QString &bookId) {
    ++m_revision;
    emit changed(bookId);
}

QVariantList Outlines::entries(const QString &bookId) const {
    if (const auto job = m_jobs.value(bookId); job && !job->entries.isEmpty())
        return job->entries;
    if (!validId(bookId))
        return {};
    if (auto it = m_saved.constFind(bookId); it != m_saved.constEnd())
        return *it;
    QVariantList out;
    QFile file(m_dataDirectory + "/" + bookId + ".json");
    if (file.open(QIODevice::ReadOnly)) {
        for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).object().value("entries").toArray()) {
            const QJsonObject e = v.toObject();
            out << QVariantMap{{"label", e.value("label").toString()},
                               {"depth", e.value("depth").toInt()},
                               {"target", e.value("page").toInt()}};
        }
    }
    m_saved.insert(bookId, out);
    return out;
}

QVariantMap Outlines::status(const QString &bookId) const {
    if (const auto job = m_jobs.value(bookId))
        return job->status;
    if (m_finished.contains(bookId))
        return m_finished.value(bookId);
    return {{"state", entries(bookId).isEmpty() ? "idle" : "done"}};
}

void Outlines::setStatus(const QString &bookId, const QString &state, int done, int total, const QString &message) {
    const QVariantMap status{{"state", state}, {"done", done}, {"total", total}, {"message", message}};
    if (const auto job = m_jobs.value(bookId))
        job->status = status;
    else
        m_finished.insert(bookId, status);
    touch(bookId);
}

void Outlines::generate(const QString &bookId, const QString &path, const QString &title) {
    if (!validId(bookId) || m_jobs.contains(bookId))
        return;
    auto job = std::make_shared<Job>();
    job->path = path;
    job->title = title;
    m_jobs.insert(bookId, job);
    m_finished.remove(bookId);
    setStatus(bookId, "reading");

    // Pages read once are kept, so trying again (or another model) starts
    // straight with the model.
    QStringList cached, cachedHeadings;
    if (cachedText(bookId, cached, cachedHeadings)) {
        QMetaObject::invokeMethod(this, [this, bookId, cached, cachedHeadings]() {
            textReady(bookId, cached, cachedHeadings);
        }, Qt::QueuedConnection);
        return;
    }
    QPointer<Outlines> self(this);
    const auto cancelled = job->cancelled;
    const bool ocr = m_ocr;
    workers().start([self, bookId, path, ocr, cancelled]() {
        const auto progress = [self, bookId](int done, int total, bool scanning) {
            QMetaObject::invokeMethod(self, [self, bookId, done, total, scanning]() {
                if (self && self->m_jobs.contains(bookId))
                    self->setStatus(bookId, scanning ? "scanning" : "reading", done, total);
            }, Qt::QueuedConnection);
        };
        QStringList headings;
        const QStringList pages = BookText::extractPdf(path, ocr, progress, *cancelled, &headings);
        QMetaObject::invokeMethod(self, [self, bookId, pages, headings]() {
            if (self)
                self->textReady(bookId, pages, headings);
        }, Qt::QueuedConnection);
    });
}

void Outlines::textReady(const QString &bookId, const QStringList &pages, const QStringList &headings) {
    const auto job = m_jobs.value(bookId);
    if (!job || *job->cancelled)
        return;
    int letters = 0;
    for (const QString &page : pages)
        for (const QChar c : page)
            letters += c.isLetter();
    if (pages.isEmpty() || letters < 200) {
        m_jobs.remove(bookId);
        setStatus(bookId, "failed", 0, 0,
                  pages.isEmpty() ? tr("This PDF could not be read.")
                                  : tr("This PDF has no text to read. Installing tesseract lets Shelf read "
                                       "pictures of pages."));
        return;
    }
    cacheText(bookId, pages, headings);
    // Books numbered by chapter ("1-5", as reference manuals are) print
    // their whole structure; read straight from the pages, it is exact and
    // needs no model.
    if (const QVariantList printed = BookText::printedOutline(pages); !printed.isEmpty()) {
        save(bookId, printed, true);
        m_jobs.remove(bookId);
        setStatus(bookId, "done", 1, 1);
        return;
    }
    job->pages = pages;
    job->headings = headings;
    job->repeated = BookText::repeatedHeadings(headings);
    // Scans whose text was machine-read give some garbled headings; for
    // those, a heading must be clearly larger and made of real words.
    job->noisy = BookText::headingNoise(headings) >= 0.09;
    if (job->noisy)
        job->vocabulary = BookText::vocabulary(pages);
    job->heads = BookText::runningHeads(pages);

    const int offset = BookText::pageNumberOffset(pages);
    job->hasOffset = offset != INT_MIN;
    job->offset = job->hasOffset ? offset : 0;
    QStringList contents;
    job->contentsPages = BookText::contentsPages(pages);
    for (int i : std::as_const(job->contentsPages))
        contents << pages[i];
    job->parts = BookText::contentsParts(contents.join('\n'), job->title);
    job->contents = contents.join('\n').left(5000);
    // Where the printed contents says each chapter starts, as PDF pages.
    // Without page numbers to line them up, the printed numbers are only a
    // rough guide, so they are left out.
    if (job->hasOffset) {
        for (const auto &line : BookText::contentsLines(job->contents, int(pages.size())))
            job->hints << QStringLiteral("%1\t%2").arg(line.printedPage + job->offset).arg(line.title);
    }
    job->batches = int((pages.size() + batchSize - 1) / batchSize);
    nextBatch(bookId);
}

QString Outlines::prompt(const Job &job, int first, int last) const {
    QStringList hints;
    for (const QString &hint : job.hints) {
        const int index = hint.section('\t', 0, 0).toInt();
        if (index >= first && index <= last)
            hints << QStringLiteral("- \"%1\" starts at [page %2]").arg(hint.section('\t', 1), QString::number(index + 1));
    }
    QString text = QStringLiteral("Book: %1\n\n").arg(job.title);
    if (!job.contents.isEmpty())
        text += QStringLiteral("The book's printed table of contents, as scanned (it may be garbled):\n%1\n\n")
                    .arg(job.contents);
    if (!hints.isEmpty())
        text += QStringLiteral("By its page numbers, the table of contents puts these on the pages below "
                               "(titles may be garbled):\n%1\n\n").arg(hints.join('\n'));
    text += QStringLiteral(
        "Below is the start of each page from [page %1] to [page %2]. List every chapter and major section "
        "that BEGINS on one of these pages, in order, with the [page N] number where it begins.\n"
        "- Only list headings that actually appear in the page text below, copied word for word; never "
        "describe or summarize a page, and never list a sentence. The table of contents is only for "
        "spelling headings correctly.\n"
        "- Only call something a chapter if the book does; a section inside a chapter is not a new chapter.\n"
        "- A page whose text is little more than a title, such as \"[page 157] Chapter 14 The Circulatory "
        "System\", is where that chapter begins; always list it.\n"
        "- Keep numbers such as \"Chapter 14\", fix OCR errors, and write titles in Title Case.\n"
        "- List a section only on the page where it starts, not again on the pages that continue it. Leave "
        "out running headers that repeat at the top of pages, page numbers, single questions or answers, "
        "and the table of contents itself.\n"
        "- Where a page lists Headings, they are the lines printed larger than its text, with their size; "
        "chapters, sections, and subsections are almost always among them. Copy them exactly. A heading "
        "may be split over two lines; join it.\n"
        "- Level 0 is a part or chapter; level 1 is a section inside a chapter; level 2 is a subsection "
        "inside a section, usually printed smaller than the section heading.\n"
        "If nothing begins on these pages, return an empty list.\n\n%3")
        .arg(first + 1).arg(last + 1).arg(BookText::digest(job.pages, first, last, 240, job.contentsPages, job.headings));
    return text;
}

void Outlines::nextBatch(const QString &bookId) {
    const auto job = m_jobs.value(bookId);
    if (!job || *job->cancelled)
        return;
    if (job->batch >= job->batches) {
        save(bookId, job->entries, true);
        m_jobs.remove(bookId);
        setStatus(bookId, "done", job->batches, job->batches);
        return;
    }
    setStatus(bookId, "thinking", job->batch, job->batches);
    const int first = job->batch * batchSize;
    const int last = std::min<int>(int(job->pages.size()) - 1, first + batchSize - 1);
    const auto cancelled = job->cancelled;
    QPointer<Outlines> self(this);
    auto done = [self, bookId, cancelled](const QString &answer, const QString &error) {
        if (!self || *cancelled)
            return;
        const auto job = self->m_jobs.value(bookId);
        if (!job)
            return;
        if (!error.isEmpty()) {
            // What was found so far stays, and trying again starts over.
            self->save(bookId, job->entries, false);
            self->m_jobs.remove(bookId);
            self->setStatus(bookId, "failed", job->batch, job->batches, error);
            return;
        }
        // Chapter title pages in this batch go in even if the model skipped
        // them; the model's own wording wins when it found them too.
        QVariantList found = BookText::parseEntries(answer.toUtf8(), int(job->pages.size()));
        const int first = job->batch * batchSize, last = first + batchSize - 1;
        // Pages whose headings were measured give their sections exactly,
        // in print order; the model's sections there are replaced, its
        // chapters kept.
        const QVariantList measured = BookText::headingEntries(job->headings, first, last, job->repeated,
                                                               job->noisy ? job->vocabulary : QHash<QString, int>());
        QSet<int> measuredPages;
        for (const QVariant &v : measured)
            measuredPages.insert(v.toMap()["target"].toInt());
        static const QRegularExpression chapterOrPart(R"(^(chapter|part)\b)", QRegularExpression::CaseInsensitiveOption);
        found.erase(std::remove_if(found.begin(), found.end(), [&](const QVariant &f) {
            return measuredPages.contains(f.toMap()["target"].toInt())
                && !chapterOrPart.match(f.toMap()["label"].toString()).hasMatch();
        }), found.end());
        found += measured;

        // A page that is only "Chapter 4 / Biochemistry" names its chapter
        // better than a model that copied half of it, so it replaces what
        // the model put on that page.
        for (const QVariant &v : BookText::titlePages(job->pages)) {
            const int page = v.toMap()["target"].toInt();
            if (page < first || page > last)
                continue;
            const QString title = v.toMap()["label"].toString();
            found.erase(std::remove_if(found.begin(), found.end(), [&](const QVariant &f) {
                return std::abs(f.toMap()["target"].toInt() - page) <= 1
                    && BookText::sameHeading(f.toMap()["label"].toString(), title);
            }), found.end());
            found << v;
        }
        // Nothing starts on the contents pages, whatever their entries say.
        found.erase(std::remove_if(found.begin(), found.end(), [&](const QVariant &v) {
            return job->contentsPages.contains(v.toMap()["target"].toInt());
        }), found.end());
        job->entries = BookText::tidyEntries(BookText::mergeEntries(job->entries, found), job->heads, job->title,
                                             job->parts, job->pages);
        self->save(bookId, job->entries, false);
        ++job->batch;
        self->nextBatch(bookId);
    };
    const QString text = prompt(*job, first, last);
    if (m_ask)
        m_ask(systemPrompt, text, entriesSchema(), done);
    else
        m_ollama->ask(systemPrompt, text, entriesSchema(), done);
}

void Outlines::cancel(const QString &bookId) {
    const auto job = m_jobs.take(bookId);
    if (!job)
        return;
    *job->cancelled = true;
    if (!job->entries.isEmpty())
        save(bookId, job->entries, false);
    setStatus(bookId, "idle");
}

void Outlines::clear(const QString &bookId) {
    cancel(bookId);
    if (!validId(bookId))
        return;
    QFile::remove(m_dataDirectory + "/" + bookId + ".json");
    m_saved.remove(bookId);
    m_finished.remove(bookId);
    touch(bookId);
}

void Outlines::save(const QString &bookId, const QVariantList &entries, bool complete) const {
    QJsonArray list;
    for (const QVariant &v : entries) {
        const QVariantMap e = v.toMap();
        list << QJsonObject{{"label", e["label"].toString()}, {"depth", e["depth"].toInt()},
                            {"page", e["target"].toInt()}};
    }
    QSaveFile file(m_dataDirectory + "/" + bookId + ".json");
    const QJsonObject root{{"version", 1}, {"complete", complete}, {"entries", list}};
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.commit();
    }
    m_saved.insert(bookId, entries);
}

bool Outlines::cachedText(const QString &bookId, QStringList &pages, QStringList &headings) const {
    QFile file(m_cacheDirectory + "/" + bookId + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    // Older caches lack heading sizes; text read without OCR is read again
    // when OCR is wanted now.
    if (root.value("version").toInt() != 8 || (m_ocr && !root.value("ocr").toBool()))
        return false;
    pages.clear();
    headings.clear();
    for (const QJsonValue &v : root.value("pages").toArray())
        pages << v.toString();
    for (const QJsonValue &v : root.value("headings").toArray())
        headings << v.toString();
    return !pages.isEmpty();
}

void Outlines::cacheText(const QString &bookId, const QStringList &pages, const QStringList &headings) const {
    QSaveFile file(m_cacheDirectory + "/" + bookId + ".json");
    const QJsonObject root{{"version", 8}, {"ocr", m_ocr}, {"pages", QJsonArray::fromStringList(pages)},
                           {"headings", QJsonArray::fromStringList(headings)}};
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
