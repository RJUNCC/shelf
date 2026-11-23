#include "booktext.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QImage>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QElapsedTimer>
#include <QProcess>
#ifdef Q_OS_LINUX
#include <sched.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QThread>
#include <QThread>
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>

namespace BookText {

namespace {

int letterCount(const QString &text) {
    int n = 0;
    for (const QChar c : text)
        if (c.isLetter())
            ++n;
    return n;
}

QStringList nonEmptyLines(const QString &text) {
    QStringList out;
    for (const QString &line : text.split('\n')) {
        const QString t = line.simplified();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

} // namespace

bool isSparse(const QString &text) { return letterCount(text) < 60; }

// The lines of a page printed larger than its body text. Each line's
// height comes from the PDF's own text geometry; the body height is the
// typical height of its long lines.
QString headingLines(QPdfDocument &doc, int page, const QString &text, const QList<QPolygonF> &runs) {
    struct Line { QString text; int start; };
    QList<Line> lines;
    int pos = 0;
    for (const QString &raw : text.split('\n')) {
        const QString t = raw.trimmed();
        if (!t.isEmpty())
            lines.append({t, pos + int(raw.indexOf(t))});
        pos += int(raw.size()) + 1;
    }
    auto height = [&](const Line &l) {
        return doc.getSelectionAtIndex(page, l.start, int(l.text.size())).boundingRectangle().height();
    };
    // Body height: the median height of the page's wide runs of text, which
    // the page's text already came with, so no extra lookups are needed.
    // Some text layers (scans especially) store every letter as its own
    // box: join boxes beside each other at the same height into lines first.
    QList<QRectF> joined;
    for (const QPolygonF &run : runs) {
        const QRectF box = run.boundingRect();
        if (box.isEmpty())
            continue;
        if (!joined.isEmpty()) {
            QRectF &line = joined.last();
            const qreal overlap = std::min(line.bottom(), box.bottom()) - std::max(line.top(), box.top());
            const qreal gap = box.left() - line.right();
            if (overlap > std::min(line.height(), box.height()) * 0.5 && gap < box.height() * 2.5 && gap > -box.height()) {
                line = line.united(box);
                continue;
            }
        }
        joined << box;
    }
    const qreal pageWidth = doc.pagePointSize(page).width();
    QList<qreal> body;
    for (const QRectF &box : std::as_const(joined))
        if (box.width() > pageWidth * 0.25)
            body << box.height();
    if (body.size() < 2)
        return {};
    std::sort(body.begin(), body.end());
    const qreal typical = body[body.size() / 2];
    if (typical <= 0)
        return {};
    struct Heading { QString text; qreal ratio; int line; };
    QList<Heading> found;
    for (int i = 0; i < lines.size(); ++i) {
        const Line &l = lines[i];
        // Headings are short, start with a capital or a number, and do not
        // end a sentence or hold one.
        // A heading in capitals may wrap after a comma ("THE CIRCULATORY,
        // RESPIRATORY,"); in ordinary text a trailing comma means a sentence.
        const bool caps = l.text == l.text.toUpper();
        if (l.text.size() > 90 || letterCount(l.text) < 3 || l.text.endsWith('.') || (l.text.endsWith(',') && !caps)
            || l.text.contains(". ") || !(l.text[0].isUpper() || l.text[0].isDigit()))
            continue;
        const qreal ratio = height(l) / typical;
        if (ratio < 1.08 || ratio >= 6)
            continue;
        // A heading that wraps continues on the next line at the same size.
        // Headings in capitals wrap with a little more size wobble in scans.
        const bool bothCaps = !found.isEmpty() && found.last().text == found.last().text.toUpper() && caps;
        const double difference = found.isEmpty() ? 1 : std::abs(found.last().ratio - ratio) / std::max(found.last().ratio, ratio);
        // A line that ended on a comma always continues.
        const bool continues = !found.isEmpty() && found.last().text.endsWith(',');
        if (!found.isEmpty() && found.last().line == i - 1
            && (continues || difference < (bothCaps ? 0.25 : 0.04)) && !l.text[0].isDigit()) {
            found.last().text += " " + l.text;
            found.last().line = i;
            continue;
        }
        found.append({l.text, ratio, i});
    }
    QStringList out;
    for (const Heading &h : std::as_const(found))
        if (!h.text.endsWith(','))
            out << QStringLiteral("%1\t%2").arg(h.ratio, 0, 'f', 2).arg(h.text);
    return out.join('\n');
}

QStringList extractPdf(const QString &path, bool ocr, const Progress &progress, const std::atomic_bool &cancel,
                       QStringList *headings) {
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None)
        return {};
    const int count = doc.pageCount();
    QStringList pages;
    QList<int> sparse;
    if (headings)
        headings->clear();
    for (int i = 0; i < count && !cancel; ++i) {
        const QPdfSelection all = doc.getAllText(i);
        pages << all.text();
        if (headings)
            *headings << headingLines(doc, i, pages.last(), all.bounds());
        if (isSparse(pages.last()))
            sparse << i;
        if (progress && (i % 10 == 0 || i == count - 1))
            progress(i + 1, count, false);
    }
    QString tesseract = ocr ? QStandardPaths::findExecutable("tesseract") : QString();
#ifdef Q_OS_WIN
    // The Windows installer (UB Mannheim) doesn't put it on the PATH.
    if (ocr && tesseract.isEmpty())
        tesseract = QStandardPaths::findExecutable("tesseract", {QStringLiteral("C:/Program Files/Tesseract-OCR"),
                                                                 QStringLiteral("C:/Program Files (x86)/Tesseract-OCR")});
#endif
    QTemporaryDir scratch;
    if (tesseract.isEmpty() || !scratch.isValid() || cancel || sparse.isEmpty())
        return pages;

    // Gentle on the computer rather than fast: the reader is reading
    // meanwhile, so the book is read in the background with one tesseract
    // on one thread, at the lowest priority (it only gets time no one else
    // wants), resting after each page as long as the page took, so even an
    // idle machine sees one core half busy, not every core flat out.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("OMP_THREAD_LIMIT", "1");
    const int parallel = 1;
    struct Running { std::shared_ptr<QProcess> process; int page; QElapsedTimer clock; };
    QList<Running> running;
    int next = 0, done = 0;
    auto start = [&](int page) {
        const QSizeF points = doc.pagePointSize(page);
        // Picture-only pages in a book with text are mostly titles in large
        // type, which read as well at 150 dpi for half the rendering; a book
        // that is all pictures needs 200 dpi for its body text.
        const double dpi = sparse.size() * 2 > count ? 200.0 : 150.0;
        const QSize size = (points * (dpi / 72.0)).toSize();
        if (size.isEmpty())
            return false;
        QImage rendered = doc.render(page, size);
        if (rendered.isNull())
            return false;
        // Transparent pages read as black; put them on white paper first.
        QImage paper(rendered.size(), QImage::Format_Grayscale8);
        paper.fill(Qt::white);
        QPainter(&paper).drawImage(0, 0, rendered);
        const QString file = QStringLiteral("%1/page-%2.pgm").arg(scratch.path()).arg(page);
        if (!paper.save(file, "PGM"))
            return false;
        auto process = std::make_shared<QProcess>();
        process->setProcessEnvironment(environment);
#if defined(Q_OS_LINUX)
        process->setChildProcessModifier([] {
            (void)::nice(19);
            sched_param idle{};
            (void)::sched_setscheduler(0, SCHED_IDLE, &idle);
        });
#elif defined(Q_OS_UNIX)
        process->setChildProcessModifier([] { (void)::nice(19); });
#elif defined(Q_OS_WIN)
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= IDLE_PRIORITY_CLASS;
        });
#endif
        process->start(tesseract, {file, "stdout", "--psm", "3"});
        QElapsedTimer clock;
        clock.start();
        running.append({process, page, clock});
        return true;
    };
    while ((next < sparse.size() || !running.isEmpty()) && !cancel) {
        while (running.size() < parallel && next < sparse.size())
            if (!start(sparse[next++]))
                ++done;
        // Collect whichever finishes.
        for (int i = 0; i < running.size(); ++i) {
            QProcess &process = *running[i].process;
            if (!process.waitForFinished(i == 0 ? 50 : 0) && process.state() != QProcess::NotRunning)
                continue;
            if (process.exitCode() == 0) {
                const QString read = QString::fromUtf8(process.readAllStandardOutput());
                const int page = running[i].page;
                if (letterCount(read) > letterCount(pages[page]))
                    pages[page] = read;
            }
            QFile::remove(QStringLiteral("%1/page-%2.pgm").arg(scratch.path()).arg(running[i].page));
            const qint64 took = running[i].clock.elapsed();
            running.removeAt(i--);
            ++done;
            if (progress)
                progress(done, int(sparse.size()), true);
            // Rest as long as the page took (up to a few seconds).
            for (qint64 rested = 0; rested < std::min<qint64>(took, 4000) && !cancel; rested += 50)
                QThread::msleep(50);
        }
    }
    for (auto &r : running)
        r.process->kill();
    return pages;
}

int pageNumberOffset(const QStringList &pages) {
    // A page number sits alone in a header or footer line, or beside a
    // bar or the running title: "146 | For more free content", "Contents | 7".
    static const QRegularExpression leading(R"(^(\d{1,4})(?:\s*[|•·]|\s*$|\s+\S))");
    static const QRegularExpression trailing(R"((?:[|•·]\s*|^|\S\s+)(\d{1,4})$)");
    QHash<int, int> votes;
    for (int i = 0; i < pages.size(); ++i) {
        const QStringList lines = nonEmptyLines(pages[i]);
        QStringList edges = lines.mid(0, 2) + lines.mid(std::max<qsizetype>(2, lines.size() - 2));
        QList<int> seen;
        for (const QString &line : std::as_const(edges)) {
            if (line.size() > 70)
                continue;
            for (const auto *re : {&leading, &trailing}) {
                const auto m = re->match(line);
                if (!m.hasMatch())
                    continue;
                const int number = m.captured(1).toInt();
                const int offset = i - number;
                if (number > 0 && offset >= 0 && !seen.contains(offset)) {
                    seen << offset;
                    ++votes[offset];
                }
            }
        }
    }
    int best = INT_MIN, bestVotes = 0, second = 0;
    for (auto it = votes.cbegin(); it != votes.cend(); ++it) {
        if (it.value() > bestVotes) {
            second = bestVotes;
            bestVotes = it.value();
            best = it.key();
        } else if (it.value() > second) {
            second = it.value();
        }
    }
    // Trust it only when many pages agree and nothing else comes close.
    if (bestVotes < std::max<qsizetype>(3, pages.size() / 20) || bestVotes < second * 2)
        return INT_MIN;
    return best;
}

