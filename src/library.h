#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>

class FilePicker;

// The user's books: where they are, what they are called, how far along the
// reader is, and which collections they belong to. Books stay where the user
// keeps them; the library only remembers their paths and never writes to them.
class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList books READ books NOTIFY viewChanged)
    Q_PROPERTY(QVariantList continueReading READ continueReading NOTIFY viewChanged)
    Q_PROPERTY(QStringList collections READ collections NOTIFY collectionsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY viewChanged)
    Q_PROPERTY(QString shelf READ shelf WRITE setShelf NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY viewChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    // Directories are injectable so tests never touch the user's library.
    explicit Library(const QString &dataDirectory = {}, const QString &cacheDirectory = {},
                     FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Library() override;

    QVariantList books() const;
    QVariantList continueReading() const;
    QStringList collections() const { return m_collections; }
    int totalCount() const { return int(m_books.size()); }

    // "all", "reading", "finished", "epub", "pdf", or "collection:<name>".
    QString shelf() const { return m_shelf; }
    void setShelf(const QString &shelf);
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    // "recent", "title", "author", or "added".
    QString sort() const { return m_sort; }
    void setSort(const QString &sort);

    QString message() const { return m_message; }

    // Adds a file (or refreshes it if it is already shelved) and returns its
    // id, or an empty string with the reason in `message`.
    Q_INVOKABLE QString addFile(const QString &path);
    // Adds the practice book the Learn course uses (bundled with Shelf,
    // copied beside the library on first use) and returns its id.
    Q_INVOKABLE QString addPracticeBook();
    Q_INVOKABLE int addUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void pickFiles();

    Q_INVOKABLE QVariantMap book(const QString &id) const;
    // Every book that is still where it was, whatever the view shows:
    // [{id, title, path, format}], by title.
    Q_INVOKABLE QVariantList allBooks() const;
    QString pathForId(const QString &id) const;
    // What the reader loads: the custom scheme for EPUBs (served by
    // SchemeHandler) and a plain file URL for PDFs.
    Q_INVOKABLE QUrl readerUrl(const QString &id) const;

    Q_INVOKABLE void markOpened(const QString &id);
    Q_INVOKABLE void saveProgress(const QString &id, const QString &location, double progress);
    Q_INVOKABLE void setFinished(const QString &id, bool finished);
    Q_INVOKABLE void remove(const QString &id);

    Q_INVOKABLE bool createCollection(const QString &name);
    Q_INVOKABLE void deleteCollection(const QString &name);
    // The ids of the books in a collection.
    Q_INVOKABLE QStringList collectionMembers(const QString &name) const;
    Q_INVOKABLE void setInCollection(const QString &id, const QString &name, bool member);
    // For several books at once (the library's select mode), saving once.
    // addToCollection creates the collection if it is new and returns how
    // many books were added to it.
    Q_INVOKABLE int addToCollection(const QStringList &ids, const QString &name);
    Q_INVOKABLE void setFinishedMany(const QStringList &ids, bool finished);
    Q_INVOKABLE void removeMany(const QStringList &ids);

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    // Forgets settings whose names start with `prefix` ("tip:" brings the
    // first-use tips back); settingsReset() follows.
    Q_INVOKABLE void resetSettings(const QString &prefix);

    Q_INVOKABLE void clearMessage();
    // Redraws the library after reading, when progress changed quietly.
    Q_INVOKABLE void refresh() { emit viewChanged(); }

    // Writes any pending change now; also runs on destruction.
    void flush();

    static QString idForPath(const QString &absolutePath);

signals:
    void viewChanged();
    void collectionsChanged();
    void messageChanged();
    void settingsReset();
    // A book arrived from outside (file manager, command line, drop) and
    // should be shown to the user.
    void openRequested(const QString &id);

public slots:
    void openExternal(const QString &path);

private:
    QVariantMap present(const QJsonObject &book) const;
    void load();
    void changed(bool viewToo = true);
    void setMessage(const QString &message);

    QString m_dataDirectory;
    QString m_coverDirectory;
    QHash<QString, QJsonObject> m_books;
    QStringList m_collections;
    QJsonObject m_settings;
    QString m_shelf = "all";
    QString m_search;
    QString m_sort = "recent";
    QString m_message;
    QTimer m_saveTimer;
    bool m_dirty = false;
    FilePicker *m_picker = nullptr;
};
