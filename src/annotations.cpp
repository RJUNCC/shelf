#include "annotations.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace {

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

// Book ids are hex; anything else never becomes a file name.
bool validId(const QString &id) {
    static const QRegularExpression hex("^[0-9a-f]{1,64}$");
    return hex.match(id).hasMatch();
}

const QStringList kinds{"highlight", "note", "bookmark"};
const QStringList colors{"yellow", "green", "blue", "pink"};

} // namespace

Annotations::Annotations(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(directory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf/annotations"
                      : directory) {
    QDir().mkpath(m_directory);
}

QString Annotations::pathFor(const QString &bookId) const { return m_directory + "/" + bookId + ".json"; }

QJsonArray Annotations::load(const QString &bookId) const {
    if (!validId(bookId))
        return {};
    auto it = m_cache.constFind(bookId);
    if (it != m_cache.constEnd())
        return *it;
    QJsonArray items;
    QFile file(pathFor(bookId));
    if (file.open(QIODevice::ReadOnly))
        items = QJsonDocument::fromJson(file.readAll()).object().value("annotations").toArray();
    m_cache.insert(bookId, items);
    return items;
}

bool Annotations::save(const QString &bookId, const QJsonArray &items) {
    // Written beside the target and renamed over it, so a crash never
    // leaves half a file of someone's notes.
    QSaveFile file(pathFor(bookId));
    const QJsonObject root{{"version", 1}, {"annotations", items}};
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        m_lastError = file.errorString();
        return false;
    }
    m_cache.insert(bookId, items);
    ++m_revision;
    emit changed(bookId);
    return true;
}

QVariantList Annotations::list(const QString &bookId) const {
    QList<QJsonObject> items;
    for (const auto &value : load(bookId))
        items << value.toObject();
    std::stable_sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("position").toDouble() < b.value("position").toDouble();
    });
    QVariantList out;
    for (const auto &item : items)
        out << item.toVariantMap();
    return out;
}

QVariantMap Annotations::get(const QString &bookId, const QString &id) const {
    for (const auto &value : load(bookId))
        if (value.toObject().value("id").toString() == id)
            return value.toObject().toVariantMap();
    return {};
}

QString Annotations::add(const QString &bookId, const QVariantMap &annotation) {
    if (!validId(bookId))
        return {};
    QJsonObject item = QJsonObject::fromVariantMap(annotation);
    if (!kinds.contains(item.value("kind").toString()))
        item["kind"] = "highlight";
    if (item.value("kind").toString() == "highlight" && !colors.contains(item.value("color").toString()))
        item["color"] = "yellow";
    item["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item["created"] = double(now());
    item["updated"] = double(now());
    QJsonArray items = load(bookId);
    items.append(item);
    // Recorded before saving, so whatever re-reads canUndo() when `changed`
    // fires sees this change.
    record(bookId, {}, item);
    if (!save(bookId, items)) {
        m_undo[bookId].removeLast();
        return {};
    }
    return item.value("id").toString();
}

void Annotations::update(const QString &bookId, const QString &id, const QVariantMap &changes) {
    QJsonArray items = load(bookId);
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items[i].toObject();
        if (item.value("id").toString() != id)
            continue;
        // Identity and position are fixed; only what the user edits changes.
        for (const QString &key : {QStringLiteral("note"), QStringLiteral("color")})
            if (changes.contains(key))
                item[key] = QJsonValue::fromVariant(changes.value(key));
        if (item == items[i].toObject())
            return;
        item["updated"] = double(now());
        const QJsonObject before = items[i].toObject();
        items[i] = item;
        record(bookId, before, item);
        if (!save(bookId, items))
            m_undo[bookId].removeLast();
        return;
    }
}

void Annotations::remove(const QString &bookId, const QString &id) {
    QJsonArray items = load(bookId);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].toObject().value("id").toString() == id) {
            const QJsonObject before = items[i].toObject();
            items.removeAt(i);
            record(bookId, before, {});
            if (!save(bookId, items))
                m_undo[bookId].removeLast();
            return;
        }
    }
}

void Annotations::record(const QString &bookId, const QJsonObject &before, const QJsonObject &after) {
    auto &stack = m_undo[bookId];
    stack.append(Change{before, after});
    // Plenty for second thoughts, without growing forever.
    if (stack.size() > 200)
        stack.removeFirst();
    m_redo.remove(bookId);
}

