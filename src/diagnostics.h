#pragma once

#include <QObject>
#include <QString>

class Library;
class Ollama;
class Voice;

// What to send when something goes wrong: Shelf keeps a small log of its
// warnings (shelf.log in its cache folder, cut back once it passes 1 MB),
// and report() puts the version, the computer, the local AI and voice, and
// the end of that log together, for "Copy diagnostic info" in Help.
// Nothing is sent anywhere by Shelf.
class Diagnostics : public QObject {
    Q_OBJECT
public:
    Diagnostics(Library *library, Ollama *ollama, Voice *voice, QObject *parent = nullptr);

    // Sends Qt's messages to the log file too (and still to the terminal).
    static void installLog(const QString &file = {});
    static QString logFile();

    Q_INVOKABLE QString report() const;
    Q_INVOKABLE void copyReport() const;
    Q_INVOKABLE void openLogFolder() const;

private:
    Library *m_library;
    Ollama *m_ollama;
    Voice *m_voice;
};
