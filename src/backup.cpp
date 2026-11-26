#include "backup.h"

#include "cards.h"
#include "filepicker.h"
#include "library.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

const char *suffix = ".shelfbackup";
const char *autoMark = " (automatic)";

QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJson(const QString &path, const QJsonObject &object) {
    QFile file(path + ".part");
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson()) < 0)
        return false;
    file.close();
    QFile::remove(path);
    return QFile::rename(path + ".part", path);
}

// Moves a folder, falling back to copy-and-delete across drives.
bool moveFolder(const QString &from, const QString &to) {
    if (QDir().rename(from, to))
        return true;
    QDir source(from);
    if (!source.exists() || !QDir().mkpath(to))
        return false;
    for (const QFileInfo &info : source.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString target = to + "/" + info.fileName();
        if (info.isDir() ? !moveFolder(info.absoluteFilePath(), target)
                         : !QFile::copy(info.absoluteFilePath(), target))
            return false;
    }
    return source.removeRecursively();
}

} // namespace

QString Backup::defaultDataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf";
}

Backup::Backup(Library *library, FilePicker *folderPicker, FilePicker *filePicker, const QString &dataDir,
               QObject *parent)
    : QObject(parent), m_library(library), m_folderPicker(folderPicker), m_filePicker(filePicker),
      m_dataDir(dataDir.isEmpty() ? defaultDataDir() : dataDir) {
    if (m_folderPicker) {
        m_folderPicker->kind = "folder";
        connect(m_folderPicker, &FilePicker::picked, this, [this](const QStringList &paths) {
            if (!paths.isEmpty() && m_library) {
                m_library->setSetting("backupFolder", paths.first());
                emit changed();
            }
        });
        connect(m_folderPicker, &FilePicker::failed, this, [this](const QString &why) { setState("failed", why); });
    }
    if (m_filePicker) {
        m_filePicker->kind = "backup";
        connect(m_filePicker, &FilePicker::picked, this, [this](const QStringList &paths) {
            if (!paths.isEmpty())
                restore(paths.first());
        });
        connect(m_filePicker, &FilePicker::failed, this, [this](const QString &why) { setState("failed", why); });
    }
}

QString Backup::folder() const {
    const QString chosen = m_library ? m_library->setting("backupFolder", QString()).toString() : QString();
    if (!chosen.isEmpty())
        return chosen;
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Shelf backups";
}

QString Backup::lastBackup() const {
    return m_library ? m_library->setting("backupLast", QString()).toString() : QString();
}

void Backup::setState(const QString &state, const QString &message) {
    m_state = state;
    m_message = message;
    emit changed();
}

void Backup::runTar(const QStringList &args, const QString &workingDir, std::function<void(bool, QString)> done) {
    m_process = new QProcess(this);
    // Gentle, like Shelf's other background work: the computer stays quick.
#if defined(Q_OS_UNIX)
    m_process->setChildProcessModifier([] { (void)::nice(19); });
#elif defined(Q_OS_WIN)
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= IDLE_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
    m_process->setWorkingDirectory(workingDir);
    connect(m_process, &QProcess::finished, this, [this, done](int code, QProcess::ExitStatus status) {
        const QString errors = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
        m_process->deleteLater();
        m_process = nullptr;
        done(status == QProcess::NormalExit && code == 0, errors);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        done(false, tr("tar isn't available on this computer."));
    });
    m_process->start("tar", args);
}

void Backup::create(bool includeBooks) { make(includeBooks, false); }

void Backup::make(bool includeBooks, bool automatic) {
    if (m_state == "working")
        return;
    if (m_library)
        m_library->flush();
    if (!QDir().mkpath(folder())) {
        setState("failed", tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(folder())));
        return;
    }
    auto staging = std::make_shared<QTemporaryDir>();
    if (!staging->isValid()) {
        setState("failed", tr("Could not make a temporary folder."));
        return;
    }

    // The books' own files, under their ids so names never clash.
    QJsonObject books;
    if (includeBooks && m_library) {
        QDir().mkpath(staging->filePath("books"));
        for (const QVariant &v : m_library->allBooks()) {
            const QVariantMap book = v.toMap();
            const QFileInfo source(book.value("path").toString());
            // Text versions are already inside the data folder.
            if (source.absoluteFilePath().startsWith(m_dataDir + "/"))
                continue;
            const QString name = "books/" + book.value("id").toString() + "." + source.suffix().toLower();
#ifdef Q_OS_UNIX
            const bool ok = QFile::link(source.absoluteFilePath(), staging->filePath(name));
#else
            // A hard link costs nothing on the same drive; else a copy.
            const bool ok = CreateHardLinkW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(staging->filePath(name)).utf16()),
                                            reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source.absoluteFilePath()).utf16()),
                                            nullptr)
                            || QFile::copy(source.absoluteFilePath(), staging->filePath(name));