QList<int> contentsPages(const QStringList &pages) {
    static const QRegularExpression endsInNumber(R"(\d{1,4}$)");
    auto numberedShare = [](const QString &text) {
        const QStringList lines = nonEmptyLines(text);
        if (lines.size() < 5)
            return 0.0;
        int numbered = 0;
        for (const QString &line : lines)
            if (endsInNumber.match(line).hasMatch())
                ++numbered;
        return double(numbered) / lines.size();
    };
    // Contents converted from an ebook have no page numbers, only entries:
    // "CHAPTER 5: MOLECULAR BIOLOGY", "5.1 DNA Structure", "A.13 Stem Cells".
    static const QRegularExpression entry(R"(^((chapter|part|appendix|unit)\b|\d+(\.\d+)+\s|[A-Z]\.\d+\s))",
                                          QRegularExpression::CaseInsensitiveOption);
    auto entryShare = [](const QString &text) {
        const QStringList lines = nonEmptyLines(text);
        if (lines.size() < 5)
            return 0.0;
        int entries = 0;
        for (const QString &line : lines)
            if (entry.match(line).hasMatch())
                ++entries;
        return double(entries) / lines.size();
    };
    auto listing = [&](const QString &text, double numbered, double entries) {
        return numberedShare(text) >= numbered || entryShare(text) >= entries;
    };
    const int scan = std::min<qsizetype>(40, pages.size());
    for (int i = 0; i < scan; ++i) {
        const QStringList head = nonEmptyLines(pages[i]).mid(0, 6);
        if (!head.join(' ').contains("contents", Qt::CaseInsensitive) || !listing(pages[i], 0.2, 0.2))
            continue;
        QList<int> out{i};
        for (int j = i + 1; j < pages.size() && out.size() < 12 && listing(pages[j], 0.25, 0.3); ++j)
            out << j;
        return out;
    }
    return {};
}

QList<ContentsLine> contentsLines(const QString &contentsText, int pageCount) {
    // A title, then dot leaders or OCR noise, then the page number.
    static const QRegularExpression line(R"(^(.*?[A-Za-z].*?)[\s.·…_\-,:;|'"‘’“”]*(\d{1,4})$)");
    QList<ContentsLine> out;
    for (const QString &text : nonEmptyLines(contentsText)) {
        const auto m = line.match(text);
        if (!m.hasMatch())
            continue;
        const QString title = m.captured(1).trimmed();
        const int page = m.captured(2).toInt();
        if (letterCount(title) < 3 || page <= 0 || page > pageCount)
            continue;
        out.append({title.left(120), page});
    }
    return out;
}

QString digest(const QStringList &pages, int first, int last, int chars, const QList<int> &contents,
               const QStringList &headings) {
    QStringList out;
    for (int i = std::max(0, first); i <= last && i < pages.size(); ++i) {
        if (contents.contains(i)) {
            out << QStringLiteral("[page %1] (the table of contents; nothing begins here)").arg(i + 1);
            continue;
        }
        // Larger lines first, by size, so headings stand out from the text.
        QStringList marked;
        if (i < headings.size()) {
            for (const QString &line : headings[i].split('\n', Qt::SkipEmptyParts)) {
                const double ratio = line.section('\t', 0, 0).toDouble();
                const QString size = ratio >= 1.6 ? "very large" : ratio >= 1.25 ? "large" : "medium";
                marked << QStringLiteral("\"%1\" (%2)").arg(line.section('\t', 1), size);
            }
        }
        if (!marked.isEmpty()) {
            QString text = pages[i].simplified();
            if (text.size() > chars / 2)
                text = text.left(chars / 2) + "…";
            out << QStringLiteral("[page %1] Headings: %2. Text: %3").arg(i + 1).arg(marked.join(", "), text);
            continue;
        }
        QString text = pages[i].simplified();
        if (text.size() > chars)
            text = text.left(chars) + "…";
        out << QStringLiteral("[page %1] %2").arg(i + 1).arg(text.isEmpty() ? QStringLiteral("(no text)") : text);
    }
    return out.join('\n');
}

QVariantList parseEntries(const QByteArray &json, int pageCount) {
    QByteArray body = json.trimmed();
    // Some models wrap the JSON in a code fence despite being asked not to.
    if (body.startsWith("```")) {
        body = body.mid(body.indexOf('\n') + 1);
        if (body.endsWith("```"))
            body.chop(3);
    }
    const QJsonValue entries = QJsonDocument::fromJson(body).object().value("entries");
    QVariantList out;
    for (const QJsonValue &value : entries.toArray()) {
        const QJsonObject e = value.toObject();
        const QString title = e.value("title").toString().simplified();
        const int page = e.value("page").toInt(-1);
        if (title.isEmpty() || page < 1 || page > pageCount)
            continue;
        const int level = std::clamp(e.value("level").toInt(0), 0, 2);
        out << QVariantMap{{"label", title}, {"depth", level}, {"level", level}, {"target", page - 1}};
    }
    return out;
}

QVariantList mergeEntries(const QVariantList &a, const QVariantList &b) {
    QVariantList all = a + b;
    // Page order; on a page, chapters first, then headings in the order
    // they are printed, then anything else by level.
    auto within = [](const QVariantMap &e) {
        return e.contains("order") ? e["order"].toInt() : e["depth"].toInt() == 0 ? 0 : 100 + e["depth"].toInt();
    };
    std::stable_sort(all.begin(), all.end(), [&](const QVariant &x, const QVariant &y) {
        const QVariantMap l = x.toMap(), r = y.toMap();
        if (l["target"].toInt() != r["target"].toInt())
            return l["target"].toInt() < r["target"].toInt();
        return within(l) < within(r);
    });
    QVariantList out;
    for (const QVariant &v : std::as_const(all)) {
        const QVariantMap e = v.toMap();
        const bool repeat = std::any_of(out.cbegin(), out.cend(), [&](const QVariant &o) {
            const QVariantMap k = o.toMap();
            return k["target"] == e["target"]
                && k["label"].toString().compare(e["label"].toString(), Qt::CaseInsensitive) == 0;
        });
        if (!repeat)
            out << e;
    }
    return out;
}

QStringList runningHeads(const QStringList &pages) {
    QHash<QString, int> counts;
    for (const QString &page : pages) {
        QStringList seen;
        for (const QString &line : nonEmptyLines(page).mid(0, 2)) {
            const QString key = line.toUpper();
            if (key.size() < 60 && letterCount(key) >= 3 && !seen.contains(key)) {
                seen << key;
                ++counts[key];
            }
        }
    }
    QStringList out;
    const int needed = std::max<qsizetype>(5, pages.size() / 10);
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() >= needed)
            out << it.key();
    return out;
}

namespace {

QString titleCase(const QString &title, const QSet<QString> &acronyms) {
    static const QSet<QString> small{"a", "an", "and", "as", "at", "but", "by", "for", "in", "nor", "of",
                                     "on", "or", "the", "to", "vs", "with"};
    static const QRegularExpression roman("^[IVXLC]+$");
    QStringList words = title.split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < words.size(); ++i) {
        QString &w = words[i];
        // Keep the punctuation around a word, change only its letters.
        int start = 0, end = int(w.size());
        while (start < end && !w[start].isLetterOrNumber())
            ++start;
        while (end > start && !w[end - 1].isLetterOrNumber())
            --end;
        const QString core = w.mid(start, end - start);
        bool digits = false;
        for (const QChar c : core)
            digits |= c.isDigit();
        if (core.isEmpty() || digits || acronyms.contains(core) || roman.match(core).hasMatch())
            continue;
        QString cased = core.toLower();
        const bool afterColon = i > 0 && words[i - 1].endsWith(':');
        // The first word after a section number starts the title: "5.3 The Role".
        bool first = true;
        for (int k = 0; k < i && first; ++k)
            first = !words[k].isEmpty() && words[k][0].isDigit();
        if (first || afterColon || !small.contains(cased))
            cased[0] = cased[0].toUpper();
        // Hyphenated words: "Self-Identity".
        for (int k = 1; k < cased.size(); ++k)
            if (cased[k - 1] == '-')
                cased[k] = cased[k].toUpper();
        w = w.left(start) + cased + w.mid(end);
    }
    return words.join(' ');
}

// All-caps words a mostly mixed-case text writes that way, such as MCAT in
// "Princeton Review MCAT Workout"; a text in capitals says nothing.
void collectAcronyms(const QString &text, QSet<QString> &acronyms, bool trusted = false) {
    static const QRegularExpression word(R"(\b[A-Z][A-Z0-9]{1,5}\b)");
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    QStringList caps;
    for (auto it = word.globalMatch(text); it.hasNext();)
        caps << it.next().captured(0);
    // Count every word in capitals, long ones too, to judge the text.
    int upper = 0;
    for (const QString &w : words)
        upper += w == w.toUpper() && letterCount(w) > 0;
    if (trusted || (!words.isEmpty() && upper * 3 <= words.size()))
        for (const QString &c : std::as_const(caps))
            acronyms.insert(c);
}

