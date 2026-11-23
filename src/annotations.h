#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariant>

// Highlights, notes, and bookmarks, kept per book in their own JSON file so
// the library file stays small and notes survive removing a book.
//
// Each annotation is a map:
//   id        generated
//   kind      "highlight" (a passage, maybe with a note), "note" (a note on a
//             page with no passage), or "bookmark"
//   location  an EPUB CFI, or a PDF page number as text
//   position  0–1 through the book, for ordering
//   text      the highlighted passage, if any
//   note      the user's note, if any
//   color     "yellow", "green", "blue", or "pink"
//   chapter   the chapter it falls in, for grouping
//   created, updated   milliseconds since the epoch
class Annotations : public QObject {
    Q_OBJECT
    // Bumps on every change so QML bindings re-read list().
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit Annotations(const QString &directory = {}, QObject *parent = nullptr);

    int revision() const { return m_revision; }

    // In reading order.
    Q_INVOKABLE QVariantList list(const QString &bookId) const;
    Q_INVOKABLE QVariantMap get(const QString &bookId, const QString &id) const;
    Q_INVOKABLE QString add(const QString &bookId, const QVariantMap &annotation);
    Q_INVOKABLE void update(const QString &bookId, const QString &id, const QVariantMap &changes);
    Q_INVOKABLE void remove(const QString &bookId, const QString &id);
    Q_INVOKABLE int count(const QString &bookId) const { return int(load(bookId).size()); }

    // The annotations as Markdown, grouped by chapter, for export or copying.
    // `places` maps an annotation's id to {group, subgroup}, the chapter
    // and section of the outline it falls in, as the Notes tab shows them;
    // without it, the chapter recorded when it was made is used.
    Q_INVOKABLE QString markdown(const QString &bookId, const QString &title, const QString &author,
                                 const QVariantMap &places = {}) const;
    // Writes markdown() to the Documents folder and returns the path, or an
    // empty string with the reason in `lastError`.
    Q_INVOKABLE QString exportMarkdown(const QString &bookId, const QString &title, const QString &author,
                                       const QString &directory = {}, const QVariantMap &places = {});
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    Q_INVOKABLE void copyToClipboard(const QString &text) const;

    // Undo and redo, per book, for every add, edit, and removal. Each
    // returns what was undone or redone ("add", "update", "remove") and its
    // kind, or an empty map when there was nothing to do.
    Q_INVOKABLE bool canUndo(const QString &bookId) const { return !m_undo.value(bookId).isEmpty(); }
    Q_INVOKABLE bool canRedo(const QString &bookId) const { return !m_redo.value(bookId).isEmpty(); }
    Q_INVOKABLE QVariantMap undo(const QString &bookId);
    Q_INVOKABLE QVariantMap redo(const QString &bookId);

signals:
    void changed(const QString &bookId);

private:
    // One change: the annotation before and after it. An add has no
    // `before`; a removal has no `after`.
    struct Change {
        QJsonObject before;
        QJsonObject after;
    };
    void record(const QString &bookId, const QJsonObject &before, const QJsonObject &after);
    // Puts an annotation back as it was (or takes it out), without recording.
    void restore(const QString &bookId, const QString &id, const QJsonObject &state);
    static QVariantMap describe(const Change &change, bool undoing);

    QJsonArray load(const QString &bookId) const;
    bool save(const QString &bookId, const QJsonArray &items);
    QString pathFor(const QString &bookId) const;

    QString m_directory;
    mutable QHash<QString, QJsonArray> m_cache;
    int m_revision = 0;
    QString m_lastError;
    QHash<QString, QList<Change>> m_undo;
    QHash<QString, QList<Change>> m_redo;
};
