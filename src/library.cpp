#include "library.h"

#include "bookmeta.h"
#include "filepicker.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace {

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

// Books named "The Hobbit" sort under H, the way a shelf in a shop does.
QString sortKey(const QString &text) {
    QString key = text.trimmed().toLower();
    for (const QString &article : {QStringLiteral("the "), QStringLiteral("a "), QStringLiteral("an ")})
        if (key.startsWith(article))
            return key.mid(article.size());
    return key;
}

} // namespace

Library::Library(const QString &dataDirectory, const QString &cacheDirectory, FilePicker *picker, QObject *parent)
    : QObject(parent),
      m_dataDirectory(dataDirectory.isEmpty()
                          ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf"
                          : dataDirectory),
      m_coverDirectory((cacheDirectory.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf"
                            : cacheDirectory)
                       + "/covers"),
      m_picker(picker) {
    QDir().mkpath(m_dataDirectory);
    QDir().mkpath(m_coverDirectory);

    // Progress arrives on every page turn; batch the writes.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &Library::flush);

    if (m_picker) {
        connect(m_picker, &FilePicker::picked, this, [this](const QStringList &paths) {
            QList<QUrl> urls;
            for (const auto &path : paths)
                urls << QUrl::fromLocalFile(path);
            addUrls(urls);
        });
        connect(m_picker, &FilePicker::failed, this, &Library::setMessage);
    }
    load();
}

Library::~Library() { flush(); }

