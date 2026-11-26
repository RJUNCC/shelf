#include "cards.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 minute = 60 * 1000;
constexpr qint64 day = 24 * 60 * minute;

QString clean(QString text) {
    text.remove(QStringLiteral("**"));
    return text.simplified();
}
} // namespace

Cards::Cards(const QString &file, QObject *parent)
    : QObject(parent),
      m_file(!file.isEmpty() ? file
                             : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                   + "/shelf/cards.json") {
    load();
}

qint64 Cards::now() const {
    return m_now ? m_now : QDateTime::currentMSecsSinceEpoch();
}

int Cards::indexOf(const QString &id) const {
    for (int i = 0; i < m_cards.size(); ++i)
        if (m_cards[i].value("id").toString() == id)
            return i;
    return -1;
}

int Cards::dueCount() const {
    const qint64 t = now();
    return int(std::count_if(m_cards.cbegin(), m_cards.cend(),
                             [t](const QVariantMap &c) { return c.value("due").toLongLong() <= t; }));
}

QVariantList Cards::parseQuiz(const QString &text) {
    // "**Q:** …", "Q: …", "1. **Q:** …", and the same for answers.
    static const QRegularExpression question(R"(^\s*(?:\d+[.)]\s*)?(?:[-*]\s*)?\**\s*Q\s*[:.]\s*\**\s*:?\s*(.*)$)",
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression answer(R"(^\s*(?:[-*]\s*)?\**\s*A\s*[:.]\s*\**\s*:?\s*(.*)$)",
                                           QRegularExpression::CaseInsensitiveOption);
    QVariantList out;
    QString front, back;
    bool inAnswer = false;
    auto finish = [&]() {
        if (!clean(front).isEmpty() && !clean(back).isEmpty())
            out << QVariantMap{{"front", clean(front)}, {"back", clean(back)}};
        front.clear();
        back.clear();
        inAnswer = false;
    };
    for (const QString &line : text.split('\n')) {
        if (const auto q = question.match(line); q.hasMatch()) {
            finish();
            front = q.captured(1);
        } else if (const auto a = answer.match(line); a.hasMatch() && !front.isEmpty()) {
            back = a.captured(1);
            inAnswer = true;
        } else if (line.trimmed().isEmpty()) {
            if (inAnswer)
                finish();
        } else if (inAnswer) {
            back += " " + line;
        } else if (!front.isEmpty()) {
            front += " " + line;   // a question over two lines
        }
    }
    finish();
    return out;
}

QString Cards::add(const QString &bookId, const QString &front, const QString &back, const QString &location) {
    if (front.trimmed().isEmpty() || back.trimmed().isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const qint64 t = now();
    m_cards.append({{"id", id}, {"bookId", bookId}, {"front", front.trimmed()}, {"back", back.trimmed()},
                    {"location", location}, {"created", t}, {"due", t}, {"interval", 0.0}, {"ease", 2.5},
                    {"reps", 0}, {"lapses", 0}});
    save();
    return id;
}

int Cards::addFromQuiz(const QString &bookId, const QString &text, const QString &location) {
    int added = 0;
    for (const QVariant &v : parseQuiz(text)) {
        const QString front = v.toMap().value("front").toString();
        const bool known = std::any_of(m_cards.cbegin(), m_cards.cend(), [&](const QVariantMap &c) {
            return c.value("bookId").toString() == bookId
                && c.value("front").toString().compare(front, Qt::CaseInsensitive) == 0;
        });
        if (known)
            continue;
        const qint64 t = now();
        m_cards.append({{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).left(12)}, {"bookId", bookId},
                        {"front", front}, {"back", v.toMap().value("back").toString()}, {"location", location},
                        {"created", t}, {"due", t}, {"interval", 0.0}, {"ease", 2.5}, {"reps", 0}, {"lapses", 0}});
        ++added;
    }
    if (added)
        save();
    return added;
}

void Cards::update(const QString &id, const QString &front, const QString &back) {
    const int i = indexOf(id);
    if (i < 0 || front.trimmed().isEmpty() || back.trimmed().isEmpty())
        return;
    m_cards[i]["front"] = front.trimmed();
    m_cards[i]["back"] = back.trimmed();
    save();
}

void Cards::remove(const QString &id) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_cards.removeAt(i);
    save();
}

QVariantList Cards::due(const QString &bookId, int limit) const {
    const qint64 t = now();
    QList<QVariantMap> found;
    for (const QVariantMap &c : m_cards)
        if (c.value("due").toLongLong() <= t && (bookId.isEmpty() || c.value("bookId").toString() == bookId))
            found << c;
    std::stable_sort(found.begin(), found.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("due").toLongLong() < b.value("due").toLongLong();
    });
    QVariantList out;
    for (int i = 0; i < found.size() && i < limit; ++i)
        out << found[i];
    return out;
}

QVariantList Cards::list(const QString &bookId) const {
    QVariantList out;
    for (const QVariantMap &c : m_cards)
        if (bookId.isEmpty() || c.value("bookId").toString() == bookId)
            out << c;
    return out;
}

