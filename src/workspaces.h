#pragma once

#include <QObject>
#include <QVariant>

class FilePicker;
class Library;

// Learning workspaces: lecture slides put beside the books they draw on.
//
// Each workspace lives in its own folder (~/.local/share/shelf/workspaces/
// <id>/) holding workspace.json and copies of its decks, so the user's own
// files are never touched and moving them breaks nothing. A deck is a PDF,
// or a PowerPoint file shown through the PDF LibreOffice makes of it (once,
// at idle priority) or, without LibreOffice, as cards drawn from its text
// and pictures. Every slide can link to sections of the books.
class Workspaces : public QObject {
    Q_OBJECT
    // [{id, name, decks, slides, updated}], most recently changed first.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    // What is being imported ("Reading Lecture 3…"), or empty.
    Q_PROPERTY(QString importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(bool hasLibreOffice READ hasLibreOffice CONSTANT)

public:
    explicit Workspaces(Library *library, FilePicker *picker, const QString &directory = {}, QObject *parent = nullptr);

    QVariantList list() const;
    QString importing() const { return m_importing; }
    bool hasLibreOffice() const;

    Q_INVOKABLE QString create(const QString &name);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void remove(const QString &id);
    // {id, name, books: [ids], decks: [{id, title, kind ("pdf", "pptx"),
    // pdf (file to show, or "" for cards), slides: [{title, paragraphs,
    // images, notes, links: [{bookId, target, label, snippet}]}]}]}
    Q_INVOKABLE QVariantMap workspace(const QString &id) const;

    // Asks for slide files, then adds them to the workspace.
    Q_INVOKABLE void pickDecks(const QString &id);
    Q_INVOKABLE void addDecks(const QString &id, const QStringList &paths);
    Q_INVOKABLE void removeDeck(const QString &id, const QString &deckId);
    // Moves a deck up (−1) or down (+1) the list, or sorts them all by
    // name, numbers in order ("week 2" before "week 10").
    Q_INVOKABLE void moveDeck(const QString &id, const QString &deckId, int step);
    Q_INVOKABLE void sortDecks(const QString &id);
    // Folders group decks (one level). A deck's folder is "" when it has
    // none; renaming or removing a folder moves its decks along (removing
    // leaves them loose, never deletes them).
    Q_INVOKABLE void setDeckFolder(const QString &id, const QString &deckId, const QString &folder);
    Q_INVOKABLE void renameFolder(const QString &id, const QString &from, const QString &to);
    Q_INVOKABLE void removeFolder(const QString &id, const QString &folder);

    // The user's own note on a slide.
    Q_INVOKABLE void setSlideNote(const QString &id, const QString &deckId, int slide, const QString &note);
    // Remembers that a slide was explained (or a question about it asked),
    // newest replacing an older entry for the same slide and question.
    Q_INVOKABLE void addHistory(const QString &id, const QString &deckId, int slide, const QString &question);

    // Asks for a document holding a study review (PDF, Word, LibreOffice,
    // a saved web page, or plain text); its text arrives in reviewLoaded.
    Q_INVOKABLE void pickReviewDocument(const QString &id);
    // A document's text, for the above and for tests; empty when it can't
    // be read.
    static QString documentText(const QString &path);

    // A study plan for the next exam: {review, examDate, topics: [{topic,
    // details, keywords, slides: [{deckId, slide}], done}], created}; an
    // empty map removes it.
    Q_INVOKABLE void setPlan(const QString &id, const QVariantMap &plan);

    Q_INVOKABLE void addLink(const QString &id, const QString &deckId, int slide, const QVariantMap &link);
    Q_INVOKABLE void removeLink(const QString &id, const QString &deckId, int slide, int index);
    // The books whose sections are suggested for slides.
    Q_INVOKABLE void setBooks(const QString &id, const QStringList &bookIds);

    // A book's sections for linking: [{label, depth, target}], from an
    // EPUB's table of contents or a PDF's bookmarks (or the outline Shelf
    // built for it), numbered by the QML side as everywhere else.
    Q_INVOKABLE QVariantList sections(const QString &bookId) const;

    // The pieces, for tests.
    static QVariantList epubSections(const QString &path);
    static QVariantList pdfSections(const QString &path);
    QString folder(const QString &id) const;

signals:
    void listChanged();
    void changed(const QString &id);
    void importingChanged();
    void message(const QString &text);
    void reviewLoaded(const QString &id, const QString &name, const QString &text);

private:
    QVariantMap load(const QString &id) const;
    bool save(const QString &id, QVariantMap workspace);
    void importNext();
    void renameFolderTo(const QString &id, const QString &from, const QString &to);

    Library *m_library;
    FilePicker *m_picker;
    QString m_directory;
    QString m_pickingFor;
    QString m_importing;
    QList<QPair<QString, QString>> m_queue;   // (workspace, file) waiting to be imported
    bool m_busy = false;
};