#endif
            if (ok)
                books.insert(book.value("id").toString(), name);
        }
    }

    const QDir data(m_dataDir);
    const QString dataName = data.dirName();
    QJsonObject manifest{{"app", "shelf"},
                         {"format", 1},
                         {"version", QCoreApplication::applicationVersion()},
                         {"created", QDateTime::currentDateTime().toString(Qt::ISODate)},
                         {"dataDir", m_dataDir},
                         {"dataName", dataName},
                         {"books", books}};
    if (!writeJson(staging->filePath("manifest.json"), manifest)) {
        setState("failed", tr("Could not write the backup's contents list."));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
    const QString target = folder() + "/Shelf backup " + stamp + (automatic ? autoMark : "") + suffix;
    const QString part = target + ".part";
    QDir parent = data;
    parent.cdUp();
    QStringList args{"-czf", part};
#ifdef Q_OS_UNIX
    if (!books.isEmpty())
        args << "-h";   // the links to books become the books themselves
#endif
    args << "--exclude=" + dataName + "/voice" << "--exclude=*.part" << "--exclude=*.bak*"
         << "-C" << staging->path() << "manifest.json";
    if (!books.isEmpty())
        args << "books";
    args << "-C" << parent.absolutePath() << dataName;
    setState("working", includeBooks ? tr("Backing up your data and books…") : tr("Backing up your data…"));
    runTar(args, staging->path(), [this, staging, part, target, automatic](bool ok, const QString &errors) {
        if (!ok) {
            QFile::remove(part);
            setState("failed", tr("The backup didn't finish: %1").arg(errors.left(300)));
            return;
        }
        QFile::remove(target);
        QFile::rename(part, target);
        if (m_library)
            m_library->setSetting("backupLast", QDateTime::currentDateTime().toString(Qt::ISODate));
        if (automatic)
            prune();
        const double mb = QFileInfo(target).size() / 1e6;
        setState("done", tr("Saved %1 (%2 MB).").arg(QFileInfo(target).fileName()).arg(mb, 0, 'f', 1));
    });
}

void Backup::createIfDue() {
    if (!m_library || !m_library->setting("backupAuto", false).toBool() || m_state == "working")
        return;
    const QDateTime last = QDateTime::fromString(lastBackup(), Qt::ISODate);
    if (last.isValid() && last.daysTo(QDateTime::currentDateTime()) < 7)
        return;
    make(false, true);
}

void Backup::prune() {
    QDir dir(folder());
    const QStringList autos =
        dir.entryList({QStringLiteral("Shelf backup *") + autoMark + suffix}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = 5; i < autos.size(); ++i)
        dir.remove(autos[i]);
}

void Backup::chooseFolder() {
    if (m_folderPicker)
        m_folderPicker->pickBooks(tr("Where to keep Shelf's backups"));
}

void Backup::openFolder() {
    QDir().mkpath(folder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
}

void Backup::chooseRestore() {
    if (m_filePicker)
        m_filePicker->pickBooks(tr("Restore a Shelf backup"));
}

void Backup::restore(const QString &path) {
    if (m_state == "working")
        return;
    if (!QFileInfo(path).isFile()) {
        setState("failed", tr("That backup file isn't there."));
        return;
    }
    QDir parent(m_dataDir);
    parent.cdUp();
    const QString staging = parent.filePath("shelf-restore.part");
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging)) {
        setState("failed", tr("Could not make a folder to unpack the backup in."));
        return;
    }
    setState("working", tr("Unpacking the backup…"));
    runTar({"-xzf", QFileInfo(path).absoluteFilePath(), "-C", staging}, staging,
           [this, staging, parent](bool ok, const QString &errors) {
               const QJsonObject manifest = readJson(staging + "/manifest.json");
               const QString name = manifest.value("dataName").toString();
               const bool valid = ok && manifest.value("app").toString() == "shelf" && !name.isEmpty()
                                  && !name.contains('/') && !name.contains("..")
                                  && QFileInfo::exists(staging + "/" + name + "/library.json");
               if (!valid) {
                   QDir(staging).removeRecursively();
                   setState("failed", ok ? tr("That file isn't a Shelf backup.")
                                         : tr("Could not unpack the backup: %1").arg(errors.left(300)));
                   return;
               }
               const QString ready = parent.filePath("shelf-restore");
               QDir(ready).removeRecursively();
               if (!QDir().rename(staging, ready)) {
                   QDir(staging).removeRecursively();
                   setState("failed", tr("Could not get the backup ready."));
                   return;
               }
               const QDateTime when = QDateTime::fromString(manifest.value("created").toString(), Qt::ISODate);
               setState("restart", tr("The backup from %1 is ready. Shelf will start again to load it; what you have now is kept aside.")
                                       .arg(QLocale().toString(when, QLocale::ShortFormat)));
           });
}