QString Library::idForPath(const QString &absolutePath) {
    return QString::fromLatin1(
        QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

void Library::load() {
    QFile file(m_dataDirectory + "/library.json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll(), &error).object();
    if (error.error != QJsonParseError::NoError) {
        // Keep the unreadable file for the user instead of overwriting it on
        // the next save.
        file.close();
        QFile::rename(file.fileName(), file.fileName() + ".broken-" + QString::number(now()));
        setMessage(tr("Your library file was unreadable and has been set aside."));
        return;
    }
    for (const auto &value : root.value("books").toArray()) {
        const QJsonObject book = value.toObject();
        if (!book.value("id").toString().isEmpty())
            m_books.insert(book.value("id").toString(), book);
    }
    for (const auto &value : root.value("collections").toArray())
        m_collections << value.toString();
    m_settings = root.value("settings").toObject();
    const QString sort = m_settings.value("sort").toString();
    if (QStringList{"recent", "title", "author", "added"}.contains(sort))
        m_sort = sort;
}

void Library::flush() {
    m_saveTimer.stop();
    if (!m_dirty)
        return;
    m_dirty = false;
    QJsonArray books;
    QStringList ids = m_books.keys();
    ids.sort();
    for (const auto &id : ids)
        books.append(m_books.value(id));
    const QJsonObject root{{"version", 1},
                           {"books", books},
                           {"collections", QJsonArray::fromStringList(m_collections)},
                           {"settings", m_settings}};
    // Written beside the target and renamed over it, so a crash mid-write
    // never leaves a truncated library.
    QSaveFile file(m_dataDirectory + "/library.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit())
        setMessage(tr("Could not save the library: %1").arg(file.errorString()));
}

void Library::changed(bool viewToo) {
    m_dirty = true;
    m_saveTimer.start();
    if (viewToo)
        emit viewChanged();
}

void Library::setMessage(const QString &message) {
    if (message == m_message)
        return;
    m_message = message;
    emit messageChanged();
}

void Library::clearMessage() { setMessage({}); }

QString Library::addPracticeBook() {
    const QString path = m_dataDirectory + "/Practising with Shelf.epub";
    if (!QFile::exists(path))
        QFile::copy(":/learn/shelf-guide.epub", path);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
    return addFile(path);
}

QString Library::addFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        setMessage(tr("%1 is not a file.").arg(info.fileName().isEmpty() ? path : info.fileName()));
        return {};
    }
    const QString absolute = info.canonicalFilePath();
    const QString id = idForPath(absolute);

    const BookMeta meta = BookMetaReader::read(absolute);
    if (!meta.error.isEmpty()) {
        setMessage(tr("Could not add %1: %2").arg(info.fileName(), meta.error));
        return {};
    }

    QJsonObject book = m_books.value(id);
    const bool isNew = book.isEmpty();
    book["id"] = id;
    book["path"] = absolute;
    book["format"] = meta.format;
    book["title"] = meta.title.isEmpty() ? info.completeBaseName() : meta.title;
    book["author"] = meta.author;
    if (isNew)
        book["added"] = double(now());

    const QString coverPath = m_coverDirectory + "/" + id + ".jpg";
    if (!meta.cover.isNull()) {
        const QImage cover = meta.cover.height() > 480
                                 ? meta.cover.scaledToHeight(480, Qt::SmoothTransformation)
                                 : meta.cover;
        // Re-saving replaces the file, so drop QML's cached image by bumping
        // a revision in the URL.
        if (cover.convertToFormat(QImage::Format_RGB32).save(coverPath, "JPG", 88))
            book["coverRevision"] = book.value("coverRevision").toInt() + 1;
    } else {
        QFile::remove(coverPath);
        book.remove("coverRevision");
    }

    m_books.insert(id, book);
    changed();
    return id;
}

int Library::addUrls(const QList<QUrl> &urls) {
    int added = 0;
    QStringList failures;
    for (const auto &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        // A dropped folder adds the books inside it.
        QStringList paths;
        if (info.isDir()) {
            QDirIterator it(info.absoluteFilePath(), {"*.epub", "*.pdf", "*.EPUB", "*.PDF"}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
                paths << it.next();
        } else {
            paths << info.absoluteFilePath();
        }
        for (const auto &path : paths) {
            if (addFile(path).isEmpty())
                failures << m_message;
            else
                ++added;
        }
    }
    if (failures.size() == 1)
        setMessage(failures.first());
    else if (failures.size() > 1)
        setMessage(tr("%n file(s) could not be added.", "", int(failures.size())));
    return added;
}

void Library::pickFiles() {
    if (!m_picker) {
        setMessage(tr("No file picker is available."));
        return;
    }
    m_picker->pickBooks(tr("Add books"));
}

void Library::openExternal(const QString &path) {
    const QString id = addFile(path);
    if (!id.isEmpty())
        emit openRequested(id);
}

QVariantMap Library::present(const QJsonObject &book) const {
    QVariantMap map = book.toVariantMap();
    const QString id = book.value("id").toString();
    const QString cover = m_coverDirectory + "/" + id + ".jpg";
    if (book.contains("coverRevision") && QFileInfo::exists(cover)) {
        QUrl url = QUrl::fromLocalFile(cover);
        url.setQuery("r=" + QString::number(book.value("coverRevision").toInt()));
        map["cover"] = url;
    } else {
        map["cover"] = QString();
    }
    map["missing"] = !QFileInfo::exists(book.value("path").toString());
    map["progress"] = book.value("progress").toDouble();
    map["finished"] = book.value("finished").toBool();
    map["collections"] = book.value("collections").toVariant().toStringList();
    return map;
}

QVariantList Library::books() const {
    QList<QJsonObject> list;
    const QString collection = m_shelf.startsWith("collection:") ? m_shelf.mid(11) : QString();
    const QString needle = m_search.trimmed();
    for (const auto &book : m_books) {
        const double progress = book.value("progress").toDouble();
        const bool finished = book.value("finished").toBool();
        if (m_shelf == "reading" && (finished || progress <= 0))
            continue;
        if (m_shelf == "finished" && !finished)
            continue;
        if ((m_shelf == "epub" || m_shelf == "pdf") && book.value("format").toString() != m_shelf)
            continue;
        if (!collection.isEmpty() && !book.value("collections").toVariant().toStringList().contains(collection))
            continue;
        if (!needle.isEmpty() && !book.value("title").toString().contains(needle, Qt::CaseInsensitive)
            && !book.value("author").toString().contains(needle, Qt::CaseInsensitive))
            continue;
        list << book;
    }

    auto byTitle = [](const QJsonObject &a, const QJsonObject &b) {
        return sortKey(a.value("title").toString()).localeAwareCompare(sortKey(b.value("title").toString())) < 0;
    };
    std::stable_sort(list.begin(), list.end(), byTitle);
    if (m_sort == "author") {
        // Surname order without guessing at name structure: compare the last
        // word, then the whole name.
        auto surname = [](const QJsonObject &b) {
            const QString author = b.value("author").toString().trimmed();
            return author.isEmpty() ? QStringLiteral("￿") : author.section(' ', -1).toLower() + ' ' + author.toLower();
        };
        std::stable_sort(list.begin(), list.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            return surname(a).localeAwareCompare(surname(b)) < 0;
        });
    } else if (m_sort == "added") {
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value("added").toDouble() > b.value("added").toDouble();
        });
    } else if (m_sort == "recent") {
        // Opened books first, newest first; never-opened ones after, newest added first.
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const double ao = a.value("lastOpened").toDouble(), bo = b.value("lastOpened").toDouble();
            if (ao != bo)
                return ao > bo;
            return a.value("added").toDouble() > b.value("added").toDouble();
        });
    }

    QVariantList result;
    for (const auto &book : list)
        result << present(book);
    return result;
}

QVariantList Library::continueReading() const {
    QList<QJsonObject> list;
    for (const auto &book : m_books)
        if (!book.value("finished").toBool() && book.value("lastOpened").toDouble() > 0)
            list << book;
    std::sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("lastOpened").toDouble() > b.value("lastOpened").toDouble();
    });
    QVariantList result;
    for (int i = 0; i < qMin(6, int(list.size())); ++i)
        result << present(list[i]);
    return result;
}

void Library::setShelf(const QString &shelf) {
    if (shelf == m_shelf)
        return;
    m_shelf = shelf;
    emit viewChanged();
}

