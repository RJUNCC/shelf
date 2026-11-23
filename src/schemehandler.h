#pragma once

#include <QWebEngineUrlSchemeHandler>

class Library;

// Serves the EPUB reader to WebEngine under shelf://:
//   shelf://app/<path>   the reader page and foliate-js, compiled into the binary
//   shelf://book/<id>    the bytes of a shelved book, read-only
//   shelf://app/pyodide/ Python for code questions, from where it is installed
//   shelf://app/tools/   the hands-on question tools, kept from the network
// A custom scheme (rather than file://) lets the page use ES modules and
// fetch() while only ever reaching the books the library knows about.
class SchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT

public:
    explicit SchemeHandler(Library *library, QObject *parent = nullptr);
    void requestStarted(QWebEngineUrlRequestJob *job) override;

    // Where Python for code questions (Pyodide) is installed, or empty.
    static QString pyodideDirectory();

    // Must run before the QGuiApplication exists.
    static void registerScheme();

private:
    Library *m_library;
};
