#include "textversions.h"

#include "reflow.h"

#include <QDir>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>

namespace {
// One at a time, at low priority: this is background work.
QThreadPool &pool() {
    static QThreadPool *p = [] {
        auto *pool = new QThreadPool;
        pool->setMaxThreadCount(1);
        pool->setThreadPriority(QThread::IdlePriority);
        return pool;
    }();
    return *p;
}
} // namespace

TextVersions::TextVersions(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(!directory.isEmpty() ? directory
                  : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf/text versions") {}

TextVersions::~TextVersions() {
    for (const auto &flag : std::as_const(m_cancel))
        *flag = true;
    pool().waitForDone();
}

QVariantMap TextVersions::status(const QString &bookId) const {
    return m_status.value(bookId, QVariantMap{{"state", "idle"}});
}

void TextVersions::setStatus(const QString &bookId, const QVariantMap &status) {
    m_status.insert(bookId, status);
    ++m_revision;
    emit changed();
}

void TextVersions::make(const QString &bookId, const QString &pdfPath, const QString &title, const QString &author,
                        const QVariantList &outline) {
    if (status(bookId)["state"] == "working")
        return;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancel.insert(bookId, cancelled);
    setStatus(bookId, {{"state", "working"}, {"done", 0}, {"total", 0}});

    QString name = title.isEmpty() ? QStringLiteral("Book") : title;
    name.replace(QRegularExpression(R"([/\\:*?"<>|])"), "-");
    const QString path = QStringLiteral("%1/%2 (text version).epub").arg(m_directory, name.left(120).trimmed());
    QPointer<TextVersions> self(this);
    pool().start([self, bookId, pdfPath, title, author, outline, path, cancelled]() {
        Reflow::Options options{title, author, outline};
        const auto progress = [self, bookId](int done, int total) {
            QMetaObject::invokeMethod(self, [self, bookId, done, total]() {
                if (self && self->status(bookId)["state"] == "working")
                    self->setStatus(bookId, {{"state", "working"}, {"done", done}, {"total", total}});
            }, Qt::QueuedConnection);
        };
        const QByteArray epub = Reflow::build(pdfPath, options, progress, *cancelled);
        QString error;
        if (!*cancelled) {
            if (epub.isEmpty()) {
                error = QObject::tr("This PDF could not be read.");
            } else {
                QDir().mkpath(QFileInfo(path).path());
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(epub) != epub.size() || !file.commit())
                    error = file.errorString();
            }
        }
        QMetaObject::invokeMethod(self, [self, bookId, path, error, cancelled]() {
            if (!self)
                return;
            if (*cancelled)
                self->setStatus(bookId, {{"state", "idle"}});
            else if (!error.isEmpty())
                self->setStatus(bookId, {{"state", "failed"}, {"message", error}});
            else {
                self->setStatus(bookId, {{"state", "done"}, {"path", path}});
                emit self->finished(bookId, path);
            }
        }, Qt::QueuedConnection);
    });
}

void TextVersions::cancel(const QString &bookId) {
    if (const auto flag = m_cancel.value(bookId))
        *flag = true;
}
