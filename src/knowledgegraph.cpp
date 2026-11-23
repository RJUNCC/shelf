#include "knowledgegraph.h"

#include "annotations.h"
#include "ollama.h"

#include <QCryptographicHash>
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

namespace {
constexpr int batchSize = 10;

QString shortLabel(const QString &text, int length = 60) {
    const QString s = text.simplified();
    return s.size() <= length ? s : s.left(length - 1).trimmed() + QStringLiteral("…");
}
} // namespace

KnowledgeGraph::KnowledgeGraph(Annotations *annotations, Ollama *ollama, const QString &file, QObject *parent)
    : QObject(parent), m_annotations(annotations), m_ollama(ollama),
      m_file(file.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf/concepts.json"
                            : file) {
    load();
}

QStringList KnowledgeGraph::wikiLinks(const QString &text) {
    QStringList out;
    static const QRegularExpression link(QStringLiteral("\\[\\[([^\\[\\]|]+)(?:\\|[^\\]]*)?\\]\\]"));
    for (auto it = link.globalMatch(text); it.hasNext();) {
        const QString name = it.next().captured(1).simplified();
        if (!name.isEmpty() && !out.contains(name, Qt::CaseInsensitive))
            out << name;
    }
    return out;
}

QString KnowledgeGraph::conceptKey(const QString &name) {
    return name.simplified().toLower();
}

QString KnowledgeGraph::itemKey(const QString &bookId, const QVariantMap &a) {
    // Changes to the passage or note mean its concepts are asked for again.
    const QByteArray content = (a.value("text").toString() + "\n" + a.value("note").toString()).toUtf8();
    return bookId + "/" + a.value("id").toString() + "/"
         + QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex().left(12);
}

QVariantMap KnowledgeGraph::graph(const QVariantList &books) const {
    QVariantList nodes, links;
    QSet<QString> seen;
    QHash<QString, QString> conceptLabel;              // key → label as first written
    QHash<QString, QStringList> conceptItems;          // key → item node ids
    QHash<QString, QSet<QString>> conceptBooks;
    QHash<QString, bool> conceptTyped;                 // came from a [[link]]
    QList<QPair<QString, QString>> itemConcepts;       // (item node, concept key)

    for (const QVariant &v : books) {
        const QVariantMap book = v.toMap();
        const QString bookId = book.value("id").toString();
        const QVariantList annotations = m_annotations->list(bookId);
        bool any = false;
        for (const QVariant &av : annotations) {
            const QVariantMap a = av.toMap();
            const QString kind = a.value("kind").toString();
            if (kind != "highlight" && kind != "note")
                continue;
            if (!any) {
                nodes << QVariantMap{{"id", "b:" + bookId}, {"kind", "book"}, {"label", book.value("title")},
                                     {"bookId", bookId}};
                any = true;
            }
            QString parent = "b:" + bookId;
            const QString chapter = a.value("chapter").toString().simplified();
            if (!chapter.isEmpty()) {
                const QString chapterId = "c:" + bookId + ":" + chapter;
                if (!seen.contains(chapterId)) {
                    seen.insert(chapterId);
                    nodes << QVariantMap{{"id", chapterId}, {"kind", "chapter"}, {"label", chapter}, {"bookId", bookId}};
                    links << QVariantMap{{"source", "b:" + bookId}, {"target", chapterId}};
                }
                parent = chapterId;
            }
            const QString itemId = "a:" + bookId + ":" + a.value("id").toString();
            const QString text = a.value("text").toString(), note = a.value("note").toString();
            nodes << QVariantMap{{"id", itemId}, {"kind", text.isEmpty() ? "note" : "highlight"},
                                 {"label", shortLabel(text.isEmpty() ? note : text)}, {"text", text}, {"note", note},
                                 {"color", a.value("color")}, {"bookId", bookId}, {"location", a.value("location")},
                                 {"format", book.value("format")}, {"bookTitle", book.value("title")},
                                 {"chapter", chapter}};
            links << QVariantMap{{"source", parent}, {"target", itemId}};

            QStringList names = wikiLinks(note);
            for (const QString &n : names)
                conceptTyped[conceptKey(n)] = true;
            names += m_concepts.value(itemKey(bookId, a));
            QSet<QString> mine;
            for (const QString &n : names) {
                const QString key = conceptKey(n);
                if (key.isEmpty() || mine.contains(key))
                    continue;
                mine.insert(key);
                if (!conceptLabel.contains(key))
                    conceptLabel[key] = n.simplified();
                itemConcepts << qMakePair(itemId, key);
            }
        }
    }
    // "enzymes" joins "enzyme" when both are there.
    QHash<QString, QString> merged;
    for (auto it = conceptLabel.cbegin(); it != conceptLabel.cend(); ++it) {
        const QString &k = it.key();
        if (k.endsWith('s') && !k.endsWith("ss") && conceptLabel.contains(k.chopped(1)))
            merged[k] = k.chopped(1);
        else if (k.endsWith("ies") && conceptLabel.contains(k.chopped(3) + "y"))
            merged[k] = k.chopped(3) + "y";
    }
    for (auto &pair : itemConcepts) {
        pair.second = merged.value(pair.second, pair.second);
        if (!conceptItems[pair.second].contains(pair.first))
            conceptItems[pair.second] << pair.first;
        conceptBooks[pair.second].insert(pair.first.section(':', 1, 1));
    }
    for (auto it = conceptItems.cbegin(); it != conceptItems.cend(); ++it) {
        const QString conceptId = "k:" + it.key();
        nodes << QVariantMap{{"id", conceptId}, {"kind", "concept"}, {"label", conceptLabel.value(it.key())},
                             {"uses", int(it.value().size())}, {"books", int(conceptBooks.value(it.key()).size())},
                             {"typed", conceptTyped.value(it.key(), false)}};
        for (const QString &item : it.value())
            links << QVariantMap{{"source", item}, {"target", conceptId}, {"concept", true}};
    }
    return {{"nodes", nodes}, {"links", links}};
}

