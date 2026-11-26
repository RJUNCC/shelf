#include "schemehandler.h"
#include "voice.h"

#include <QRegularExpression>

#include "library.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

SchemeHandler::SchemeHandler(Library *library, QObject *parent)
    : QWebEngineUrlSchemeHandler(parent), m_library(library) {}

void SchemeHandler::registerScheme() {
    QWebEngineUrlScheme scheme("shelf");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled
                    | QWebEngineUrlScheme::FetchApiAllowed | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

QString SchemeHandler::pyodideDirectory() {
    // Beside the binary in a build tree or on Windows, in share/ when
    // installed as a package.
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &dir : {app + "/pyodide", app + "/../share/shelf/pyodide",
                               QStringLiteral("/usr/share/shelf/pyodide")})
        if (QFileInfo::exists(dir + "/pyodide.js"))
            return QDir(dir).canonicalPath();
    return {};
}

void SchemeHandler::requestStarted(QWebEngineUrlRequestJob *job) {
    const QUrl url = job->requestUrl();
    const QString host = url.host();
    const QString path = url.path();

    QString filePath;
    QByteArray mime = "application/octet-stream";
    if (host == "app") {
        if (path == "/qwebchannel.js") {
            // Shipped inside Qt WebChannel's own resources.
            filePath = ":/qtwebchannel/qwebchannel.js";
        } else if (path.startsWith("/voice/")) {
            // Speech made for lessons (src/voice.cpp).
            static const QRegularExpression name(QStringLiteral("^[0-9a-f]{40}\\.wav$"));
            const QString file = path.mid(7);
            if (!name.match(file).hasMatch())
                return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            filePath = Voice::speechDirectory() + "/" + file;
        } else if (path.startsWith("/pyodide/")) {
            // Python for code questions, too big to compile in; installed beside it.
            const QString name = path.mid(9);
            const QString dir = pyodideDirectory();
            if (dir.isEmpty() || name.contains('/') || name.contains(".."))
                return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            filePath = dir + "/" + name;
        } else {
            const QString clean = QUrl(path).path(QUrl::FullyDecoded);
            if (clean.contains(".."))
                return job->fail(QWebEngineUrlRequestJob::RequestDenied);
            filePath = ":/web" + clean;
        }
        if (filePath.endsWith(".html"))
            mime = "text/html";
        else if (filePath.endsWith(".js"))
            mime = "text/javascript";
        else if (filePath.endsWith(".css"))
            mime = "text/css";
        else if (filePath.endsWith(".wasm"))
            mime = "application/wasm";
        else if (filePath.endsWith(".json"))
            mime = "application/json";
        else if (filePath.endsWith(".zip"))
            mime = "application/zip";
        else if (filePath.endsWith(".woff2"))
            mime = "font/woff2";
        else if (filePath.endsWith(".svg"))
            mime = "image/svg+xml";
        else if (filePath.endsWith(".wav"))
            mime = "audio/wav";
        // Code questions run the student's (and the model's) code. It runs in
        // Chromium's sandbox already; this also keeps it from reaching
        // anything outside Shelf, so it can't send anything anywhere.
        if (path.startsWith("/tools/"))
            job->setAdditionalResponseHeaders(
                {{"Content-Security-Policy",
                  "default-src shelf:; script-src shelf: 'unsafe-eval' 'wasm-unsafe-eval'; "
                  "style-src shelf: 'unsafe-inline'; connect-src shelf:; worker-src shelf:; img-src shelf: data:"}});
        // The AI's answers are drawn from Shelf's files only, so nothing in
        // an answer (an image link, say) can make Shelf fetch anything.
        else if (path.startsWith("/rich/"))
            job->setAdditionalResponseHeaders(
                {{"Content-Security-Policy",
                  "default-src shelf:; script-src shelf:; style-src shelf: 'unsafe-inline'; font-src shelf:; "
                  "img-src shelf: data:; media-src shelf: blob:; connect-src shelf:"}});
    } else if (host == "book") {
        // The id may carry a ".epub" suffix so foliate-js sees a file name.
        const QString id = path.mid(1).section('.', 0, 0);
        filePath = m_library->pathForId(id);
        if (filePath.isEmpty())
            return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        mime = "application/epub+zip";
    } else {
        return job->fail(QWebEngineUrlRequestJob::UrlInvalid);
    }

    auto *file = new QFile(filePath, job);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    }
    job->reply(mime, file);
}
