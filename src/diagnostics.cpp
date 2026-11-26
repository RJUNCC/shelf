#include "diagnostics.h"

#include "library.h"
#include "ollama.h"
#include "voice.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <cstdio>

namespace {

QString g_logFile;
QMutex g_logMutex;
QtMessageHandler g_previous = nullptr;

void writeLog(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (g_previous)
        g_previous(type, context, message);
    // Debug chatter stays out; warnings and worse are what help.
    if (type == QtDebugMsg || type == QtInfoMsg)
        return;
    const char *kind = type == QtWarningMsg ? "warning" : type == QtCriticalMsg ? "critical" : "fatal";
    QMutexLocker lock(&g_logMutex);
    QFile file(g_logFile);
    if (file.size() > 1024 * 1024) {
        // Keep the newer half.
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray all = file.readAll();
            file.close();
            QByteArray tail = all.right(all.size() / 2);
            tail = tail.mid(tail.indexOf('\n') + 1);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(tail);
                file.close();
            }
        }
    }
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(QStringLiteral("%1 %2 %3\n")
                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate), QLatin1String(kind), message)
                   .toUtf8());
}

} // namespace

Diagnostics::Diagnostics(Library *library, Ollama *ollama, Voice *voice, QObject *parent)
    : QObject(parent), m_library(library), m_ollama(ollama), m_voice(voice) {}

QString Diagnostics::logFile() {
    return g_logFile.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/shelf.log"
               : g_logFile;
}

void Diagnostics::installLog(const QString &file) {
    g_logFile = file.isEmpty() ? logFile() : file;
    QDir().mkpath(QFileInfo(g_logFile).absolutePath());
    g_previous = qInstallMessageHandler(writeLog);
    qWarning("Shelf %s started", qPrintable(QCoreApplication::applicationVersion()));
}

QString Diagnostics::report() const {
    QStringList lines;
    lines << QStringLiteral("Shelf %1").arg(QCoreApplication::applicationVersion())
          << QStringLiteral("System: %1 (%2, %3)")
                 .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture())
          << QStringLiteral("Qt: %1, %2").arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName());
    if (m_library)
        lines << QStringLiteral("Books: %1").arg(m_library->totalCount());
    if (m_ollama)
        lines << QStringLiteral("Local AI: %1, model %2, %3")
                     .arg(m_ollama->status(), m_ollama->model().isEmpty() ? QStringLiteral("none") : m_ollama->model(),
                          m_ollama->useGpu() ? QStringLiteral("graphics card") : QStringLiteral("processor only"));
    if (m_voice)
        lines << QStringLiteral("Voice: %1 (%2)").arg(m_voice->current(), m_voice->status());

    // The end of the log, with the home folder taken out of paths.
    QFile file(logFile());
    QString log;
    if (file.open(QIODevice::ReadOnly)) {
        if (file.size() > 12000)
            file.seek(file.size() - 12000);
        log = QString::fromUtf8(file.readAll());
        log = log.mid(file.pos() > 12000 ? log.indexOf('\n') + 1 : 0);
    }
    const QString home = QDir::homePath();
    log.replace(home, QStringLiteral("~"));
    log.replace(QDir::toNativeSeparators(home), QStringLiteral("~"));
    lines << QString() << QStringLiteral("Recent log:") << (log.trimmed().isEmpty() ? QStringLiteral("(empty)") : log.trimmed());
    return lines.join('\n');
}

void Diagnostics::copyReport() const {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(report());
}

void Diagnostics::openLogFolder() const {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(logFile()).absolutePath()));
}