void Annotations::restore(const QString &bookId, const QString &id, const QJsonObject &state) {
    QJsonArray items = load(bookId);
    int at = -1;
    for (int i = 0; i < items.size(); ++i)
        if (items[i].toObject().value("id").toString() == id)
            at = i;
    if (state.isEmpty() && at >= 0)
        items.removeAt(at);
    else if (!state.isEmpty() && at >= 0)
        items[at] = state;
    else if (!state.isEmpty())
        items.append(state);
    save(bookId, items);
}

QVariantMap Annotations::describe(const Change &change, bool undoing) {
    const QString action = change.before.isEmpty() ? "add" : change.after.isEmpty() ? "remove" : "update";
    const QJsonObject &item = change.before.isEmpty() ? change.after : change.before;
    return {{"action", action}, {"kind", item.value("kind").toString()}, {"undone", undoing},
            {"location", item.value("location").toString()}};
}

QVariantMap Annotations::undo(const QString &bookId) {
    auto &stack = m_undo[bookId];
    if (stack.isEmpty())
        return {};
    const Change change = stack.takeLast();
    const QString id = (change.before.isEmpty() ? change.after : change.before).value("id").toString();
    m_redo[bookId].append(change);
    restore(bookId, id, change.before);
    return describe(change, true);
}

QVariantMap Annotations::redo(const QString &bookId) {
    auto &stack = m_redo[bookId];
    if (stack.isEmpty())
        return {};
    const Change change = stack.takeLast();
    const QString id = (change.before.isEmpty() ? change.after : change.before).value("id").toString();
    m_undo[bookId].append(change);
    restore(bookId, id, change.after);
    return describe(change, false);
}

QString Annotations::markdown(const QString &bookId, const QString &title, const QString &author,
                              const QVariantMap &places) const {
    QString out = "# " + title + "\n";
    if (!author.isEmpty())
        out += "\n*" + author + "*\n";
    QString chapter = QStringLiteral("\x01");  // never a real chapter name
    QString section;
    for (const QVariant &value : list(bookId)) {
        const QVariantMap a = value.toMap();
        const QString kind = a.value("kind").toString();
        const QVariantMap place = places.value(a.value("id").toString()).toMap();
        const QString thisChapter = place.isEmpty() ? a.value("chapter").toString() : place.value("group").toString();
        const QString thisSection = place.value("subgroup").toString();
        if (thisChapter != chapter) {
            chapter = thisChapter;
            section.clear();
            out += "\n## " + (chapter.isEmpty() ? tr("Untitled section") : chapter) + "\n";
        }
        if (thisSection != section) {
            section = thisSection;
            if (!section.isEmpty())
                out += "\n### " + section + "\n";
        }
        out += "\n";
        if (kind == "bookmark") {
            out += "- " + tr("Bookmark") + "\n";
            continue;
        }
        const QString text = a.value("text").toString().trimmed();
        if (!text.isEmpty()) {
            // Blockquote every line so multi-paragraph passages stay quoted.
            for (const QString &line : text.split('\n'))
                out += "> " + line.trimmed() + "\n";
        }
        const QString note = a.value("note").toString().trimmed();
        if (!note.isEmpty())
            out += (text.isEmpty() ? QString() : QStringLiteral("\n")) + note + "\n";
    }
    return out;
}

QString Annotations::exportMarkdown(const QString &bookId, const QString &title, const QString &author,
                                    const QString &directory, const QVariantMap &places) {
    const QString base = directory.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Shelf"
                             : directory;
    if (!QDir().mkpath(base)) {
        m_lastError = tr("Could not create %1").arg(base);
        return {};
    }
    // A file name that every system accepts.
    QString name = title;
    name.replace(QRegularExpression(R"([\\/:*?"<>|\x00-\x1f])"), " ");
    name = name.simplified().left(120);
    if (name.isEmpty())
        name = tr("Notes");
    const QString path = base + "/" + name + " — " + tr("notes") + ".md";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(markdown(bookId, title, author, places).toUtf8()) < 0
        || !file.commit()) {
        m_lastError = file.errorString();
        return {};
    }
    return path;
}

void Annotations::copyToClipboard(const QString &text) const {
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}
