#pragma once

#include <QHash>
#include <QObject>
#include <QVariant>
#include <atomic>
#include <memory>

// Makes the text version of a PDF (see Reflow) in the background and saves
// it beside the library as an EPUB, to be added to the library and read
// like any other book.
class TextVersions : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit TextVersions(const QString &directory = {}, QObject *parent = nullptr);
    ~TextVersions() override;

    int revision() const { return m_revision; }

    // {state: "idle" | "working" | "done" | "failed", done, total, message, path}
    Q_INVOKABLE QVariantMap status(const QString &bookId) const;
    // `outline` is the book's numbered outline, as the outline panel shows it.
    Q_INVOKABLE void make(const QString &bookId, const QString &pdfPath, const QString &title, const QString &author,
                          const QVariantList &outline);
    Q_INVOKABLE void cancel(const QString &bookId);

signals:
    void changed();
    // The EPUB is written; QML adds it to the library and opens it.
    void finished(const QString &bookId, const QString &path);

private:
    void setStatus(const QString &bookId, const QVariantMap &status);

    QString m_directory;
    QHash<QString, QVariantMap> m_status;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_cancel;
    int m_revision = 0;
};
