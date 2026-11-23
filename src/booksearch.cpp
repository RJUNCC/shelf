#include "booksearch.h"

#include "booktext.h"
#include "zipreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QXmlStreamReader>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

// One worker, at idle priority: reading books is background work.
QThreadPool &worker() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1);
        p->setThreadPriority(QThread::IdlePriority);
        return p;
    }();
    return *pool;
}

QStringList words(const QString &text) {
    static const QRegularExpression word(R"([\p{L}\p{N}]{2,})");
    QStringList out;
    for (auto it = word.globalMatch(text.toLower()); it.hasNext();)
        out << it.next().captured(0);
    return out;
}

// Words that say nothing about what a question is after.
const QSet<QString> &stopwords() {
    static const QSet<QString> set{
        "the", "a", "an", "and", "or", "of", "to", "in", "on", "for", "with", "by", "as", "at", "is", "are",
        "was", "were", "be", "been", "it", "its", "this", "that", "these", "those", "what", "which", "who",
        "whom", "how", "why", "when", "where", "does", "do", "did", "can", "could", "would", "should", "about",
        "from", "into", "than", "then", "there", "their", "they", "them", "you", "your", "we", "our", "i",
        "me", "my", "explain", "tell", "book", "chapter", "between", "difference", "describe", "mean", "means"};
    return set;
}

QString decodeEntities(QString text) {
    static const QRegularExpression numeric(R"(&#(x?)([0-9a-fA-F]+);)");
    QString out;
    qsizetype last = 0;
    for (auto it = numeric.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        bool ok = false;
        const uint code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        if (ok)
            out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
        last = m.capturedEnd();
    }
    out += text.mid(last);
    out.replace("&nbsp;", " ").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
        .replace("&apos;", "'").replace("&amp;", "&");
    return out;
}

// Text from XHTML: blocks become line breaks, tags go.
QString plainText(const QByteArray &html, QString *heading) {
    QString text = QString::fromUtf8(html);
    static const QRegularExpression drop(R"(<(script|style|head)\b.*?</\1>)",
                                         QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression firstHeading(R"(<h[1-3]\b[^>]*>(.*?)</h[1-3]>)",
                                                 QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression blocks(R"(</?(p|div|h[1-6]|li|br|tr|section|blockquote)\b[^>]*>)",
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(R"(<[^>]+>)");
    text.remove(drop);
    if (heading) {
        const auto m = firstHeading.match(text);
        *heading = m.hasMatch() ? decodeEntities(QString(m.captured(1)).remove(tags)).simplified() : QString();
    }
    text.replace(blocks, "\n");
    text.remove(tags);
    text = decodeEntities(text);
    QStringList lines;
    for (const QString &line : text.split('\n'))
        if (!line.simplified().isEmpty())
            lines << line.simplified();
    return lines.join('\n');
}

QString snippet(const QString &text, const QStringList &terms) {
    const QString lower = text.toLower();
    qsizetype at = -1;
    for (const QString &t : terms)
        if (const qsizetype i = lower.indexOf(t); i >= 0 && (at < 0 || i < at))
            at = i;
    const qsizetype from = std::max<qsizetype>(0, at - 70);
    QString part = text.mid(from, 200).simplified();
    // Bold the words found, for a rich-text label.
    part = part.toHtmlEscaped();
    for (const QString &t : terms) {
        const QRegularExpression re("(" + QRegularExpression::escape(t) + ")", QRegularExpression::CaseInsensitiveOption);
        part.replace(re, "<b>\\1</b>");
    }
    return (from > 0 ? "…" : "") + part + (from + 200 < text.size() ? "…" : "");
}

} // namespace

BookSearch::BookSearch(const QString &cacheDirectory, const QString &outlineCacheDirectory, QObject *parent)
    : QObject(parent),
      m_cache(!cacheDirectory.isEmpty() ? cacheDirectory
                                        : QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                                              + "/shelf/search"),
      m_outlineCache(!outlineCacheDirectory.isEmpty()
                         ? outlineCacheDirectory
                         : QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/text") {
    QDir().mkpath(m_cache);
}

BookSearch::~BookSearch() {
    if (m_cancelSearch)
        *m_cancelSearch = true;
    worker().waitForDone();
}

QString BookSearch::rangeText(const QList<Unit> &units, const QVariant &from, const QVariant &to) {
    // Where a target falls: a number is a page or section index; a link is
    // matched to its section's file by name, ignoring the #fragment.
    auto index = [&](const QVariant &target, int fallback) {
        if (!target.isValid() || target.toString().isEmpty())
            return fallback;
        bool number = false;
        const int n = target.toString().toInt(&number);
        if (number)
            return n;
        const QString file = target.toString().section('#', 0, 0);
        for (const Unit &u : units)
            if (!u.href.isEmpty() && (u.href.endsWith(file) || file.endsWith(u.href)))
                return u.target;
        return fallback;
    };
    const int first = index(from, 0), last = index(to, INT_MAX);
    QString out;
    for (const Unit &u : units)
        if (u.target >= first && (u.target < last || (last == first && u.target == first)))
            out += u.text + "\n";
    return out;
}

void BookSearch::chapterText(const QVariantMap &bookMap, const QVariant &from, const QVariant &to) {
    const Book book{bookMap.value("id").toString(), bookMap.value("title").toString(),
                    bookMap.value("path").toString(), bookMap.value("format").toString()};
    ++m_busy;
    emit busyChanged();
    QPointer<BookSearch> self(this);
    worker().start([self, this, book, from, to]() {
        static const std::atomic_bool never{false};
        const QString text = self ? rangeText(units(book, never), from, to) : QString();
        QMetaObject::invokeMethod(self, [self, book, from, text]() {
            if (!self)
                return;
            --self->m_busy;
            emit self->busyChanged();
            emit self->textReady(book.id, from, text);
        }, Qt::QueuedConnection);
    });
}

void BookSearch::forget(const QString &bookId) {
    QMutexLocker lock(&m_mutex);
    m_loaded.remove(bookId);
}

QList<BookSearch::Unit> BookSearch::epubUnits(const QString &path) {
    QList<Unit> out;
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
    QHash<QString, QString> manifest;   // id → href
    QStringList spine;
    QXmlStreamReader xml(zip.read(opfPath));
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == u"item")
            manifest.insert(xml.attributes().value("id").toString(), xml.attributes().value("href").toString());
        else if (xml.name() == u"itemref")
            spine << xml.attributes().value("idref").toString();
    }
    for (int i = 0; i < spine.size(); ++i) {
        const QString href = QUrl::fromPercentEncoding(manifest.value(spine[i]).toUtf8());
        if (href.isEmpty())
            continue;
        QString heading;
        const QString text = plainText(zip.read(QDir::cleanPath(base + href)), &heading);
        if (text.trimmed().isEmpty())
            continue;
        // Long chapters in pieces, so a passage is about one thing.
        const QStringList paragraphs = text.split('\n');
        QString piece;
        for (const QString &p : paragraphs) {
            piece += (piece.isEmpty() ? QString() : QStringLiteral("\n")) + p;
            if (piece.size() > 1500) {
                out.append({i, heading, piece, href});
                piece.clear();
            }
        }
        if (!piece.isEmpty())
            out.append({i, heading, piece, href});
    }
    return out;
}