// Acronyms the book's own sentences use (MCAT, DNA, ATP): capitalised
// words that keep turning up in ordinary mixed-case lines.
QSet<QString> textAcronyms(const QStringList &pages) {
    static const QRegularExpression word(R"(\b[A-Z][A-Z0-9]{1,5}\b)");
    static const QRegularExpression lower(R"(\b[a-z]{2,6}\b)");
    QHash<QString, int> counts, lowerCounts;
    for (const QString &page : pages) {
        for (const QString &line : page.split('\n')) {
            if (line.size() < 30 || line == line.toUpper())
                continue;
            for (auto it = word.globalMatch(line); it.hasNext();)
                ++counts[it.next().captured(0)];
            for (auto it = lower.globalMatch(line); it.hasNext();)
                ++lowerCounts[it.next().captured(0)];
        }
    }
    // "AND" in capitals is a heading's styling when the book says "and"
    // everywhere else; "DNA" is an acronym because it never says "dna".
    QSet<QString> out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() >= 3 && lowerCounts.value(it.key().toLower()) < it.value())
            out.insert(it.key());
    return out;
}

// How alike two strings are, 0–1, by edit distance: "Ildentity" and
// "Identity" are alike; "Enzymes" and "Lipids" are not.
double similarity(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty())
        return 0;
    QList<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return 1.0 - double(row[b.size()]) / std::max(a.size(), b.size());
}

QString normalized(const QString &title) {
    QString out;
    for (const QChar c : title.toLower())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

} // namespace

QVariantList titlePages(const QStringList &pages) {
    // OCR sometimes runs the number into the word or adds a stray stroke:
    // "Chapter1l MCAT Basics" is Chapter 1, "Chapter 4y Enzymes" Chapter 4.
    static const QRegularExpression title(R"(^Chapter\s*(\d{1,3})(?:[a-z|]|I)?[:.]?\s+(\S.{2,100})$)");
    QVariantList out;
    for (int i = 0; i < pages.size(); ++i) {
        const QString text = pages[i].simplified();
        if (text.size() > 120)
            continue;
        // "CHAPTER" in capitals too; the letter after the number stays
        // case-sensitive, so a real "4A" is not mistaken for a slip.
        const QString probe = text.startsWith("CHAPTER") ? "Chapter" + text.mid(7) : text;
        const auto m = title.match(probe.startsWith("chapter") ? "Chapter" + probe.mid(7) : probe);
        if (m.hasMatch() && letterCount(m.captured(2)) >= 3)
            out << QVariantMap{{"label", QStringLiteral("Chapter %1 %2").arg(m.captured(1), m.captured(2))},
                               {"depth", 0}, {"target", i}, {"titlePage", true}};
    }
    return out;
}

namespace {
// Whether a line reads as words: most of its tokens are words with a vowel,
// or numbers, rather than OCR debris like "P| as [ar |".
bool readsAsWords(const QString &text) {
    static const QRegularExpression word(R"re(^[("'“]?([A-Za-z][A-Za-z'’\-]*|\d+(\.\d+)*)[)"'”,:;.?!]*$)re");
    static const QRegularExpression vowel("[aeiouyAEIOUY]");
    const QStringList tokens = text.split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return false;
    int good = 0;
    for (const QString &t : tokens) {
        const auto m = word.match(t);
        good += m.hasMatch() && (m.captured(1)[0].isDigit() || m.captured(1).size() == 1
                                 || vowel.match(m.captured(1)).hasMatch());
    }
    return good >= tokens.size() * 0.7;
}
} // namespace

double headingNoise(const QStringList &headings) {
    int total = 0, noisy = 0;
    for (const QString &page : headings) {
        for (const QString &line : page.split('\n', Qt::SkipEmptyParts)) {
            ++total;
            noisy += !readsAsWords(line.section('\t', 1));
        }
    }
    return total == 0 ? 0 : double(noisy) / total;
}

QSet<QString> repeatedHeadings(const QStringList &headings) {
    QHash<QString, int> pagesWith;
    for (const QString &page : headings) {
        QSet<QString> seen;
        for (const QString &line : page.split('\n', Qt::SkipEmptyParts))
            seen.insert(normalized(line.section('\t', 1)));
        for (const QString &key : std::as_const(seen))
            ++pagesWith[key];
    }
    QSet<QString> out;
    for (auto it = pagesWith.cbegin(); it != pagesWith.cend(); ++it)
        if (it.value() >= 4)
            out.insert(it.key());
    return out;
}

QHash<QString, int> vocabulary(const QStringList &pages) {
    static const QRegularExpression word(R"([A-Za-z]{3,})");
    QHash<QString, int> out;
    for (const QString &page : pages)
        for (auto it = word.globalMatch(page); it.hasNext();)
            ++out[it.next().captured(0).toLower()];
    return out;
}

