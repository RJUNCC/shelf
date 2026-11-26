#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <functional>

// Talks to Ollama, which runs language models on this computer, so nothing
// in a book ever leaves it. Shelf works without it; the AI features say
// what is missing (Ollama, or a model) instead of failing quietly.
class Ollama : public QObject {
    Q_OBJECT
    // "checking", "notInstalled", "notRunning" (installed, but its server
    // does not answer), "noModels", or "ready".
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY statusChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString url READ url CONSTANT)
    // True while asking; `status` keeps its last answer meanwhile, so what
    // is shown does not flicker.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // When Ollama last answered (or failed to), for "checked at …".
    Q_PROPERTY(QDateTime checked READ checked NOTIFY busyChanged)
    // Run models on the graphics card when there is one (Ollama's choice),
    // or on the processor only, leaving the graphics card free.
    Q_PROPERTY(bool useGpu READ useGpu WRITE setUseGpu NOTIFY useGpuChanged)
    // The last test(): {ok, seconds, processor ("gpu", "cpu", "mixed"), error}.
    Q_PROPERTY(QVariantMap testResult READ testResult NOTIFY testChanged)
    Q_PROPERTY(bool testing READ testing NOTIFY testChanged)
    // Downloading a model: which one, how far (0–1, or −1 before the size
    // is known), what Ollama says it is doing, and why it failed.
    Q_PROPERTY(QString pulling READ pulling NOTIFY pullChanged)
    Q_PROPERTY(double pullProgress READ pullProgress NOTIFY pullChanged)
    Q_PROPERTY(QString pullStatus READ pullStatus NOTIFY pullChanged)
    Q_PROPERTY(QString pullError READ pullError NOTIFY pullChanged)
    // Models Shelf suggests, smallest first: [{name, size, about}].
    Q_PROPERTY(QVariantList recommended READ recommended CONSTANT)

public:
    // The model Shelf suggests: small enough for most graphics cards, good
    // at following instructions and answering in JSON.
    static constexpr const char *suggestedModel = "qwen3:8b";

    // LaTeX in a JSON answer often loses its backslash: "\frac" arrives as
    // a form feed and "rac", "\times" as a tab and "imes". Puts them back.
    static QString repairLatex(const QString &text);

    explicit Ollama(const QString &url = {}, QObject *parent = nullptr);

    QString status() const { return m_status; }
    QStringList models() const { return m_models; }
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QString url() const { return m_url; }
    bool busy() const { return m_busy; }
    bool useGpu() const { return m_useGpu; }
    void setUseGpu(bool useGpu);
    QVariantMap testResult() const { return m_testResult; }
    bool testing() const { return m_testing; }

    // Asks the chosen model for a two-word answer, to show it really works
    // and where it runs.
    Q_INVOKABLE void test();

    QString pulling() const { return m_pulling; }
    double pullProgress() const { return m_pullProgress; }
    QString pullStatus() const { return m_pullStatus; }
    QString pullError() const { return m_pullError; }
    QVariantList recommended() const;
    // Downloads a model into Ollama, then picks it if none was chosen.
    Q_INVOKABLE void pull(const QString &name);
    Q_INVOKABLE void cancelPull();
    // Whether the Ollama program is on this computer, running or not.
    Q_INVOKABLE bool installed() const;
    QDateTime checked() const { return m_checked; }

    // Asks Ollama which models are installed.
    Q_INVOKABLE void refresh();

    // One question with an answer in the JSON shape `schema`. `done` gets
    // the answer, or an empty answer and the reason.
    using Reply = std::function<void(const QString &answer, const QString &error)>;
    void ask(const QString &system, const QString &prompt, const QJsonObject &schema, const Reply &done);

    // Streams a plain-text (Markdown) answer: `chunk` gets each piece as the
    // model writes it, `done` the whole answer or the reason it stopped.
    // The reply can be aborted to stop early.
    using Chunk = std::function<void(const QString &piece)>;
    QNetworkReply *stream(const QString &system, const QString &prompt, const Chunk &chunk, const Reply &done);

signals:
    void statusChanged();
    void modelChanged();
    void busyChanged();
    void useGpuChanged();
    void testChanged();
    void pullChanged();

private:
    void post(const QString &path, const QJsonObject &body, const Reply &done, bool retried);
    QJsonObject options(const QJsonObject &extra = {}) const;
    void setStatus(const QString &status);

    QNetworkAccessManager m_network;
    QString m_url;
    QString m_status = "checking";
    QStringList m_models;
    QString m_model;
    bool m_busy = false;
    bool m_useGpu = true;
    bool m_testing = false;
    QVariantMap m_testResult;
    QString m_pulling;
    double m_pullProgress = -1;
    QString m_pullStatus;
    QString m_pullError;
    QPointer<QNetworkReply> m_pullReply;
    // Models known to sit entirely on the graphics card. Ollama otherwise
    // keeps several processor threads busy waiting on it, so for these
    // Shelf asks for two threads: the same speed on far less power.
    QHash<QString, bool> m_resident;
    bool m_checkingResident = false;
    void checkResidency();
    QDateTime m_checked;
};
