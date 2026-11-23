#pragma once

#include <QHash>
#include <QObject>
#include <QVariant>

class Annotations;
class Ollama;

// The connections between what has been read: books, their chapters, the
// highlights and notes in them, and the concepts those are about.
//
// Concepts come from two places: [[wiki links]] typed in notes, at once;
// and, when asked (findConcepts), key terms the local model picks out of
// each highlight and note, once, cached in ~/.local/share/shelf/concepts.json.
// A concept shared by highlights in different books links those books.
class KnowledgeGraph : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY progressChanged)
    // Bumps when the concepts change, so the graph is rebuilt.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit KnowledgeGraph(Annotations *annotations, Ollama *ollama, const QString &file = {},
                            QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString error() const { return m_error; }
    int revision() const { return m_revision; }

    // {nodes: [{id, kind, label, ...}], links: [{source, target}]} for the
    // given books ([{id, title, format}]). Kinds: book, chapter, highlight,
    // note, concept. Highlights and notes carry bookId, location, format,
    // text, note and color, so the view can open them.
    Q_INVOKABLE QVariantMap graph(const QVariantList &books) const;

    // How many highlights and notes in these books have no concepts yet.
    Q_INVOKABLE int pending(const QVariantList &books) const;
    // Asks the local model for the concepts of every highlight and note
    // that has none yet, a few at a time.
    Q_INVOKABLE void findConcepts(const QVariantList &books);
    Q_INVOKABLE void cancel();

    // [[wiki links]] in a note.
    static QStringList wikiLinks(const QString &text);
    // A concept's key: lower case, single spaces, without a plural "s" when
    // the singular is also known.
    static QString conceptKey(const QString &name);

signals:
    void changed();
    void progressChanged();

private:
    struct Item {
        QString key, text;
    };
    QList<Item> missing(const QVariantList &books) const;
    void askNext();
    void load();
    void save() const;
    static QString itemKey(const QString &bookId, const QVariantMap &annotation);

    Annotations *m_annotations;
    Ollama *m_ollama;
    QString m_file;
    QHash<QString, QStringList> m_concepts;     // itemKey → concept names
    QList<Item> m_queue;
    bool m_busy = false;
    int m_done = 0, m_total = 0, m_revision = 0, m_generation = 0;
    QString m_error;
};
