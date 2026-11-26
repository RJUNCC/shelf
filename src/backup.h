#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class Cards;
class FilePicker;
class Library;
class QProcess;

// Backs up everything Shelf keeps about your reading into one file, and
// brings it back on this or another computer.
//
// A backup (".shelfbackup", a gzipped tar made by the system's tar, which
// Windows 10 and 11 have too) holds manifest.json and Shelf's data folder:
// the library with reading positions and settings, highlights and notes,
// flashcards, outlines, concepts, workspaces with their slides, and text
// versions. Downloaded voices are left out (they download again). With
// `includeBooks`, the books' own files go in books/ as well, so a new
// computer gets them too.
//
// Restoring unpacks into a folder beside the data folder and asks for a
// restart; on the next start, before anything is loaded, applyPending()
// sets the current data aside (shelf-before-restore-<date>) and puts the
// backup in its place, pointing books at their restored files when their
// old place doesn't exist here.
class Backup : public QObject {
    Q_OBJECT
    // "idle", "working", "done", "failed", "restart" (a restore is
    // unpacked and waits for Shelf to start again) or "restored" (this
    // start applied one).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // Where backups go; defaults to Documents/Shelf backups.
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    // ISO date of the last backup, or empty.
    Q_PROPERTY(QString lastBackup READ lastBackup NOTIFY changed)

public:
    Backup(Library *library, FilePicker *folderPicker, FilePicker *filePicker, const QString &dataDir = {},
           QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString folder() const;
    QString lastBackup() const;

    // Makes a backup in folder(). Done or failed sets state and message.
    Q_INVOKABLE void create(bool includeBooks);
    // Makes one if automatic backups are on and the last is a week old;
    // keeps the newest five automatic ones.
    Q_INVOKABLE void createIfDue();
    Q_INVOKABLE void chooseFolder();
    Q_INVOKABLE void openFolder();
    // Asks for a backup file, then restore(path).
    Q_INVOKABLE void chooseRestore();
    Q_INVOKABLE void restore(const QString &path);
    // Writes every flashcard to a text file in folder() for Anki ("anki")
    // or Quizlet ("quizlet"); done or failed sets state and message.
    Q_INVOKABLE void exportCards(const QString &format);
    void setCards(Cards *cards) { m_cards = cards; }
    // Starts Shelf again (after a restore).
    Q_INVOKABLE void restart();

    // Called first thing at startup. Returns a message when a restore was
    // applied (or failed), else empty.
    static QString applyPending(const QString &dataDir = {});
    static QString defaultDataDir();
    // Shows what applyPending() said (state "restored").
    void setRestored(const QString &message) {
        if (!message.isEmpty())
            setState("restored", message);
    }

signals:
    void changed();

private:
    void setState(const QString &state, const QString &message = {});
    void make(bool includeBooks, bool automatic);
    void runTar(const QStringList &args, const QString &workingDir, std::function<void(bool, QString)> done);
    void prune();

    Library *m_library;
    FilePicker *m_folderPicker, *m_filePicker;
    QString m_dataDir;
    QString m_state = "idle", m_message;
    QProcess *m_process = nullptr;
    Cards *m_cards = nullptr;
};
