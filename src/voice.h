#pragma once

#include <QList>
#include <QObject>
#include <QVariant>
#include <functional>

class FilePicker;
class Library;
class QNetworkAccessManager;
class QProcess;
class StudioVoice;

// The voices that read lessons aloud, all run on this computer by
// sherpa-onnx's command-line tool: Kokoro and Kokoro HD (closest to a
// person), Supertonic, Kitten, and Piper's best English voices, including
// LibriTTS with 904 speakers. (Piper's own program still runs the two Piper
// voices from earlier versions.) Shelf downloads a voice model only when
// asked, checks every file against a SHA-256 checksum written into Shelf,
// and keeps it in Shelf's data folder; every speaker of a model comes with it.
//
// Speech is made a scene at a time (so it flows rather than starting over
// each sentence), at idle priority, never more than one scene ahead of the
// one playing, and kept, so a lesson heard again costs nothing.
class Voice : public QObject {
    Q_OBJECT
    // "none", "downloading", "ready", "failed", or "unsupported" (no build
    // for this computer).
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // [{id, label, about, size (MB), speakers: [{voice, label}]}]
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    // The voice in use, "<model>/<speaker>" (or a Piper voice's name), and
    // its model.
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(QString currentModel READ currentModel NOTIFY changed)
    // Whether the lesson should play the speech faster or slower itself
    // (sherpa-onnx's tool has no pace setting; Piper's does).
    Q_PROPERTY(bool playbackPace READ playbackPace NOTIFY changed)
    // Installing a studio voice (Chatterbox, XTTS): what it's doing.
    Q_PROPERTY(QString studioStep READ studioStep NOTIFY changed)
    // Whether an NVIDIA graphics card's tools are on this computer, for the
    // studio voices' warning.
    Q_PROPERTY(bool hasNvidia READ hasNvidia CONSTANT)

public:
    explicit Voice(Library *library, const QString &directory = {}, FilePicker *picker = nullptr,
                   QObject *parent = nullptr);
    ~Voice() override;

    QString status() const { return m_status; }
    bool ready() const { return m_status == "ready"; }
    double progress() const;
    QString error() const { return m_error; }
    QVariantList models() const;
    QString current() const { return m_voice; }
    QString currentModel() const;
    bool playbackPace() const;
    QString studioStep() const;
    bool hasNvidia() const;

    // Installs a studio voice ("chatterbox", "xtts") after the user has
    // read the warning (and, for XTTS, agreed to Coqui's licence).
    Q_INVOKABLE void installStudio(const QString &engine);
    // Asks for a recording of a voice for Chatterbox to speak like.
    Q_INVOKABLE void addRecording();
    Q_INVOKABLE void removeRecording(const QString &voiceId);

    // Whether a voice's (or a model's) files are downloaded.
    Q_INVOKABLE bool installed(const QString &voiceOrModel) const;
    // Picks a voice, downloading its model first if it isn't here.
    Q_INVOKABLE void install(const QString &voiceId);
    Q_INVOKABLE void cancelInstall();
    // Deletes every voice.
    Q_INVOKABLE void remove();

    // Speech for a scene of a lesson (or one sentence: `part` is just passed
    // back); spoken(key, scene, part, url) follows. `now`: the lesson is
    // waiting for it; otherwise it's made ahead, after anything waited for.
    Q_INVOKABLE void say(const QString &key, int scene, int part, const QString &text, bool now);
    // Forgets what hasn't been made yet (the lesson closed).
    Q_INVOKABLE void stop();

    // Where made speech is kept; the scheme handler serves it from there.
    static QString speechDirectory();
    // Voice ids from earlier versions ("kokoro-af_heart") in today's form.
    static QString migrate(const QString &voiceId);

    struct Download {
        QString url, sha256, file;
        qint64 size = 0;
    };
    // What has to be downloaded for a voice on this kind of computer.
    static QList<Download> downloadsFor(const QString &voiceId, const QString &platform);
    // The command line that speaks `text` into `output` with a voice.
    QStringList arguments(const QString &voiceId, const QString &text, const QString &output) const;

signals:
    void changed();
    // Chatterbox recordings or XTTS's speakers changed.
    void modelsChanged();
    void spoken(const QString &key, int scene, int part, const QString &url);

private:
    QString piperExecutable() const;
    QString sherpaExecutable() const;
    QString modelDirectory(const QString &model) const;
    QString piperModelFile(const QString &voiceId) const;
    void setStatus(const QString &status, const QString &error = {});
    void downloadNext();
    void unpack();
    void runNext();

    struct Job {
        QString key;
        int scene = 0, part = 0;
        QString text, file;
        bool now = false;
    };

    Library *m_library;
    QString m_directory;
    QNetworkAccessManager *m_network;
    QString m_status = "none", m_error, m_voice;
    double m_progress = 0;
    QList<Download> m_downloads;
    int m_downloadIndex = 0;
    qint64 m_downloadedBytes = 0, m_totalBytes = 0;
    QObject *m_reply = nullptr;
    QList<Job> m_queue;
    QProcess *m_process = nullptr;
    bool m_studioBusy = false;
    Job m_running;
    StudioVoice *m_studio = nullptr;
    FilePicker *m_picker = nullptr;
};