QVariantList headingEntries(const QStringList &headings, int first, int last, const QSet<QString> &repeated,
                            const QHash<QString, int> &vocabulary) {
    static const QRegularExpression wordPattern(R"([A-Za-z]{3,})");
    static const QRegularExpression debris(R"([|\[\]{}<>_~^*#@])");
    const bool strict = !vocabulary.isEmpty();
    // A table row or label read as a heading: it starts with a stray letter
    // ("P SOR"), or mixes a capitalised code with ordinary words ("SOR
    // Decreased contractile force"). Real headings are in capitals or in
    // title case throughout.
    auto tableRow = [](const QString &text) {
        const QStringList words = text.split(' ', Qt::SkipEmptyParts);
        if (words.isEmpty())
            return true;
        if (words[0].size() == 1 && words[0] != "A" && words[0] != "I" && !words[0][0].isDigit())
            return true;
        bool caps = false, lower = false;
        for (const QString &w : words) {
            if (letterCount(w) < 2)
                continue;
            if (w == w.toUpper())
                caps = true;
            else if (w[0].isLower() && w.size() > 3)
                lower = true;
        }
        return caps && lower;
    };
    // Strict: most longer words are ones the book uses at least three times.
    auto known = [&](const QString &text) {
        int words = 0, familiar = 0;
        for (auto it = wordPattern.globalMatch(text); it.hasNext();) {
            ++words;
            familiar += vocabulary.value(it.next().captured(0).toLower()) >= 3;
        }
        return words > 0 && familiar >= words * 0.85;
    };
    static const QRegularExpression furniture(
        R"(^([A-D][).]\s|\d+[.)]\s|\d+\s+[A-D]\b|(chapter|part)\s+\d+\s*$|figure\b|table\b|contents\b))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numbered(R"(^\d+(\.\d+)+\s)");
    QVariantList out;
    for (int page = std::max(0, first); page <= last && page < headings.size(); ++page) {
        int order = 0;
        for (const QString &line : headings[page].split('\n', Qt::SkipEmptyParts)) {
            const double ratio = line.section('\t', 0, 0).toDouble();
            const QString text = line.section('\t', 1).simplified();
            // Unnumbered lines need to stand out more than numbered ones.
            const bool isNumbered = numbered.match(text).hasMatch();
            const QStringList words = text.split(' ', Qt::SkipEmptyParts);
            // A single word with digits is a formula or a label (APO4).
            bool digits = false;
            for (const QChar c : text)
                digits |= c.isDigit();
            // Long unnumbered lines are headings only if written like one,
            // with most words capitalised; otherwise they are table cells.
            int capitalised = 0;
            for (const QString &w : words)
                capitalised += w[0].isUpper();
            const bool sentenceLike = !isNumbered && words.size() > 6 && capitalised * 2 < words.size();
            if (strict && (ratio < 1.2 || debris.match(text).hasMatch() || !known(text) || tableRow(text)))
                continue;
            if (text.size() < 3 || !readsAsWords(text) || furniture.match(text).hasMatch() || repeated.contains(normalized(text))
                || ratio < (isNumbered ? 1.05 : 1.1) || (!isNumbered && words.size() > 16) || sentenceLike
                || (words.size() == 1 && digits))
                continue;
            out << QVariantMap{{"label", text}, {"depth", isNumbered ? 1 : 2}, {"level", isNumbered ? 1 : 2},
                               {"target", page}, {"order", ++order}, {"ratio", ratio}};
        }
    }
    return out;
}

bool sameHeading(const QString &a, const QString &b) {
    static const QRegularExpression label(R"(^(chapter|part)\s*\d+\s*[:.\-–]?\s*)", QRegularExpression::CaseInsensitiveOption);
    QString x = a, y = b;
    x.remove(label);
    y.remove(label);
    // The same title, give or take OCR slips; one that merely contains the
    // other ("Freestanding Questions: The Circulatory System" and "The
    // Circulatory System") is a different heading.
    const QString nx = normalized(x), ny = normalized(y);
    return !nx.isEmpty() && !ny.isEmpty() && (nx == ny || similarity(nx, ny) >= 0.85);
}

QStringList contentsParts(const QString &contentsText, const QString &bookTitle) {
    static const QRegularExpression part(R"(^part\s+(\d{1,2})\s*[:.\-–]?\s*(.*[A-Za-z].*?)[\s.\d]*$)",
                                         QRegularExpression::CaseInsensitiveOption);
    QSet<QString> acronyms;
    collectAcronyms(bookTitle, acronyms, bookTitle != bookTitle.toUpper());
    QStringList out;
    for (const QString &line : nonEmptyLines(contentsText)) {
        const auto m = part.match(line);
        if (!m.hasMatch())
            continue;
        QString name = m.captured(2).trimmed();
        if (name == name.toUpper())
            name = titleCase(name, acronyms);
        const QString label = QStringLiteral("Part %1: %2").arg(m.captured(1), name);
        if (!out.contains(label))
            out << label;
    }
    return out;
}

QVariantList tidyEntries(const QVariantList &entries, const QStringList &heads, const QString &bookTitle,
                         const QStringList &parts, const QStringList &pages) {
    // Acronyms: all-caps words the book writes that way in mixed-case text.
    QSet<QString> acronyms;
    collectAcronyms(bookTitle, acronyms, bookTitle != bookTitle.toUpper());
    for (const QVariant &v : entries)
        collectAcronyms(v.toMap()["label"].toString(), acronyms);
    // Book-wide facts are worked out once per book, not for every batch:
    // the same page list (shared, unchanged) comes back each time.
    struct Memo { const void *key = nullptr; qsizetype size = -1; QSet<QString> acronyms; QStringList normalizedPages; };
    thread_local Memo memo;
    if (memo.key != static_cast<const void *>(pages.constData()) || memo.size != pages.size()) {
        memo.key = pages.constData();
        memo.size = pages.size();
        memo.acronyms = textAcronyms(pages);
        memo.normalizedPages.clear();
        for (const QString &page : pages)
            memo.normalizedPages << normalized(page);
    }
    acronyms.unite(memo.acronyms);

    static const QRegularExpression part(R"(^part\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression chapter(R"(^chapter\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numbered(R"(^(chapter\s+)(\d+)\b[\s:.\-–]*)",
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bare(R"(^((?:chapter|part)\s+\d+)\s+([^:.\s].*)$)",
                                         QRegularExpression::CaseInsensitiveOption);
    // Page furniture a model mistakes for headings: question numbers ("4. A
    // mixture…", unlike "3.1 Protein Structure"), captions, the contents.
    static const QRegularExpression question(R"(^\d+\.\s)");
    static const QRegularExpression caption(R"(^(figure|fig\.|table|contents|table of contents)\b)",
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sectionNumber(R"(^\d+(\.\d+)+\s*)");

    auto isSection = [](const QString &t) { return !part.match(t).hasMatch() && !chapter.match(t).hasMatch(); };
    // A section heading is printed on its page; a model sometimes describes
    // a page instead, which this catches.
    auto onPage = [&](const QString &title, int page) {
        if (page < 0 || page >= pages.size())
            return true;
        const QString &text = memo.normalizedPages[page];
        QString t = title;
        t.remove(sectionNumber);
        const QString key = normalized(t);
        return key.isEmpty() || text.contains(key);
    };

    QVariantList cleaned;
    for (const QVariant &v : entries) {
        QVariantMap e = v.toMap();
        QString title = e["label"].toString().simplified();
        // Strip running heads from the front, in any order, repeatedly.
        for (bool stripped = true; stripped;) {
            stripped = false;
            for (const QString &head : heads) {
                // At the start, or after a word or two of page furniture
                // ("Drill MCAT WORKOUT …").
                const qsizetype at = title.toUpper().indexOf(head);
                const qsizetype end = at + head.size();
                if (at >= 0 && at <= 12 && (at == 0 || !title[at - 1].isLetterOrNumber())
                    && (end == title.size() || !title[end].isLetterOrNumber())) {
                    title = title.mid(end);
                    while (!title.isEmpty() && !title[0].isLetterOrNumber())
                        title.remove(0, 1);
                    stripped = true;
                }
            }
        }
        // Dot leaders and stray bars from a contents line.
        static const QRegularExpression trailing(R"([\s.·…|_]{2,}$|\s*\|$)");
        title.remove(trailing);
        if (letterCount(title) < 3)
            continue;
        // A title naming a part from the printed contents is that part.
        for (const QString &p : parts) {
            const QString name = normalized(p.section(':', 1));
            const QString t = normalized(title);
            if (!name.isEmpty() && (t == name || t == normalized(p) || t.endsWith(name)) && t.size() <= name.size() + 8)
                title = p;
        }
        if (title == title.toUpper())
            title = titleCase(title, acronyms);
        // "Chapter 24 Thermodynamics" reads like its neighbours as
        // "Chapter 24: Thermodynamics".
        if (const auto m = bare.match(title); m.hasMatch())
            title = m.captured(1) + ": " + m.captured(2);
        if (isSection(title)) {
            const int words = int(title.split(' ', Qt::SkipEmptyParts).size());
            int nonSpace = 0;
            for (const QChar c : title)
                nonSpace += !c.isSpace();
            const bool formula = title.contains(QChar(0x2192)) || title.contains('=') || title.contains('+')
                              || title.contains(" > ") || title.contains(" < ")
                              || letterCount(title) < nonSpace * 0.6;
            const bool sentence = title[0].isLower() || title.endsWith('.') || title.contains('?') || words > 14;
            if (formula)
                continue;
            if (sentence || question.match(title).hasMatch() || caption.match(title).hasMatch()
                || !onPage(title, e["target"].toInt()))
                continue;
        }
        e["label"] = title;
        cleaned << e;
    }

    // Measured headings before the first chapter are front matter (credits,
    // copyright lines), not sections.
    {
        const auto firstChapter = std::find_if(cleaned.cbegin(), cleaned.cend(), [&](const QVariant &v) {
            return chapter.match(v.toMap()["label"].toString()).hasMatch();
        });
        if (firstChapter != cleaned.cend()) {
            const int start = firstChapter->toMap()["target"].toInt();
            cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(), [&](const QVariant &v) {
                const QVariantMap e = v.toMap();
                return e.contains("order") && e["target"].toInt() < start;
            }), cleaned.end());
        }
    }

    // OCR reads "Chapter 1" as "Chapter1l" and a model makes it 11; a number
    // larger than the next chapter's, holding the one expected, is that one.
    for (int i = 0; i < cleaned.size(); ++i) {
        QVariantMap e = cleaned[i].toMap();
        const auto m = numbered.match(e["label"].toString());
        if (!m.hasMatch())
            continue;
        for (int j = i + 1; j < cleaned.size(); ++j) {
            const auto next = numbered.match(cleaned[j].toMap()["label"].toString());
            if (!next.hasMatch())
                continue;
            const int n = m.captured(2).toInt(), following = next.captured(2).toInt();
            const QString expected = QString::number(following - 1);
            if (n > following && following > 1 && m.captured(2).contains(expected)) {
                QString label = e["label"].toString();
                label.replace(m.capturedStart(2), m.capturedLength(2), expected);
                e["label"] = label;
                cleaned[i] = e;
            }
            break;
        }
    }

    // Chapters come in order. The chapter numbers kept are the most
    // consistent rising run through the book (big jumps cost), so a stray
    // "Chapter 14" inside Chapter 4, a "Chapter 5: Summary", or entries
    // copied off the contents page become sections of the chapter they are
    // in, or go if nothing is left of them.
    {
        QList<int> index, number;
        for (int i = 0; i < cleaned.size(); ++i) {
            const auto m = numbered.match(cleaned[i].toMap()["label"].toString());
            if (m.hasMatch()) {
                index << i;
                number << m.captured(2).toInt();
            }
        }
        // Longest rising run first; among equally long ones, the one that
        // skips the fewest chapter numbers.
        const qsizetype n = number.size();
        QList<int> length(n, 1), skipped(n, 0), previous(n, -1);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < j; ++i) {
                if (number[i] >= number[j])
                    continue;
                const int l = length[i] + 1, k = skipped[i] + number[j] - number[i] - 1;
                if (l > length[j] || (l == length[j] && k < skipped[j])) {
                    length[j] = l;
                    skipped[j] = k;
                    previous[j] = i;
                }
            }
        }
        QSet<int> chain;
        int best = -1;
        for (int j = 0; j < n; ++j)
            if (best < 0 || length[j] > length[best] || (length[j] == length[best] && skipped[j] < skipped[best]))
                best = j;
        for (int j = best; j >= 0; j = previous[j])
            chain.insert(index[j]);
        // When a chapter number appears more than once, its title page is
        // where it starts; near-identical copies go, and the best-spelled
        // wording stays (OCR reads "Identity" as "Ildentity"). Different
        // titles under the same number ("Chapter 5: Summary") are left to
        // become sections below.
        auto rest = [&](const QString &t) { return normalized(t.mid(numbered.match(t).capturedLength())); };
        for (int j = 0; j < n; ++j) {
            if (!chain.contains(index[j]))
                continue;
            QList<int> same;   // positions in `index` with this number and a matching title
            for (int k = 0; k < n; ++k)
                if (number[k] == number[j]
                    && (k == j || similarity(rest(cleaned[index[k]].toMap()["label"].toString()),
                                             rest(cleaned[index[j]].toMap()["label"].toString())) >= 0.8))
                    same << k;
            if (same.size() < 2)
                continue;
            int at = j;
            for (int k : same)
                if (cleaned[index[k]].toMap().value("titlePage").toBool()) {
                    at = k;
                    break;
                }
            QString label = cleaned[index[at]].toMap()["label"].toString();
            for (int k : same)
                if (!cleaned[index[k]].toMap().value("titlePage").toBool()) {
                    label = cleaned[index[k]].toMap()["label"].toString();
                    break;
                }
            for (int k : same) {
                QVariantMap e = cleaned[index[k]].toMap();
                if (k == at) {
                    e["label"] = label;
                } else {
                    e["label"] = QString();   // dropped below
                    chain.remove(index[k]);
                }
                cleaned[index[k]] = e;
            }
            chain.insert(index[at]);
        }
        QVariantList kept;
        for (int i = 0; i < cleaned.size(); ++i) {
            QVariantMap e = cleaned[i].toMap();
            const QString t = e["label"].toString();
            if (t.isEmpty())
                continue;
            const auto m = numbered.match(t);
            if (m.hasMatch() && !chain.contains(i)) {
                const QString rest = t.mid(m.capturedLength()).trimmed();
                if (letterCount(rest) < 3 || !onPage(rest, e["target"].toInt()))
                    continue;
                e["label"] = rest;
            }
            kept << e;
        }
        cleaned = kept;
    }

    // A chapter listed where only its title appears (a contents page that
    // went unrecognised) moves to where its title shows again as a heading.
    {
        QVariantList kept;
        int chapterAt = -1;
        QString chapterTitle;
        for (const QVariant &v : std::as_const(cleaned)) {
            QVariantMap e = v.toMap();
            const QString t = e["label"].toString();
            if (const auto m = numbered.match(t); m.hasMatch()) {
                chapterAt = int(kept.size());
                // A chapter found on its own title page is already where it
                // starts; a heading repeating its title later stays a section.
                chapterTitle = e.value("titlePage").toBool() ? QString() : normalized(t.mid(m.capturedLength()));
            } else if (chapterAt >= 0 && !chapterTitle.isEmpty() && normalized(t) == chapterTitle) {
                QVariantMap c = kept[chapterAt].toMap();
                const int gap = e["target"].toInt() - c["target"].toInt();
                if (gap > 0 && gap <= 20) {
                    c["target"] = e["target"];
                    kept[chapterAt] = c;
                    chapterTitle.clear();
                    continue;
                }
            }
            kept << e;
        }
        cleaned = kept;
    }

    // A title repeated on the pages just after it is the same section
    // continuing; further on, it may be a new one ("Summary").
    {
        QHash<QString, int> lastPage;
        QVariantList kept;
        for (const QVariant &v : std::as_const(cleaned)) {
            const QVariantMap e = v.toMap();
            const QString key = normalized(e["label"].toString());
            const int page = e["target"].toInt();
            const bool repeat = lastPage.contains(key) && page - lastPage.value(key) <= 8;
            lastPage.insert(key, page);
            if (!repeat)
                kept << e;
        }
        cleaned = kept;
    }

    // Levels from the labels themselves, which models get more reliably
    // than the level they are asked for.
    bool hasParts = false, hasChapters = false;
    for (const QVariant &v : std::as_const(cleaned)) {
        const QString t = v.toMap()["label"].toString();
        hasParts |= part.match(t).hasMatch();
        hasChapters |= chapter.match(t).hasMatch();
    }
    if (!hasChapters)
        return cleaned;
    // Chapters sit inside parts only when every part was found; otherwise
    // some chapters would hang indented under nothing.
    bool allParts = hasParts && !parts.isEmpty();
    for (const QString &p : parts) {
        allParts = allParts && std::any_of(cleaned.cbegin(), cleaned.cend(),
                                           [&](const QVariant &v) { return v.toMap()["label"].toString() == p; });
    }
    const int chapterDepth = allParts ? 1 : 0;
    // Inside a chapter: numbered headings take their level from the number
    // ("3.1" a section, "3.1.2" a subsection); once a chapter has numbered
    // sections, unnumbered headings after one are its subsections, except
    // the chapter's closing matter (summary, practice) which are sections;
    // in chapters without numbers, the level the model gave is used.
    static const QRegularExpression numberedSection(R"(^(\d+)((?:\.\d+)+)\b)");
    static const QRegularExpression closing(
        R"(^(chapter\s+)?(summary|review|key terms|practice|freestanding|solutions?|answers?|questions|drill|exercises)\b|\b(practice passages?|practice questions|freestanding questions|solutions to|chapter summary|review questions)\b)",
        QRegularExpression::CaseInsensitiveOption);
    // Measured headings in chapters without numbered sections take their
    // level from their size: the largest in the chapter are sections, the
    // clearly smaller ones subsections.
    QHash<int, double> largest;   // chapter start index → largest heading ratio in it
    {
        int chapterAt = -1;
        for (int i = 0; i < cleaned.size(); ++i) {
            const QVariantMap e = cleaned[i].toMap();
            if (chapter.match(e["label"].toString()).hasMatch())
                chapterAt = i;
            else if (chapterAt >= 0 && e.contains("ratio"))
                largest[chapterAt] = std::max(largest.value(chapterAt), e["ratio"].toDouble());
        }
    }
    int chapterAt = -1;
    bool inChapter = false, numberedSeen = false;
    for (int index = 0; index < cleaned.size(); ++index) {
        QVariant &v = cleaned[index];
        QVariantMap e = v.toMap();
        const QString t = e["label"].toString();
        if (part.match(t).hasMatch()) {
            e["depth"] = 0;
            inChapter = false;
        } else if (chapter.match(t).hasMatch()) {
            e["depth"] = chapterDepth;
            inChapter = true;
            numberedSeen = false;
            chapterAt = index;
        } else if (!inChapter) {
            e["depth"] = 0;
        } else if (const auto m = numberedSection.match(t); m.hasMatch()) {
            const int levels = int(m.captured(2).count('.'));
            e["depth"] = chapterDepth + std::min(2, levels);
            numberedSeen = true;
        } else if (closing.match(t).hasMatch()) {
            e["depth"] = chapterDepth + 1;
            numberedSeen = false;
        } else if (numberedSeen) {
            e["depth"] = chapterDepth + 2;
        } else if (e.contains("ratio") && largest.value(chapterAt) > 0) {
            e["depth"] = chapterDepth + (e["ratio"].toDouble() >= largest.value(chapterAt) * 0.92 ? 1 : 2);
        } else {
            e["depth"] = chapterDepth + std::clamp(e.value("level", 1).toInt(), 1, 2);
        }
        v = e;
    }
    return cleaned;
}

namespace {
// Fixes OCR slips in a title with the book's own words: a word the book
// hardly uses is replaced by a common one a letter or two away with the
// same first letter ("Farm" → "Form", "Comic" → "Conic").
QString correctWords(const QString &title, const QHash<QString, int> &vocabulary) {
    static const QRegularExpression word(R"([A-Za-z]{3,})");
    QString out = title;
    qsizetype shift = 0;
    for (auto it = word.globalMatch(title); it.hasNext();) {
        const auto m = it.next();
        QString w = m.captured(0), lower = w.toLower();
        // A word the book uses often is right; a rarer one is fixed only
        // to a word used far more ("Caleulus" a few times, "calculus" often).
        const int own = vocabulary.value(lower);
        if (own >= 20)
            continue;
        // A stray capital stuck to a word: "TFasteners".
        if (w.size() > 4 && w[0].isUpper() && w[1].isUpper() && w[2].isLower() && vocabulary.value(lower.mid(1)) >= 4) {
            out.replace(m.capturedStart() + shift, 1, QString());
            shift -= 1;
            continue;
        }
        QString best;
        int bestCount = 0;
        for (auto v = vocabulary.cbegin(); v != vocabulary.cend(); ++v) {
            const QString &candidate = v.key();
            if (v.value() < std::max(8, own * 10) || candidate[0] != lower[0] || std::abs(candidate.size() - lower.size()) > 2
                || candidate.size() < 4)
                continue;
            if (similarity(candidate, lower) >= (lower.size() <= 5 ? 0.74 : 0.8) && v.value() > bestCount) {
                best = candidate;
                bestCount = v.value();
            }
        }
        if (best.isEmpty())
            continue;
        // Keep the original's capitals: Title, CAPS, or lower.
        QString fixed = best;
        if (w == w.toUpper())
            fixed = fixed.toUpper();
        else if (w[0].isUpper())
            fixed[0] = fixed[0].toUpper();
        out.replace(m.capturedStart() + shift, w.size(), fixed);
        shift += fixed.size() - w.size();
    }
    return out;
}
} // namespace

QVariantList printedOutline(const QStringList &pages) {
    // Page labels: "1-5" or "12 – 3" at the start or end of a header or
    // footer line (OCR reads the dash as ~, _, or an en dash). Diagnostic
    // and index pages ("DE I-1", "I-12") have labels of their own.
    static const QRegularExpression atStart(R"(^\s*(\d{1,2})\s*[-~–_]\s*(\d{1,3})\b)");
    static const QRegularExpression atEnd(R"(\b(\d{1,2})\s*[-~–_]\s*(\d{1,3})\s*$)");
    static const QRegularExpression diagnostic(R"(\bDE\s*([IVXL1]{1,5})\s*[-~–_.]\s*(\d{1,3}))");
    const int count = int(pages.size());
    QList<QStringList> lines;
    for (const QString &page : pages)
        lines << nonEmptyLines(page);
    const QHash<QString, int> words = vocabulary(pages);

    QHash<int, QHash<int, int>> votes;           // chapter → start index → votes
    QHash<int, QHash<QString, int>> headVotes;   // chapter → running-head title → votes
    QHash<QString, int> diagnosticStart;         // topic numeral → first page
    QHash<int, QList<QPair<int, int>>> labels;   // chapter → (printed page, index) seen
    QSet<int> diagnosticPages;                   // pages of the diagnostic exams
    int labelled = 0;
    for (int i = 0; i < count; ++i) {
        const QStringList &ls = lines[i];
        QStringList edges = ls.mid(0, 3) + ls.mid(std::max<qsizetype>(3, ls.size() - 3));
        for (const QString &line : std::as_const(edges)) {
            if (const auto d = diagnostic.match(line); d.hasMatch()) {
                diagnosticPages.insert(i);
                const QString topic = d.captured(1).replace('1', 'I');
                const int start = i - d.captured(2).toInt() + 1;
                if (start >= 0 && (!diagnosticStart.contains(topic) || start < diagnosticStart[topic]))
                    diagnosticStart[topic] = start;
                break;
            }
            auto m = atStart.match(line);
            bool front = true;
            if (!m.hasMatch()) {
                m = atEnd.match(line);
                front = false;
            }
            if (!m.hasMatch() || line.size() > 80)
                continue;
            const int chapter = m.captured(1).toInt(), page = m.captured(2).toInt();
            if (chapter < 1 || page < 1 || page > 200 || i - page + 1 < 0)
                continue;
            ++votes[chapter][i - page + 1];
            labels[chapter].append({page, i});
            ++labelled;
            QString beside = (front ? line.mid(m.capturedEnd()) : line.left(m.capturedStart())).simplified();
            if (letterCount(beside) >= 4 && beside == beside.toUpper() && readsAsWords(beside))
                ++headVotes[chapter][beside];
            break;
        }
    }
    if (labelled < count / 5 || votes.size() < 3)
        return {};
    // Where each chapter starts: from its lowest-numbered labelled page.
    // Scans often lose a page or two, so labels further in drift; the
    // opening pages are right. A chapter needs a couple of labels to count.
    // The earliest start that at least two pages agree on: stray labels
    // (an index line "pressure, 9-4") agree with nothing, and drifted ones
    // point later than the opening.
    QMap<int, int> starts;
    for (auto it = votes.cbegin(); it != votes.cend(); ++it) {
        int earliest = -1;
        for (auto v = it.value().cbegin(); v != it.value().cend(); ++v)
            if (v.value() >= 2 && (earliest < 0 || v.key() < earliest) && !diagnosticPages.contains(v.key()))
                earliest = v.key();
        if (earliest >= 0)
            starts.insert(it.key(), earliest);
        std::sort(labels[it.key()].begin(), labels[it.key()].end());
    }
    // Scans that lost a page shift a chapter's labels; its opening page is
    // the one nearby with the chapter's own contents list ("1. Logarithms
    // .... 2-1").
    {
        static const QRegularExpression listLine(R"(^[0-9LlI.]{1,3}[.,]?\s+[A-Z].*(\.\s?\.|\d\s*[-~–_]\s*\d))");
        auto listLines = [&](int page) {
            int n = 0;
            for (const QString &line : lines[page].mid(0, 25))
                n += listLine.match(line).hasMatch();
            return n;
        };
        QList<int> keys = starts.keys();
        for (int k = 0; k < keys.size(); ++k) {
            const int at = starts[keys[k]];
            const int lo = std::max(k > 0 ? starts[keys[k - 1]] + 1 : 0, at - 1);
            const int hi = std::min(k + 1 < keys.size() ? starts[keys[k + 1]] - 1 : count - 1, at + 1);
            int best = at, most = listLines(at);
            for (int page = lo; page <= hi; ++page)
                if (listLines(page) > most + 1) {
                    best = page;
                    most = listLines(page);
                }
            starts[keys[k]] = best;
        }
    }
    // A printed reference "C-P" as a page: through the nearest labelled
    // page of that chapter, so missing pages do not shift it.
    auto resolve = [&](int chapter, int page) {
        const auto list = labels.value(chapter);
        if (list.isEmpty())
            return starts.value(chapter, 0) + page - 1;
        int best = list.first().second + page - list.first().first;
        int distance = std::abs(list.first().first - page);
        for (const auto &[p, index] : list)
            if (std::abs(p - page) < distance) {
                distance = std::abs(p - page);
                best = index + page - p;
            }
        return best;
    };
    {
        QMap<int, int> ordered;
        int previous = -1;
        for (auto it = starts.cbegin(); it != starts.cend(); ++it)
            if (it.value() > previous) {
                ordered.insert(it.key(), it.value());
                previous = it.value();
            }
        starts = ordered;
    }
    if (starts.size() < 3)
        return {};
    QHash<QString, int> chaptersWith;
    for (auto it = headVotes.cbegin(); it != headVotes.cend(); ++it)
        for (auto h = it.value().cbegin(); h != it.value().cend(); ++h)
            ++chaptersWith[normalized(h.key())];
    // The book's own running head (on every left-hand page), however OCR
    // spells it: anything close to the most common one is the book, not a
    // chapter.
    QString bookHead;
    {
        int most = 0;
        for (auto it = chaptersWith.cbegin(); it != chaptersWith.cend(); ++it)
            if (it.value() > most) {
                most = it.value();
                bookHead = it.key();
            }
    }
    // Lines at the top of many pages are furniture (the book's title in
    // its running head), never a chapter's title.
    QHash<QString, int> topLines;
    for (const QStringList &ls : std::as_const(lines))
        for (const QString &line : ls.mid(0, 2))
            ++topLines[normalized(line).remove(QRegularExpression("[0-9]"))];
    auto isBookHead = [&](const QString &text) {
        const QString key = normalized(text).remove(QRegularExpression("[0-9]"));
        if (topLines.value(key) >= std::max(5, count / 10))
            return true;
        return !bookHead.isEmpty() && similarity(normalized(text), bookHead) >= 0.6;
    };

    // The book's contents: chapter titles (by the chapter their page
    // reference names, or in order after the last one known) and topics.
    static const QRegularExpression reference(R"([\s.…·,_]*(\d{1,2})\s*[-~–_]\s*1?\s*$)");
    static const QRegularExpression leaders(R"(\s*[.…·,_ ]{3,}.*$)");
    static const QRegularExpression topicLine(R"(^Topic\s+([IVXL1l][IVXLl1ifH]{0,5}|\d{1,2})\s*[:;.]\s*(.+?)[\s\\/‘'’|]*$)", QRegularExpression::CaseInsensitiveOption);
    // OCR's spellings of roman numerals: "Iil", "Vill", "Xif", "XHI".
    auto roman = [](QString n) {
        n = n.toUpper();
        n.replace('H', "II").replace('F', 'I').replace('1', 'I').replace('L', 'I');
        return n;
    };
    // Words the book uses elsewhere, to tell titles from OCR debris.
    auto familiar = [&](const QString &text) {
        static const QRegularExpression w(R"([A-Za-z]{3,})");
        int all = 0, known = 0;
        for (auto it = w.globalMatch(text); it.hasNext();) {
            ++all;
            known += words.value(it.next().captured(0).toLower()) >= 2;
        }
        // A lone word must be a common one: OCR debris ("oot") is rare.
        if (all == 1)
            return words.value(text.simplified().toLower().remove(QRegularExpression("[^a-z]"))) >= 5;
        return all > 0 && known * 4 >= all * 3;
    };
    QHash<int, QString> listedTitles;
    QStringList unplaced;                    // contents titles not tied to a chapter
    QList<QPair<QString, QString>> topics;   // numeral, name
    static const QRegularExpression romanReference(R"([\s.…·,_]+[ivxlcIVXLC]{1,8}\s*$)");
    static const QRegularExpression dotLeaders(R"([.·…]\s?[.·…]\s?[.·…])");
    // All contents pages read as one list, so numbering carries across them.
    // Everything before the first topic is front matter when there are topics.
    QList<QPair<QString, int>> items;   // title, chapter its reference names (0 if unreadable)
    {
        QString carried;
        bool inDiagnostic = false, sawTopic = false, topicOpen = false, diagnosticOpen = false;
        auto flush = [&]() {
            carried = correctWords(carried, words);
            carried.remove(QRegularExpression(R"(\s+[A-Za-z]$)"));
            if (!carried.isEmpty() && sawTopic && letterCount(carried) >= 3 && readsAsWords(carried) && familiar(carried))
                items.append({carried, 0});
            carried.clear();
        };
        for (int i : contentsPages(pages)) {
            for (const QString &raw : lines[i]) {
                const QString line = raw.simplified();
                if (const auto t = topicLine.match(line); t.hasMatch()) {
                    flush();
                    topics.append({roman(t.captured(1)), t.captured(2).simplified()});
                    if (!sawTopic)
                        items.clear();   // what came before the first topic was front matter
                    sawTopic = true;
                    inDiagnostic = false;
                    topicOpen = t.captured(2).trimmed().endsWith(',') || t.captured(2).contains(QRegularExpression(",[\\s\\/'‘’]*$"));
                    continue;
                }
                // The rest of a wrapped topic name ("and Controls").
                if (topicOpen && line[0].isLower()) {
                    topics.last().second += " " + line;
                    topicOpen = false;
                    continue;
                }
                topicOpen = false;
                if (line.startsWith("Diagnostic", Qt::CaseInsensitive)) {
                    flush();
                    // Its name may wrap; a line ending "and" or "," surely does.
                    inDiagnostic = !diagnostic.match(line).hasMatch();
                    diagnosticOpen = inDiagnostic && (line.endsWith(',') || line.endsWith(" and") || line.endsWith(" of"));
                    continue;
                }
                if (diagnosticOpen) {
                    diagnosticOpen = false;
                    inDiagnostic = false;
                    continue;
                }
                if (letterCount(line) < 3)
                    continue;
                // The rest of a wrapped diagnostic exam's name.
                if (inDiagnostic && (line[0].isLower() || diagnostic.match(line).hasMatch())) {
                    inDiagnostic = !diagnostic.match(line).hasMatch() && !dotLeaders.match(line).hasMatch();
                    continue;
                }
                inDiagnostic = false;
                if (line[0].isLower() && carried.isEmpty())
                    continue;
                const bool leadersHere = dotLeaders.match(line).hasMatch();
                const auto r = reference.match(line);
                QString title = line;
                title.remove(leaders);
                title.remove(reference);
                title = title.simplified();
                // A title wrapped onto two lines: no leaders and no reference yet.
                if (!r.hasMatch() && !leadersHere && title.size() < 60 && !title.endsWith('.') && line[0].isLower() == false
                    && title.endsWith(',') ) {
                    carried = carried.isEmpty() ? title : carried + " " + title;
                    continue;
                }
                if (!r.hasMatch() && !leadersHere && title.size() < 60 && !title.endsWith('.')) {
                    // A line on its own: a wrapped start ("Codes and References
                    // Used to Prepare") or a whole title with its leaders lost.
                    if (!carried.isEmpty())
                        title = carried + " " + title;
                    carried = title;
                    continue;
                }
                if (!carried.isEmpty()) {
                    // Joined when this line continues it; else both stand alone.
                    if (line[0].isLower() || carried.endsWith(',') || carried.endsWith(" and") || carried.endsWith(" of"))
                        title = carried + " " + title;
                    else
                        flush();
                }
                carried.clear();
                if (!sawTopic && romanReference.match(line).hasMatch() && !r.hasMatch())
                    continue;
                title.remove(QRegularExpression(R"([\s.…·,_]+$)"));
                title.remove(QRegularExpression(R"(\s+[A-Za-z]$)"));   // a stray letter from the leaders
                if (isBookHead(title))
                    continue;
                title = correctWords(title, words);
                if (letterCount(title) < 3 || !readsAsWords(title) || !familiar(title))
                    continue;
                items.append({title, r.hasMatch() ? r.captured(1).toInt() : 0});
            }
        }
        flush();
        if (qEnvironmentVariableIsSet("SHELF_DEBUG_CONTENTS"))
            for (const auto &item : std::as_const(items))
                qInfo().noquote() << "ITEM" << item.second << item.first;
    }
    {
        // Titles whose reference could not be read take the chapter numbers
        // between their neighbours, but only when they fit exactly; those
        // before the first reference count up to it.
        int previousIndex = -1, previousChapter = 0;
        // How many titles appear at the top of their chapter's opening page.
        auto onOpening = [&](int chapter, const QString &title) {
            if (!starts.contains(chapter))
                return false;
            const QString want = normalized(title);
            if (want.size() < 4)
                return false;
            for (const QString &line : lines[starts[chapter]].mid(0, 8)) {
                const QString have = normalized(line);
                if (have == want || similarity(have, want) >= 0.85)
                    return true;
            }
            return false;
        };
        // Titles between two known chapters go to the chapters between, in
        // order. When OCR lost some titles there are more chapters than
        // titles: each title goes where it is printed on the opening page,
        // found by aligning the two sequences (as diff does) for the most
        // such matches.
        auto fill = [&](int from, int to, int firstChapter, int lastChapter) {
            const int n = to - from - 1, g = lastChapter - firstChapter - 1;
            if (n <= 0 || n > g)
                return;
            auto item = [&](int i) { return items[from + i].first; };   // 1-based
            QList<QList<int>> f(n + 1, QList<int>(g + 1, -1000000));
            for (int c = 0; c <= g; ++c)
                f[0][c] = 0;
            for (int i = 1; i <= n; ++i)
                for (int c = i; c <= g; ++c)
                    f[i][c] = std::max(c > i ? f[i][c - 1] : -1000000,
                                       f[i - 1][c - 1] + (onOpening(firstChapter + c, item(i)) ? 1 : 0));
            for (int i = n, c = g; i > 0; --c) {
                const int take = f[i - 1][c - 1] + (onOpening(firstChapter + c, item(i)) ? 1 : 0);
                if (f[i][c] == take) {
                    listedTitles.insert(firstChapter + c, item(i));
                    --i;
                }
            }
        };
        for (int k = 0; k < items.size(); ++k) {
            const int chapter = items[k].second;
            if (chapter <= previousChapter || chapter > previousChapter + (k - previousIndex) + 2)
                continue;   // out of order, or a misread that skips too far
            listedTitles.insert(chapter, items[k].first);
            fill(previousIndex, k, previousChapter, chapter);
            previousIndex = k;
            previousChapter = chapter;
        }
        // After the last reference, titles carry on in order.
        for (int k = previousIndex + 1, ch = previousChapter + 1; k < items.size(); ++k, ++ch)
            if (!listedTitles.contains(ch))
                listedTitles.insert(ch, items[k].first);
        for (const auto &item : std::as_const(items))
            if (!std::any_of(listedTitles.cbegin(), listedTitles.cend(), [&](const QString &t) { return t == item.first; }))
                unplaced << item.first;
    }
    // Topics in order of their numerals, each starting after the last.
    {
        auto value = [](const QString &roman) {
            const QHash<QChar, int> v{{'I', 1}, {'V', 5}, {'X', 10}, {'L', 50}, {'C', 100}};
            int total = 0;
            for (int i = 0; i < roman.size(); ++i) {
                const int a = v.value(roman[i]), b = i + 1 < roman.size() ? v.value(roman[i + 1]) : 0;
                total += a < b ? -a : a;
            }
            return total;
        };
        std::stable_sort(topics.begin(), topics.end(), [&](const auto &a, const auto &b) { return value(a.first) < value(b.first); });
        int previous = -1;
        QList<QPair<QString, QString>> kept;
        for (const auto &t : std::as_const(topics))
            if (diagnosticStart.contains(t.first) && diagnosticStart[t.first] > previous) {
                kept.append(t);
                previous = diagnosticStart[t.first];
            }
        topics = kept;
    }

    static const QRegularExpression numberedHeading(R"(^([0-9LlI]{1,2})[.,]\s+([A-Z][A-Z0-9 ,:;()'’&/\-]{3,}?)(\s+[A-Za-z])?[\s\-'’]*$)");
    static const QRegularExpression contentsItem(R"(^([0-9LlI]{1,2})[.,]\s+(.+?)[\s.…·,_]*(\d{1,2}\s*[-~–_.]\s*\d{1,3}|\d{2,4})?\s*$)");
    static const QRegularExpression equation(R"(^Equations?\s+[\d.,]+.*?:\s*(.+)$)");
    static const QSet<QString> dangling{"of", "the", "and", "a", "an", "to", "in", "for", "on", "with", "or", "by"};
    auto number = [](const QString &n) {
        QString t = n;
        t.replace('L', '1').replace('l', '1').replace('I', '1');
        return t.toInt();
    };
    auto tidyName = [&](QString name) {
        name = name.simplified();
        if (name == name.toUpper())
            name = titleCase(name, {});
        return correctWords(name, words);
    };
    QVariantList out;
    // Topics (the book's parts) begin with their diagnostic exam.
    QList<QPair<int, QString>> parts;
    for (const auto &[numeral, name] : std::as_const(topics))
        if (diagnosticStart.contains(numeral))
            parts.append({diagnosticStart[numeral], QStringLiteral("Topic %1: %2").arg(numeral, tidyName(name))});
    // A listed chapter whose pages carry no readable labels: its opening
    // page is the first between its neighbours with its title at the top.
    for (auto it = listedTitles.cbegin(); it != listedTitles.cend(); ++it) {
        const int chapter = it.key();
        if (starts.contains(chapter) || chapter < 1)
            continue;
        auto next = starts.upperBound(chapter);
        if (next == starts.cbegin() || next == starts.cend())
            continue;
        const int from = std::prev(next).value() + 1, to = next.value();
        const QString want = normalized(it.value());
        for (int page = from; page < to && want.size() >= 4; ++page) {
            bool found = false;
            for (const QString &line : lines[page].mid(0, 6))
                if (similarity(normalized(line), want) >= 0.8 || normalized(line).startsWith(want)) {
                    found = true;
                    break;
                }
            if (found) {
                starts.insert(chapter, page);
                break;
            }
        }
    }
    const QList<int> chapters = starts.keys();
    auto partsUpTo = [&](int page) {
        while (!parts.isEmpty() && parts.first().first <= page) {
            const auto part = parts.takeFirst();
            out << QVariantMap{{"label", part.second}, {"depth", 0}, {"target", part.first}};
        }
    };
    for (int c = 0; c < chapters.size(); ++c) {
        const int chapter = chapters[c];
        const int first = starts[chapter];
        const int last = c + 1 < chapters.size() ? starts[chapters[c + 1]] - 1 : count - 1;
        partsUpTo(first);

        // Its title: from the book's contents, else its most-voted running
        // head (not the book's own, which every chapter shares), else the
        // opening page's first lines.
        QString title = listedTitles.value(chapter);
        if (title.isEmpty()) {
            int titleVotes = 0;
            for (auto h = headVotes[chapter].cbegin(); h != headVotes[chapter].cend(); ++h)
                if (h.value() > titleVotes && h.value() >= 2 && !isBookHead(h.key())
                    && chaptersWith.value(normalized(h.key())) < chapters.size() / 2) {
                    title = h.key();
                    titleVotes = h.value();
                }
        }
        // The opening page: title lines, then the chapter's contents list.
        QStringList opening;
        QList<QPair<int, QString>> listed;   // section number, title
        QHash<int, int> listedPage;          // section number → page, from its reference
        static const QRegularExpression itemStart(R"(^([0-9LlI]{1,2})[.,]\s+(\S.*)$)");
        static const QRegularExpression pageRef(R"((\d{1,2})\s*[-~–_]\s*(\d{1,3})\s*$)");
        auto addListed = [&](int n, QString text) {
            const auto ref = pageRef.match(text);
            if (ref.hasMatch() && ref.captured(1).toInt() == chapter) {
                listedPage[n] = resolve(chapter, ref.captured(2).toInt());
            } else if (const auto digits = QRegularExpression(R"((\d{2,4})\s*$)").match(text); digits.hasMatch()) {
                // The dash lost: "19" for 1-9, "34" for 3-4.
                const QString d = digits.captured(1), c = QString::number(chapter);
                if (d.startsWith(c) && d.size() > c.size() && d.mid(c.size()).toInt() >= 1 && d.mid(c.size()).toInt() <= 60)
                    listedPage[n] = resolve(chapter, d.mid(c.size()).toInt());
            }
            text.remove(leaders);
            text.remove(QRegularExpression(R"([\s.…·,_]*\d[\d\s\-~–_.]*$)"));
            text.remove(QRegularExpression(R"(\s+[A-Za-z]$)"));
            text.remove(QRegularExpression(R"([^A-Za-z0-9)]+$)"));
            text = correctWords(text.simplified(), words);
            // A name clipped at a column edge ("Nus", "Boili"): its last word
            // completed with the book's most common word that starts so.
            if (const QString tail = text.section(' ', -1).toLower(); tail.size() >= 3 && words.value(tail) < 3) {
                QString best;
                int most = 4;
                for (auto v = words.cbegin(); v != words.cend(); ++v)
                    if (v.key().size() > tail.size() && v.key().startsWith(tail) && v.value() > most) {
                        best = v.key();
                        most = v.value();
                    }
                if (!best.isEmpty()) {
                    best[0] = text.section(' ', -1)[0].isUpper() ? best[0].toUpper() : best[0];
                    text = text.section(' ', 0, -2).isEmpty() ? best : text.section(' ', 0, -2) + " " + best;
                }
            }
            if (letterCount(text) >= 3 && (listed.isEmpty() || n > listed.last().first))
                listed.append({n, text});
        };
        {
            const QStringList &ls = lines[first];
            int pendingNumber = -1;
            QString pending;
            for (int k = 0; k < ls.size(); ++k) {
                const QString line = ls[k].simplified();
                const bool hasLeaders = dotLeaders.match(line).hasMatch() || pageRef.match(line).hasMatch();
                if (const auto m = itemStart.match(line); m.hasMatch() && letterCount(m.captured(2)) >= 3) {
                    if (pendingNumber >= 0)
                        addListed(pendingNumber, pending);
                    pendingNumber = number(m.captured(1));
                    pending = m.captured(2);
                    // An item without leaders may wrap onto the next line.
                    if (hasLeaders || line.contains("..")) {
                        addListed(pendingNumber, pending);
                        pendingNumber = -1;
                    }
                    continue;
                }
                // An item whose number OCR lost (". Derivatives .... 3-1"):
                // the next number, when the line has leaders and real words.
                if (pendingNumber < 0 && hasLeaders && line.size() < 70 && (!listed.isEmpty() || k < 4)) {
                    QString text = line;
                    text.remove(QRegularExpression(R"(^[.,·\s]+)"));
                    QString name = text;
                    name.remove(leaders);
                    if (letterCount(name) >= 3 && name[0].isUpper() && familiar(name.simplified())) {
                        addListed(listed.isEmpty() ? 1 : listed.last().first + 1, text);
                        continue;
                    }
                }
                if (pendingNumber >= 0 && hasLeaders && line.size() < 70) {
                    addListed(pendingNumber, pending + " " + line);   // the rest of a wrapped item
                    pendingNumber = -1;
                    continue;
                }
                if (pendingNumber >= 0) {
                    addListed(pendingNumber, pending);
                    pendingNumber = -1;
                }
                if (!listed.isEmpty()) {
                    // Debris between the list and the text is skipped; a
                    // sentence means the text has begun.
                    if (line.split(' ', Qt::SkipEmptyParts).size() >= 5)
                        break;
                    continue;
                }
                if (line.startsWith("Nomenclature") || line.startsWith("Description") || line.startsWith("Introduction"))
                    break;   // the chapter's text has begun
                if (opening.size() < 2 && readsAsWords(line) && !line.contains("www") && !isBookHead(line)
                    && letterCount(line) * 10 >= line.size() * 8 && line[0].isUpper() && !line.endsWith('.')
                    && line.split(' ', Qt::SkipEmptyParts).size() <= 7 && familiar(line))
                    opening << line;
            }
            if (pendingNumber >= 0)
                addListed(pendingNumber, pending);
        }
        // A contents title not tied to a chapter by its reference, printed
        // at the top of this chapter's opening page, is this chapter's.
        if (title.isEmpty()) {
            const QString top = normalized(lines[first].mid(0, 8).join(' '));
            for (int u = 0; u < unplaced.size(); ++u) {
                if (normalized(unplaced[u]).size() >= 5 && top.contains(normalized(unplaced[u]))) {
                    title = unplaced.takeAt(u);
                    break;
                }
            }
        }
        // The opening's first line, and the next only when the first plainly
        // runs on ("Heating, Ventilating, and").
        if (opening.size() > 1 && !(opening[0].endsWith(',') || opening[0].endsWith(" and") || opening[0].endsWith(" of")))
            opening = opening.mid(0, 1);
        if (title.isEmpty() && !opening.isEmpty() && !isBookHead(opening.join(' ')))
            title = opening.join(' ');
        if (!title.isEmpty())
            title = tidyName(title);
        if (isBookHead(title))
            title.clear();
        out << QVariantMap{{"label", title.isEmpty() ? QStringLiteral("Chapter %1").arg(chapter)
                                                     : QStringLiteral("Chapter %1: %2").arg(chapter).arg(title)},
                           {"depth", 0}, {"target", first}};

        // Sections: numbered headings in the text, in order, named after
        // the chapter's own contents list when one matches (by number with
        // a similar or cut-off name, since OCR clips headings at column
        // edges). Every listed section is required: one not found in the
        // text goes where its page reference points, else just after the
        // section before it.
        QVariantList sections;
        QHash<int, QPair<int, double>> placed;   // section number → (page, order)
        int lastNumber = 0, lastPage = first;
        const int maxListed = listed.isEmpty() ? 0 : listed.last().first;
        auto listedName = [&](int n) {
            for (const auto &[ln, lt] : std::as_const(listed))
                if (ln == n)
                    return lt;
            return QString();
        };
        for (int page = first; page <= last; ++page) {
            const QStringList &ls = lines[page];
            for (int k = 0; k < ls.size(); ++k) {
                const QString &line = ls[k];
                if (const auto m = numberedHeading.match(line); m.hasMatch()) {
                    int n = number(m.captured(1));
                    QString name = m.captured(2).simplified();
                    if (k + 1 < ls.size() && ls[k + 1] == ls[k + 1].toUpper() && letterCount(ls[k + 1]) >= 3
                        && ls[k + 1].size() < 40 && !numberedHeading.match(ls[k + 1]).hasMatch())
                        name += " " + ls[k + 1].simplified();
                    const QString have = normalized(name);
                    // The listed section it is: same number and a like or
                    // clipped name, or failing that the best-matching name.
                    bool named = false;
                    if (const QString same = listedName(n); !same.isEmpty()) {
                        const QString want = normalized(same);
                        // The list's name cut off at a column edge: the
                        // heading has it whole.
                        if (want.size() >= 3 && have.startsWith(want) && have.size() > want.size())
                            named = true;
                        else if (similarity(want, have) >= 0.45 || (have.size() >= 3 && want.startsWith(have.left(std::min<qsizetype>(have.size(), 8))))) {
                            name = same;
                            named = true;
                        }
                    }
                    if (!named) {
                        double best = 0;
                        for (const auto &[listedNumber, listedTitle] : std::as_const(listed)) {
                            const double sim = similarity(normalized(listedTitle), have);
                            if (sim > best && sim >= 0.6) {
                                best = sim;
                                n = listedNumber;
                                name = listedTitle;
                                named = true;
                            }
                        }
                    }
                    const int reach = std::max(lastNumber + 6, maxListed);
                    if (n <= lastNumber || n > reach || page < lastPage || letterCount(name) < 3 || placed.contains(n))
                        continue;
                    // Unlisted headings in a chapter with a list are suspect.
                    if (!named && listed.size() >= 3 && !familiar(name))
                        continue;
                    sections << QVariantMap{{"label", QStringLiteral("%1.%2 %3").arg(chapter).arg(n).arg(tidyName(name))},
                                            {"depth", 1}, {"target", page}, {"n", n}, {"order", double(k)}};
                    placed.insert(n, {page, double(k)});
                    lastNumber = n;
                    lastPage = page;
                } else if (const auto e = equation.match(line); e.hasMatch() && lastNumber > 0) {
                    QString name = e.captured(1).simplified();
                    if (k + 1 < ls.size() && !name.endsWith('.') && ls[k + 1].size() < 45 && ls[k + 1][0].isUpper()
                        && !ls[k + 1].startsWith("Description") && !ls[k + 1].startsWith("Variation")
                        && readsAsWords(ls[k + 1]))
                        name += " " + ls[k + 1].simplified();
                    name.remove(QRegularExpression(R"([^A-Za-z0-9)\]]+$)"));
                    name = correctWords(name, words);
                    const QString lastWord = name.section(' ', -1).toLower();
                    // Real subsection names: two or more known words, no
                    // stray symbols or numbers from the equations around them.
                    if (letterCount(name) < 6 || !readsAsWords(name) || dangling.contains(lastWord)
                        || name.split(' ', Qt::SkipEmptyParts).size() < 2 || name.contains(QRegularExpression(R"([;§\[\]"{}|=]|\d)"))
                        || !name[0].isUpper() || !familiar(name))
                        continue;
                    sections << QVariantMap{{"label", tidyName(name)}, {"depth", 2}, {"target", page}, {"order", double(k)}};
                }
            }
        }
        for (const auto &[n, name] : std::as_const(listed)) {
            if (placed.contains(n))
                continue;
            int page = -1;
            double order = -1;
            if (listedPage.contains(n) && listedPage[n] >= first && listedPage[n] <= last) {
                page = listedPage[n];
            } else {
                // Just after the nearest earlier section placed, else at the
                // chapter's start.
                page = first;
                for (int before = n - 1; before >= 1; --before)
                    if (placed.contains(before)) {
                        page = placed[before].first;
                        order = placed[before].second + 0.5;
                        break;
                    }
            }
            sections << QVariantMap{{"label", QStringLiteral("%1.%2 %3").arg(chapter).arg(n).arg(tidyName(name))},
                                    {"depth", 1}, {"target", page}, {"n", n}, {"order", order}};
            placed.insert(n, {page, order});
        }
        // Sections keep their numbered order: one whose page reference was
        // misread to before the section ahead of it goes just after that one.
        {
            QList<int> numbers = placed.keys();
            std::sort(numbers.begin(), numbers.end());
            QPair<int, double> previous{first, -2};
            for (int n : std::as_const(numbers)) {
                auto &[page, order] = placed[n];
                if (page < previous.first || (page == previous.first && order < previous.second)) {
                    page = previous.first;
                    order = previous.second + 0.25;
                    for (QVariant &v : sections) {
                        QVariantMap e = v.toMap();
                        if (e["depth"].toInt() == 1 && e["n"].toInt() == n) {
                            e["target"] = page;
                            e["order"] = order;
                            v = e;
                        }
                    }
                }
                previous = {page, order};
            }
        }
        // In page order, and on a page in reading order.
        std::stable_sort(sections.begin(), sections.end(), [](const QVariant &a, const QVariant &b) {
            const QVariantMap x = a.toMap(), y = b.toMap();
            if (x["target"].toInt() != y["target"].toInt())
                return x["target"].toInt() < y["target"].toInt();
            return x["order"].toDouble() < y["order"].toDouble();
        });
        for (QVariant &v : sections) {
            QVariantMap e = v.toMap();
            e.remove("n");
            e.remove("order");
            v = e;
        }
        out += sections;
    }
    partsUpTo(count);
    return out;
}

} // namespace BookText
