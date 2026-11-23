#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QVariant>
#include <atomic>
#include <memory>

// The text of every book, for finding words across the library and for
// picking the passages that answer a question about one book.
//
// A book's text is read once (PDF pages, or EPUB chapters cut into pieces),
// in the background at idle priority, and kept in ~/.cache/shelf/search/.
// Scanned PDFs use the text the outline builder read with OCR when there is
// some. Each piece is a "unit": its text and its target, a page number for
// PDFs and a spine index for EPUBs, which both readers can go to.
class BookSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    struct Unit {
        int target = 0;      // PDF page index, or EPUB spine index
        QString label;       // "p. 12", or the chapter's title
        QString text;
        QString href;        // EPUB: the section's file, as the outline links to it
    };
    struct Book {
        QString id, title, path, format;
    };

    explicit BookSearch(const QString &cacheDirectory = {}, const QString &outlineCacheDirectory = {},
                        QObject *parent = nullptr);
    ~BookSearch() override;

    bool busy() const { return m_busy > 0; }

    // Searches all the given books ([{id, title, path, format}]) for every
    // word of `query`; results arrive in resultsReady.
    Q_INVOKABLE void search(const QString &query, const QVariantList &books);
    // The passages of one book that best answer a question; they arrive in
    // passagesReady as [{n, target, label, text}].
    Q_INVOKABLE void passages(const QVariantMap &book, const QString &question, int count = 6);

    // The text from one outline entry's place to the next's (a PDF page
    // index, or an EPUB link; `to` may be empty for the end of the book).
    // It arrives in textReady.
    Q_INVOKABLE void chapterText(const QVariantMap &book, const QVariant &from, const QVariant &to);

    // Drops a book's text from memory, so it is read again (after its
    // outline was built with OCR, say).
    Q_INVOKABLE void forget(const QString &bookId);

    // The work itself, synchronous, for the worker and for tests.
    QList<Unit> units(const Book &book, const std::atomic_bool &cancel);
    static QVariantList find(const QList<Unit> &units, const QString &query, int limit);
    static QVariantList best(const QList<Unit> &units, const QString &question, int count);
    static QList<Unit> epubUnits(const QString &path);
    static QString rangeText(const QList<Unit> &units, const QVariant &from, const QVariant &to);

signals:
    void resultsReady(const QString &query, const QVariantList &results);
    void passagesReady(const QString &bookId, const QString &question, const QVariantList &passages);
    void busyChanged();
    void textReady(const QString &bookId, const QVariant &from, const QString &text);

private:
    QString m_cache, m_outlineCache;
    QMutex m_mutex;
    QHash<QString, QList<Unit>> m_loaded;
    std::shared_ptr<std::atomic_bool> m_cancelSearch;
    int m_busy = 0;
};