QList<KnowledgeGraph::Item> KnowledgeGraph::missing(const QVariantList &books) const {
    QList<Item> out;
    for (const QVariant &v : books) {
        const QString bookId = v.toMap().value("id").toString();
        for (const QVariant &av : m_annotations->list(bookId)) {
            const QVariantMap a = av.toMap();
            const QString kind = a.value("kind").toString();
            if (kind != "highlight" && kind != "note")
                continue;
            const QString key = itemKey(bookId, a);
            const QString text = (a.value("text").toString() + "\n" + a.value("note").toString()).simplified();
            if (!m_concepts.contains(key) && text.size() >= 3)
                out << Item{key, text.left(1200)};
        }
    }
    return out;
}

int KnowledgeGraph::pending(const QVariantList &books) const {
    return int(missing(books).size());
}

void KnowledgeGraph::findConcepts(const QVariantList &books) {
    if (m_busy)
        return;
    m_queue = missing(books);
    m_error.clear();
    m_done = 0;
    m_total = int(m_queue.size());
    if (m_queue.isEmpty() || !m_ollama) {
        emit progressChanged();
        return;
    }
    m_busy = true;
    ++m_generation;
    emit progressChanged();
    askNext();
}

void KnowledgeGraph::cancel() {
    ++m_generation;
    m_queue.clear();
    m_busy = false;
    emit progressChanged();
}

void KnowledgeGraph::askNext() {
    if (m_queue.isEmpty()) {
        m_busy = false;
        emit progressChanged();
        return;
    }
    const QList<Item> batch = m_queue.mid(0, batchSize);
    // Concepts already in use, so the model reuses their wording and the
    // same idea in two books becomes one node.
    QHash<QString, int> uses;
    for (const QStringList &names : std::as_const(m_concepts))
        for (const QString &n : names)
            uses[n] += 1;
    QStringList known = uses.keys();
    std::sort(known.begin(), known.end(), [&](const QString &a, const QString &b) { return uses[a] > uses[b]; });
    known = known.mid(0, 150);

    QString passages;
    for (int i = 0; i < batch.size(); ++i)
        passages += QStringLiteral("[%1] %2\n\n").arg(i + 1).arg(batch[i].text);
    const QString prompt = QStringLiteral(
        "Here are passages a student highlighted or notes they wrote, numbered.\n\n%1"
        "For each one, list the one to four key concepts it is about: the terms a student would look up or be "
        "tested on, as short noun phrases (\"glycolysis\", \"stroke volume\", \"Newton's second law\"), singular, "
        "lower case unless a name. Not generic words like \"process\" or \"example\".%2")
        .arg(passages,
             known.isEmpty() ? QString()
                             : QStringLiteral("\n\nWhen a concept is one of these, write it exactly the same way: %1.")
                                   .arg(known.join(", ")));
    const QJsonObject schema{
        {"type", "object"},
        {"properties",
         QJsonObject{{"items", QJsonObject{{"type", "array"},
                                           {"items", QJsonObject{{"type", "object"},
                                                                 {"properties", QJsonObject{{"n", QJsonObject{{"type", "integer"}}},
                                                                                            {"concepts", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}}}},
                                                                 {"required", QJsonArray{"n", "concepts"}}}}}}}},
        {"required", QJsonArray{"items"}}};
    const int generation = m_generation;
    QPointer<KnowledgeGraph> self(this);
    m_ollama->ask(QStringLiteral("You pick out the key concepts of study notes. Answer only with JSON."), prompt, schema,
                  [self, generation, batch](const QString &answer, const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        if (!error.isEmpty()) {
            self->m_error = error;
            self->m_busy = false;
            self->m_queue.clear();
            emit self->progressChanged();
            return;
        }
        const QJsonArray items = QJsonDocument::fromJson(answer.toUtf8()).object().value("items").toArray();
        QHash<int, QStringList> found;
        for (const QJsonValue &v : items) {
            QStringList names;
            for (const QJsonValue &c : v.toObject().value("concepts").toArray()) {
                const QString name = c.toString().simplified();
                if (name.size() >= 2 && name.size() <= 60 && names.size() < 4)
                    names << name;
            }
            found[v.toObject().value("n").toInt()] = names;
        }
        // Every passage asked about is recorded, even with no concepts, so
        // it isn't asked about again.
        for (int i = 0; i < batch.size(); ++i)
            self->m_concepts[batch[i].key] = found.value(i + 1);
        self->m_queue = self->m_queue.mid(batch.size());
        self->m_done += int(batch.size());
        self->save();
        ++self->m_revision;
        emit self->changed();
        emit self->progressChanged();
        self->askNext();
    });
}

void KnowledgeGraph::load() {
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object().value("items").toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        QStringList names;
        for (const QJsonValue &v : it.value().toArray())
            names << v.toString();
        m_concepts.insert(it.key(), names);
    }
}

void KnowledgeGraph::save() const {
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QJsonObject items;
    for (auto it = m_concepts.cbegin(); it != m_concepts.cend(); ++it)
        items.insert(it.key(), QJsonArray::fromStringList(it.value()));
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"items", items}}).toJson(QJsonDocument::Compact));
    f.commit();
}
