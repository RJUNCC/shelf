#pragma once

#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <atomic>
#include <functional>
#include <memory>

class Ollama;

// Outlines made by a local model for PDFs that have none, such as scans.
// A book's pages are read on a worker thread (with OCR for pages that are
// only a picture), then the model is shown about thirty pages at a time,
// with hints from the book's printed contents, and names the chapters that
// start on them. Entries appear as each batch comes back, so the outline
// fills in while the book is being read, and it is kept for next time.
class Outlines : public QObject {
    Q_OBJECT
    // Bumps on every change so QML bindings re-read entries() and status().
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit Outlines(Ollama *ollama, const QString &dataDirectory = {}, const QString &cacheDirectory = {},
                      QObject *parent = nullptr);
    ~Outlines() override;

    int revision() const { return m_revision; }

    // [{label, depth, target}], target a page index, like a PDF's own outline.
    Q_INVOKABLE QVariantList entries(const QString &bookId) const;
    // {state, done, total, message}. state is "idle", "reading" (page
    // text), "scanning" (OCR), "thinking" (the model), "done", or "failed".
    Q_INVOKABLE QVariantMap status(const QString &bookId) const;
    Q_INVOKABLE void generate(const QString &bookId, const QString &path, const QString &title);
    Q_INVOKABLE void cancel(const QString &bookId);
    Q_INVOKABLE void clear(const QString &bookId);

    // For tests: answer in place of the model, and skip OCR.
    using Ask = std::function<void(const QString &system, const QString &prompt, const QJsonObject &schema,
                                   const std::function<void(const QString &, const QString &)> &done)>;
    void setAsk(const Ask &ask) { m_ask = ask; }
    void setOcr(bool ocr) { m_ocr = ocr; }
    static constexpr int batchSize = 30;

signals:
    void changed(const QString &bookId);

private:
    struct Job {
        QString path;
        QString title;
        QStringList pages;
        QStringList headings;   // per page, lines printed larger than the body
        QSet<QString> repeated; // headings on many pages, which are furniture
        bool noisy = false;     // a scan with machine-read text: filter headings strictly
        QHash<QString, int> vocabulary; // the book's words, for that filter
        QStringList heads;      // running heads, which are not chapters
        QStringList parts;      // "Part 3: MCAT Biology", from the printed contents
        QStringList hints;      // "title" at [page N], from the printed contents
        QString contents;       // the contents pages' text, for spelling
        QList<int> contentsPages;
        int offset = 0;
        bool hasOffset = false;
        int batch = 0;
        int batches = 0;
        QVariantList entries;
        QVariantMap status;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    };

    void setStatus(const QString &bookId, const QString &state, int done = 0, int total = 0,
                   const QString &message = {});
    void textReady(const QString &bookId, const QStringList &pages, const QStringList &headings);
    void nextBatch(const QString &bookId);
    QString prompt(const Job &job, int first, int last) const;
    void save(const QString &bookId, const QVariantList &entries, bool complete) const;
    bool cachedText(const QString &bookId, QStringList &pages, QStringList &headings) const;
    void cacheText(const QString &bookId, const QStringList &pages, const QStringList &headings) const;
    void touch(const QString &bookId);

    Ollama *m_ollama;
    QString m_dataDirectory;
    QString m_cacheDirectory;
    Ask m_ask;
    bool m_ocr = true;
    QHash<QString, std::shared_ptr<Job>> m_jobs;
    mutable QHash<QString, QVariantList> m_saved;
    QHash<QString, QVariantMap> m_finished;   // the last state of jobs that ended
    int m_revision = 0;
};