QList<BookSearch::Unit> BookSearch::units(const Book &book, const std::atomic_bool &cancel) {
    {
        QMutexLocker lock(&m_mutex);
        if (auto it = m_loaded.constFind(book.id); it != m_loaded.constEnd())
            return *it;
    }
    const QFileInfo info(book.path);
    const QString stamp = QStringLiteral("%1:%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    const QString cacheFile = m_cache + "/" + book.id + ".json";
    QList<Unit> out;
    // The outline builder's text (with OCR) is better than our own reading.
    bool fromOutline = false;
    if (book.format == "pdf") {
        QFile file(m_outlineCache + "/" + book.id + ".json");
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonArray pages = QJsonDocument::fromJson(file.readAll()).object().value("pages").toArray();
            for (int i = 0; i < pages.size(); ++i)
                out.append({i, QString(), pages[i].toString()});
            fromOutline = !out.isEmpty();
        }
    }
    if (!fromOutline) {
        QFile file(cacheFile);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            if (root.value("version").toInt() == 2 && root.value("stamp").toString() == stamp)
                for (const QJsonValue &v : root.value("units").toArray()) {
                    const QJsonObject u = v.toObject();
                    out.append({u.value("t").toInt(), u.value("l").toString(), u.value("x").toString(),
                                u.value("h").toString()});
                }
        }
        if (out.isEmpty() && !cancel) {
            if (book.format == "pdf") {
                const QStringList pages = BookText::extractPdf(book.path, false, {}, cancel, nullptr);
                for (int i = 0; i < pages.size(); ++i)
                    out.append({i, QString(), pages[i]});
            } else {
                out = epubUnits(book.path);
            }
            if (cancel)
                return {};
            QJsonArray list;
            for (const Unit &u : std::as_const(out))
                list << QJsonObject{{"t", u.target}, {"l", u.label}, {"x", u.text}, {"h", u.href}};
            QSaveFile save(cacheFile);
            if (save.open(QIODevice::WriteOnly)) {
                save.write(QJsonDocument(QJsonObject{{"version", 2}, {"stamp", stamp}, {"units", list}})
                               .toJson(QJsonDocument::Compact));
                save.commit();
            }
        }
    }
    for (Unit &u : out)
        if (u.label.isEmpty() && book.format == "pdf")
            u.label = tr("p. %1").arg(u.target + 1);
    QMutexLocker lock(&m_mutex);
    m_loaded.insert(book.id, out);
    return out;
}