int Cards::count(const QString &bookId) const {
    return int(std::count_if(m_cards.cbegin(), m_cards.cend(),
                             [&](const QVariantMap &c) { return c.value("bookId").toString() == bookId; }));
}

Cards::Schedule Cards::next(const QVariantMap &card, int grade) const {
    Schedule s{card.value("interval").toDouble(), card.value("ease", 2.5).toDouble(), card.value("reps").toInt(),
               card.value("lapses").toInt(), 0};
    const qint64 t = now();
    if (grade <= Again) {
        // Forgotten: back in ten minutes, and it grows more slowly from now on.
        s.reps = 0;
        ++s.lapses;
        s.ease = std::max(1.3, s.ease - 0.2);
        s.interval = 0;
        s.due = t + 10 * minute;
        return s;
    }
    if (s.reps == 0) {
        // A new or relearned card: a day, or four if it was easy.
        s.interval = grade == Easy ? 4 : 1;
    } else if (s.reps == 1 && grade != Hard) {
        s.interval = grade == Easy ? std::max(6.0, s.interval * s.ease * 1.3) : 6;
    } else if (grade == Hard) {
        s.interval = std::max(s.interval + 1, s.interval * 1.2);
    } else if (grade == Good) {
        s.interval = std::max(s.interval + 1, s.interval * s.ease);
    } else {
        s.interval = std::max(s.interval + 1, s.interval * s.ease * 1.3);
    }
    if (grade == Hard)
        s.ease = std::max(1.3, s.ease - 0.15);
    else if (grade == Easy)
        s.ease += 0.15;
    s.interval = std::min(36500.0, std::round(s.interval));
    ++s.reps;
    s.due = t + qint64(s.interval * day);
    return s;
}

void Cards::grade(const QString &id, int grade) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    const Schedule s = next(m_cards[i], grade);
    m_cards[i]["interval"] = s.interval;
    m_cards[i]["ease"] = s.ease;
    m_cards[i]["reps"] = s.reps;
    m_cards[i]["lapses"] = s.lapses;
    m_cards[i]["due"] = s.due;
    m_cards[i]["reviewed"] = now();
    save();
}

QString Cards::preview(const QString &id, int grade) const {
    const int i = indexOf(id);
    if (i < 0)
        return {};
    const qint64 gap = next(m_cards[i], grade).due - now();
    if (gap < 60 * minute)
        return tr("%1 min").arg(gap / minute);
    if (gap < day)
        return tr("%1 h").arg(gap / (60 * minute));
    const qint64 days = (gap + day / 2) / day;
    if (days < 31)
        return tr("%1 d").arg(days);
    if (days < 365)
        return tr("%1 mo").arg(QString::number(days / 30.0, 'f', 1));
    return tr("%1 y").arg(QString::number(days / 365.0, 'f', 1));
}

void Cards::load() {
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).object().value("cards").toArray())
        m_cards << v.toObject().toVariantMap();
}

void Cards::save() {
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QJsonArray list;
    for (const QVariantMap &c : std::as_const(m_cards))
        list << QJsonObject::fromVariantMap(c);
    QSaveFile file(m_file);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", list}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
    ++m_revision;
    emit changed();
}

namespace {

// LaTeX as Anki's MathJax wants it: $$…$$ → \[…\], $…$ → \(…\).
QString ankiMath(QString text) {
    static const QRegularExpression display(QStringLiteral(R"(\$\$(.+?)\$\$)"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression inlineMath(QStringLiteral(R"((?<![\\$])\$(?!\s)([^$\n]+?)(?<!\s)\$)"));
    text.replace(display, QStringLiteral(R"(\[\1\])"));
    text.replace(inlineMath, QStringLiteral(R"(\(\1\))"));
    return text;
}

QString ankiField(const QString &text) {
    QString out = ankiMath(text.trimmed()).toHtmlEscaped();
    out.replace('\t', ' ');
    out.replace('\n', QStringLiteral("<br>"));
    return out;
}

QString plainField(const QString &text) {
    QString out = text.trimmed();
    out.replace('\t', ' ');
    out.replace(QRegularExpression(QStringLiteral(R"(\s*\n\s*)")), QStringLiteral(" / "));
    return out;
}

} // namespace

QString Cards::exportText(const QVariantList &cards, const QString &format, const QVariantMap &bookTitles) {
    QString out;
    const bool anki = format == "anki";
    if (anki)
        out += QStringLiteral("#separator:tab\n#html:true\n#notetype:Basic\n#deck column:3\n");
    for (const QVariant &v : cards) {
        const QVariantMap card = v.toMap();
        if (anki) {
            QString title = bookTitles.value(card.value("bookId").toString()).toString().simplified();
            title.replace("::", ":");
            title.replace('\t', ' ');
            out += ankiField(card.value("front").toString()) + '\t' + ankiField(card.value("back").toString()) + '\t'
                   + (title.isEmpty() ? QStringLiteral("Shelf") : QStringLiteral("Shelf::") + title) + '\n';
        } else {
            out += plainField(card.value("front").toString()) + '\t' + plainField(card.value("back").toString()) + '\n';
        }
    }
    return out;
}