void Backup::exportCards(const QString &format) {
    if (!m_cards || m_state == "working")
        return;
    const QVariantList cards = m_cards->list();
    if (cards.isEmpty()) {
        setState("failed", tr("There are no flashcards yet."));
        return;
    }
    QVariantMap titles;
    if (m_library)
        for (const QVariant &v : cards) {
            const QString id = v.toMap().value("bookId").toString();
            if (!titles.contains(id))
                titles.insert(id, m_library->book(id).value("title"));
        }
    QDir().mkpath(folder());
    const bool anki = format == "anki";
    const QString path = folder() + "/Shelf flashcards (" + (anki ? "Anki" : "Quizlet") + ") "
                         + QDate::currentDate().toString(Qt::ISODate) + ".txt";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState("failed", tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    file.write(Cards::exportText(cards, format, titles).toUtf8());
    file.close();
    setState("done", anki ? tr("Saved %n card(s) for Anki to %1. In Anki: File › Import.", "", int(cards.size()))
                                .arg(QFileInfo(path).fileName())
                          : tr("Saved %n card(s) for Quizlet to %1. In Quizlet: Create › Import, and paste it in.", "",
                               int(cards.size()))
                                .arg(QFileInfo(path).fileName()));
}

void Backup::restart() {
    // The new Shelf waits for this one to be gone (see "--restarted" in main).
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--restarted"});
    QCoreApplication::quit();
}

QString Backup::applyPending(const QString &dataDirIn) {
    const QString dataDir = dataDirIn.isEmpty() ? defaultDataDir() : dataDirIn;
    QDir parent(dataDir);
    parent.cdUp();
    const QString ready = parent.filePath("shelf-restore");
    if (!QFileInfo(ready).isDir())
        return {};
    const QJsonObject manifest = readJson(ready + "/manifest.json");
    const QString name = manifest.value("dataName").toString();
    const QString restored = ready + "/" + name;
    if (name.isEmpty() || !QFileInfo::exists(restored + "/library.json")) {
        QDir(ready).removeRecursively();
        return QCoreApplication::translate("Backup", "The backup couldn't be restored: it was incomplete.");
    }

    // What's here now is kept, not deleted.
    const QString aside =
        parent.filePath("shelf-before-restore-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    if (QFileInfo::exists(dataDir) && !moveFolder(dataDir, aside)) {
        QDir(ready).removeRecursively();
        return QCoreApplication::translate("Backup", "The backup couldn't be restored: Shelf's data folder is in use.");
    }
    if (!moveFolder(restored, dataDir)) {
        moveFolder(aside, dataDir);
        QDir(ready).removeRecursively();
        return QCoreApplication::translate("Backup", "The backup couldn't be restored: its files couldn't be moved into place.");
    }
    // Downloaded voices weren't in the backup; keep the ones here.
    if (QFileInfo::exists(aside + "/voice") && !QFileInfo::exists(dataDir + "/voice"))
        moveFolder(aside + "/voice", dataDir + "/voice");

    // Books point at their files here: their old place if it exists on this
    // computer, else the copy from the backup; text versions follow the
    // data folder.
    const QString oldData = manifest.value("dataDir").toString();
    const QJsonObject bookFiles = manifest.value("books").toObject();
    QJsonObject library = readJson(dataDir + "/library.json");
    QJsonArray books = library.value("books").toArray();
    for (int i = 0; i < books.size(); ++i) {
        QJsonObject book = books[i].toObject();
        QString path = book.value("path").toString();
        if (!oldData.isEmpty() && path.startsWith(oldData + "/"))
            path = dataDir + path.mid(oldData.size());
        const QString copy = bookFiles.value(book.value("id").toString()).toString();
        if (!QFileInfo::exists(path) && !copy.isEmpty() && !copy.contains("..")
            && QFileInfo::exists(ready + "/" + copy)) {
            QDir().mkpath(dataDir + "/books");
            const QString target = dataDir + "/" + copy;
            QFile::remove(target);
            if (QFile::rename(ready + "/" + copy, target) || QFile::copy(ready + "/" + copy, target))
                path = target;
        }
        book["path"] = path;
        books[i] = book;
    }
    library["books"] = books;
    writeJson(dataDir + "/library.json", library);
    QDir(ready).removeRecursively();
    return QCoreApplication::translate("Backup", "Your backup is restored. What you had before is kept in %1.")
        .arg(QDir::toNativeSeparators(aside));
}
