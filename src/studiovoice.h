#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVariant>
#include <functional>

class QNetworkAccessManager;
class QProcess;

// Studio voices: Chatterbox (Resemble AI) and XTTS-v2 (Coqui), the most
// human-sounding voices that run on this computer. They need an NVIDIA
// graphics card and a lot of room (about 6 GB each, PyTorch included), so
// Shelf installs them only when asked, after a warning, into a private
// Python environment inside its own data folder made with uv: nothing is
// installed for the rest of the system, and removing the voices removes
// all of it.
//
// Once installed, a voice runs as a helper process that loads its model
// onto the graphics card once and then speaks whatever it's sent (one JSON
// line per scene on its input), so only the first scene waits for the
// model. The helper quits after a few idle minutes to give the memory back.
class StudioVoice : public QObject {
    Q_OBJECT

public:
    explicit StudioVoice(const QString &directory, QObject *parent = nullptr);
    ~StudioVoice() override;

    static bool isStudio(const QString &model) { return model == "chatterbox" || model == "xtts"; }

    bool installed(const QString &engine) const;
    bool busy() const { return !m_installing.isEmpty(); }
    QString installing() const { return m_installing; }
    QString step() const { return m_step; }
    double progress() const { return m_progress; }

    // Installs an engine: uv, then Python, PyTorch for the graphics card,
    // the engine, and finally its model, which the helper downloads the
    // first time it starts. finished(engine, error) says how it went.
    void install(const QString &engine);
    void cancel();
    void removeAll();

    // Speaks `text` into `output` (a WAV) with `voice` (a speaker name for
    // XTTS, a reference recording or "default" for Chatterbox); `done`
    // gets an error message, empty when it worked.
    void speak(const QString &engine, const QString &voice, const QString &text, const QString &output,
               const std::function<void(const QString &error)> &done);
    void stopSpeaking();

    // XTTS's speakers as the helper reported them, or the list it ships with.
    QStringList xttsSpeakers() const;
    // The helper program, written next to the environments.
    static QByteArray helperScript();

signals:
    void changed();
    void finished(const QString &engine, const QString &error);

private:
    QString envDirectory(const QString &engine) const;
    QString python(const QString &engine) const;
    QString uv() const;
    QProcessEnvironment environment(const QString &engine) const;
    void runStep(const QString &program, const QStringList &args, const QString &label,
                 const std::function<void()> &next, const QString &workDir = {},
                 const std::function<void(const QString &)> &onError = {});
    void fail(const QString &error);
    void startHelper(const QString &engine);
    void sendNext();

    QString m_directory;
    QNetworkAccessManager *m_network;
    QString m_installing, m_step;
    double m_progress = -1;
    QProcess *m_installer = nullptr;
    QObject *m_download = nullptr;

    struct Request {
        int id = 0;
        QString engine, voice, text, output;
        std::function<void(const QString &)> done;
    };
    QProcess *m_helper = nullptr;
    QString m_helperEngine;
    bool m_helperReady = false;
    QByteArray m_helperBuffer;
    QList<Request> m_requests;   // waiting, the first one sent when the helper is free
    Request m_current;
    bool m_sending = false;
    int m_nextId = 1;
    QObject *m_idle = nullptr;
};