void Library::setSearch(const QString &search) {
    if (search == m_search)
        return;
    m_search = search;
    emit viewChanged();
}

void Library::setSort(const QString &sort) {
    if (sort == m_sort)
        return;
    m_sort = sort;
    m_settings["sort"] = sort;
    changed();
}

QVariantMap Library::book(const QString &id) const {
    return m_books.contains(id) ? present(m_books.value(id)) : QVariantMap();
}

QVariantList Library::allBooks() const {
    QList<QVariantMap> list;
    for (auto it = m_books.cbegin(); it != m_books.cend(); ++it) {
        const QString path = it.value().value("path").toString();
        if (!QFileInfo::exists(path))
            continue;
        list << QVariantMap{{"id", it.key()}, {"title", it.value().value("title").toString()}, {"path", path},
                            {"format", it.value().value("format").toString()}};
    }
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("title").toString().localeAwareCompare(b.value("title").toString()) < 0;
    });
    QVariantList out;
    for (const QVariantMap &m : std::as_const(list))
        out << m;
    return out;
}

QString Library::pathForId(const QString &id) const { return m_books.value(id).value("path").toString(); }

QUrl Library::readerUrl(const QString &id) const {
    const QJsonObject book = m_books.value(id);
    if (book.value("format").toString() == "pdf")
        return QUrl::fromLocalFile(book.value("path").toString());
    return QUrl("shelf://book/" + id + ".epub");
}

void Library::markOpened(const QString &id) {
    if (!m_books.contains(id))
        return;
    m_books[id]["lastOpened"] = double(now());
    changed();
}

void Library::saveProgress(const QString &id, const QString &location, double progress) {
    if (!m_books.contains(id))
        return;
    QJsonObject &book = m_books[id];
    book["location"] = location;
    book["progress"] = qBound(0.0, progress, 1.0);
    // The grid shows progress, but redrawing it on every page turn while the
    // reader covers it is wasted work; it refreshes when the reader closes.
    changed(false);
}

void Library::setFinished(const QString &id, bool finished) {
    if (!m_books.contains(id))
        return;
    m_books[id]["finished"] = finished;
    changed();
}

void Library::remove(const QString &id) {
    if (!m_books.remove(id))
        return;
    QFile::remove(m_coverDirectory + "/" + id + ".jpg");
    changed();
}

bool Library::createCollection(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_collections.contains(trimmed, Qt::CaseInsensitive))
        return false;
    m_collections << trimmed;
    m_collections.sort(Qt::CaseInsensitive);
    emit collectionsChanged();
    changed(false);
    return true;
}

void Library::deleteCollection(const QString &name) {
    if (!m_collections.removeOne(name))
        return;
    for (auto &book : m_books) {
        QStringList members = book.value("collections").toVariant().toStringList();
        if (members.removeAll(name))
            book["collections"] = QJsonArray::fromStringList(members);
    }
    if (m_shelf == "collection:" + name)
        m_shelf = "all";
    emit collectionsChanged();
    changed();
}

void Library::setInCollection(const QString &id, const QString &name, bool member) {
    if (!m_books.contains(id) || !m_collections.contains(name))
        return;
    QStringList members = m_books[id].value("collections").toVariant().toStringList();
    if (member == members.contains(name))
        return;
    if (member)
        members << name;
    else
        members.removeAll(name);
    m_books[id]["collections"] = QJsonArray::fromStringList(members);
    changed();
}

int Library::addToCollection(const QStringList &ids, const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return 0;
    // An existing collection matches whatever its case.
    QString collection;
    for (const QString &c : std::as_const(m_collections))
        if (c.compare(trimmed, Qt::CaseInsensitive) == 0)
            collection = c;
    if (collection.isEmpty()) {
        createCollection(trimmed);
        collection = trimmed;
    }
    int added = 0;
    for (const QString &id : ids) {
        if (!m_books.contains(id))
            continue;
        QStringList members = m_books[id].value("collections").toVariant().toStringList();
        if (members.contains(collection))
            continue;
        members << collection;
        m_books[id]["collections"] = QJsonArray::fromStringList(members);
        ++added;
    }
    if (added > 0)
        changed();
    return added;
}

void Library::setFinishedMany(const QStringList &ids, bool finished) {
    bool any = false;
    for (const QString &id : ids) {
        if (m_books.contains(id)) {
            m_books[id]["finished"] = finished;
            any = true;
        }
    }
    if (any)
        changed();
}

void Library::removeMany(const QStringList &ids) {
    bool any = false;
    for (const QString &id : ids) {
        if (m_books.remove(id)) {
            QFile::remove(m_coverDirectory + "/" + id + ".jpg");
            any = true;
        }
    }
    if (any)
        changed();
}

QVariant Library::setting(const QString &key, const QVariant &fallback) const {
    return m_settings.contains(key) ? m_settings.value(key).toVariant() : fallback;
}

void Library::setSetting(const QString &key, const QVariant &value) {
    m_settings[key] = QJsonValue::fromVariant(value);
    changed(false);
}