QVariantList BookSearch::find(const QList<Unit> &units, const QString &query, int limit) {
    const QStringList terms = words(query);
    QVariantList out;
    if (terms.isEmpty())
        return out;
    const QString phrase = query.simplified().toLower();
    struct Hit { int score; int index; };
    QList<Hit> hits;
    for (int i = 0; i < units.size(); ++i) {
        const QString lower = units[i].text.toLower();
        int score = 0;
        bool all = true;
        for (const QString &t : terms) {
            const int n = int(lower.count(t));
            if (n == 0) {
                all = false;
                break;
            }
            score += std::min(n, 10);
        }
        if (!all)
            continue;
        if (terms.size() > 1 && lower.contains(phrase))
            score += 50;   // the words together, as typed
        hits.append({score, i});
    }
    // Best first, then in reading order; one result per target.
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    QSet<int> seen;
    for (const Hit &h : std::as_const(hits)) {
        const Unit &u = units[h.index];
        if (seen.contains(u.target))
            continue;
        seen.insert(u.target);
        out << QVariantMap{{"target", u.target}, {"label", u.label}, {"snippet", snippet(u.text, terms)},
                           {"score", h.score}};
        if (out.size() >= limit)
            break;
    }
    return out;
}

QVariantList BookSearch::best(const QList<Unit> &units, const QString &question, int count) {
    // BM25 over the units, with the question's meaningful words.
    QStringList terms;
    for (const QString &w : words(question))
        if (!stopwords().contains(w) && !terms.contains(w))
            terms << w;
    QVariantList out;
    if (terms.isEmpty() || units.isEmpty())
        return out;
    QList<QStringList> unitWords;
    double totalLength = 0;
    QHash<QString, int> documentFrequency;
    for (const Unit &u : units) {
        unitWords << words(u.text);
        totalLength += unitWords.last().size();
        const QSet<QString> present(unitWords.last().cbegin(), unitWords.last().cend());
        for (const QString &t : std::as_const(terms))
            // Prefix match, so "enzyme" finds "enzymes".
            for (const QString &w : present)
                if (w.startsWith(t)) {
                    ++documentFrequency[t];
                    break;
                }
    }
    const double average = std::max(1.0, totalLength / units.size());
    const double k1 = 1.4, b = 0.75;
    QList<QPair<double, int>> scored;
    for (int i = 0; i < units.size(); ++i) {
        QHash<QString, int> frequency;
        for (const QString &w : std::as_const(unitWords[i]))
            for (const QString &t : std::as_const(terms))
                if (w.startsWith(t))
                    ++frequency[t];
        double score = 0;
        for (const QString &t : std::as_const(terms)) {
            const int f = frequency.value(t);
            if (!f)
                continue;
            const double df = documentFrequency.value(t);
            const double idf = std::log(1 + (units.size() - df + 0.5) / (df + 0.5));
            score += idf * f * (k1 + 1) / (f + k1 * (1 - b + b * unitWords[i].size() / average));
        }
        if (score > 0)
            scored.append({score, i});
    }
    std::sort(scored.begin(), scored.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
    for (int k = 0; k < scored.size() && out.size() < count; ++k) {
        const Unit &u = units[scored[k].second];
        out << QVariantMap{{"n", int(out.size()) + 1}, {"target", u.target}, {"label", u.label},
                           {"text", u.text.left(2000)}};
    }
    return out;
}

void BookSearch::search(const QString &query, const QVariantList &books) {
    if (m_cancelSearch)
        *m_cancelSearch = true;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancelSearch = cancel;
    QList<Book> list;
    for (const QVariant &v : books) {
        const QVariantMap m = v.toMap();
        list.append({m.value("id").toString(), m.value("title").toString(), m.value("path").toString(),
                     m.value("format").toString()});
    }
    ++m_busy;
    emit busyChanged();
    QPointer<BookSearch> self(this);
    worker().start([self, this, query, list, cancel]() {
        QVariantList results;
        for (const Book &book : list) {
            if (*cancel || !self)
                break;
            for (QVariant hit : find(units(book, *cancel), query, 30)) {
                QVariantMap m = hit.toMap();
                m["bookId"] = book.id;
                m["title"] = book.title;
                m["format"] = book.format;
                results << m;
            }
        }
        QMetaObject::invokeMethod(self, [self, query, results, cancel]() {
            if (!self)
                return;
            --self->m_busy;
            emit self->busyChanged();
            if (!*cancel)
                emit self->resultsReady(query, results);
        }, Qt::QueuedConnection);
    });
}

void BookSearch::passages(const QVariantMap &bookMap, const QString &question, int count) {
    const Book book{bookMap.value("id").toString(), bookMap.value("title").toString(),
                    bookMap.value("path").toString(), bookMap.value("format").toString()};
    ++m_busy;
    emit busyChanged();
    QPointer<BookSearch> self(this);
    worker().start([self, this, book, question, count]() {
        static const std::atomic_bool never{false};
        const QVariantList found = self ? best(units(book, never), question, count) : QVariantList();
        QMetaObject::invokeMethod(self, [self, book, question, found]() {
            if (!self)
                return;
            --self->m_busy;
            emit self->busyChanged();
            emit self->passagesReady(book.id, question, found);
        }, Qt::QueuedConnection);
    });
}
