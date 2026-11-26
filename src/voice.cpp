#include "voice.h"

#include "library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <algorithm>
#include <iterator>
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// --- sherpa-onnx and its voice models ------------------------------------

const char *sherpaRelease = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.3/";
const char *modelRelease = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/";

// How a model is run: which family of flags it takes.
enum class Kind { Kokoro, Vits };

struct Model {
    const char *id;
    Kind kind;
    const char *folder;   // the archive's folder, and its name without .tar.bz2
    const char *sha;
    qint64 size;
    const char *file;     // a file inside the folder that shows it's unpacked
};
// Checksums as GitHub publishes them for the sherpa-onnx tts-models release.
const Model sherpaModels[] = {
    {"kokoro", Kind::Kokoro, "kokoro-int8-multi-lang-v1_0",
     "72c347230107ef8a31098ec4d468a605244beef5f1ed00db97efa13518800653", 132353410, "model.int8.onnx"},
    {"kokoro-hd", Kind::Kokoro, "kokoro-multi-lang-v1_0",
     "60703707bc05ccd48d60e1eb723f38b07d6e474795a0b6a588d7220400e0f7a4", 349886476, "model.onnx"},
    {"lessac-hd", Kind::Vits, "vits-piper-en_US-lessac-high",
     "8619d204c7005866fe4f420181dfa79622af6a6222389f0b0818d2af31e0db0e", 115545841, "en_US-lessac-high.onnx"},
};

const Model *modelFor(const QString &voiceOrModel) {
    const QString id = voiceOrModel.section('/', 0, 0);
    for (const Model &m : sherpaModels)
        if (id == QLatin1String(m.id))
            return &m;
    return nullptr;
}
int speakerOf(const QString &voiceId) { return voiceId.section('/', 1, 1).toInt(); }

// Kokoro's English speakers, numbered as in kokoro-multi-lang-v1_0.
struct Speaker {
    const char *name, *label;
};
const Speaker kokoroSpeakers[] = {
    {"af_alloy", QT_TRANSLATE_NOOP("Voice", "Alloy (US, woman)")},
    {"af_aoede", QT_TRANSLATE_NOOP("Voice", "Aoede (US, woman)")},
    {"af_bella", QT_TRANSLATE_NOOP("Voice", "Bella (US, woman)")},
    {"af_heart", QT_TRANSLATE_NOOP("Voice", "Heart (US, woman)")},
    {"af_jessica", QT_TRANSLATE_NOOP("Voice", "Jessica (US, woman)")},
    {"af_kore", QT_TRANSLATE_NOOP("Voice", "Kore (US, woman)")},
    {"af_nicole", QT_TRANSLATE_NOOP("Voice", "Nicole (US, woman, soft)")},
    {"af_nova", QT_TRANSLATE_NOOP("Voice", "Nova (US, woman)")},
    {"af_river", QT_TRANSLATE_NOOP("Voice", "River (US, woman)")},
    {"af_sarah", QT_TRANSLATE_NOOP("Voice", "Sarah (US, woman)")},
    {"af_sky", QT_TRANSLATE_NOOP("Voice", "Sky (US, woman)")},
    {"am_adam", QT_TRANSLATE_NOOP("Voice", "Adam (US, man)")},
    {"am_echo", QT_TRANSLATE_NOOP("Voice", "Echo (US, man)")},
    {"am_eric", QT_TRANSLATE_NOOP("Voice", "Eric (US, man)")},
    {"am_fenrir", QT_TRANSLATE_NOOP("Voice", "Fenrir (US, man)")},
    {"am_liam", QT_TRANSLATE_NOOP("Voice", "Liam (US, man)")},
    {"am_michael", QT_TRANSLATE_NOOP("Voice", "Michael (US, man)")},
    {"am_onyx", QT_TRANSLATE_NOOP("Voice", "Onyx (US, man, deep)")},
    {"am_puck", QT_TRANSLATE_NOOP("Voice", "Puck (US, man)")},
    {"am_santa", QT_TRANSLATE_NOOP("Voice", "Santa (US, man)")},
    {"bf_alice", QT_TRANSLATE_NOOP("Voice", "Alice (British, woman)")},
    {"bf_emma", QT_TRANSLATE_NOOP("Voice", "Emma (British, woman)")},
    {"bf_isabella", QT_TRANSLATE_NOOP("Voice", "Isabella (British, woman)")},
    {"bf_lily", QT_TRANSLATE_NOOP("Voice", "Lily (British, woman)")},
    {"bm_daniel", QT_TRANSLATE_NOOP("Voice", "Daniel (British, man)")},
    {"bm_fable", QT_TRANSLATE_NOOP("Voice", "Fable (British, man)")},
    {"bm_george", QT_TRANSLATE_NOOP("Voice", "George (British, man)")},
    {"bm_lewis", QT_TRANSLATE_NOOP("Voice", "Lewis (British, man)")},
};
QString platform() {
#ifdef Q_OS_WIN
    return QSysInfo::currentCpuArchitecture() == "x86_64" ? "windows-x86_64" : QString();
#elif defined(Q_OS_LINUX)
    const QString cpu = QSysInfo::currentCpuArchitecture();
    return cpu == "x86_64" ? "linux-x86_64" : cpu == "arm64" ? "linux-aarch64" : QString();
#else
    return {};
#endif
}

QString sherpaFolder(const QString &platform) {
    if (platform == "linux-x86_64")
        return "sherpa-onnx-v2.0.3-linux-x64-shared";
    if (platform == "linux-aarch64")
        return "sherpa-onnx-v2.0.3-linux-aarch64-shared-cpu";
    if (platform == "windows-x86_64")
        return "sherpa-onnx-v2.0.3-win-x64-shared-MT-Release";
    return {};
}

Voice::Download sherpaProgram(const QString &platform) {
    if (platform == "linux-x86_64")
        return {QString(sherpaRelease) + "sherpa-onnx-v2.0.3-linux-x64-shared.tar.bz2",
                "615b1bf0140bf09051c15fde6579e48543696352ebd477217d2e69b49d9aec13", "kokoro/program.tar.bz2", 29936514};
    if (platform == "linux-aarch64")
        return {QString(sherpaRelease) + "sherpa-onnx-v2.0.3-linux-aarch64-shared-cpu.tar.bz2",
                "c42748225180235bdd4ba10f8c4575b54beb8d5b26a5303bb3051026a0fe4e53", "kokoro/program.tar.bz2", 29516176};
    if (platform == "windows-x86_64")
        return {QString(sherpaRelease) + "sherpa-onnx-v2.0.3-win-x64-shared-MT-Release.tar.bz2",
                "1d2414ac8921f718ba3188782f80475f525a3d82eeb59cef75ceb9360fbf8182", "kokoro/program.tar.bz2", 32180449};
    return {};
}

} // namespace

Voice::Voice(Library *library, const QString &directory, QObject *parent)
    : QObject(parent), m_library(library),
      m_directory(directory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/shelf/voice"
                      : directory),
      m_network(new QNetworkAccessManager(this)) {
    m_voice = migrate(m_library ? m_library->setting("voiceId", "kokoro/3").toString() : QStringLiteral("kokoro/3"));
    if (platform().isEmpty())
        m_status = "unsupported";
    else if (installed(m_voice))
        m_status = "ready";
}

Voice::~Voice() {
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString Voice::migrate(const QString &voiceId) {
    if (voiceId.startsWith("kokoro-") && !voiceId.contains('/')) {
        const QString name = voiceId.mid(7);
        for (int i = 0; i < int(std::size(kokoroSpeakers)); ++i)
            if (name == QLatin1String(kokoroSpeakers[i].name))
                return QStringLiteral("kokoro/%1").arg(i);
        return QStringLiteral("kokoro/3");
    }
    // Piper's medium voices became Lessac HD; the voices Shelf no longer
    // offers (Supertonic, Kitten, LibriTTS, Ryan, Linda, the studio voices)
    // fall back to Kokoro's default.
    if (voiceId.startsWith("en_US-"))
        return QStringLiteral("lessac-hd/0");
    if (!modelFor(voiceId))
        return QStringLiteral("kokoro/3");
    return voiceId;
}

QVariantList Voice::models() const {
    const auto speakers = [](int count, const std::function<QString(int)> &label) {
        QVariantList out;
        for (int i = 0; i < count; ++i)
            out << QVariantMap{{"voice", QString::number(i)}, {"label", label(i)}};
        return out;
    };
    const QVariantList kokoro =
        speakers(int(std::size(kokoroSpeakers)), [](int i) { return tr(kokoroSpeakers[i].label); });
    return {
        QVariantMap{{"id", "kokoro"}, {"label", tr("Kokoro")}, {"size", 160},
                    {"about", tr("Natural and quick; 28 English voices.")}, {"speakers", kokoro}},
        QVariantMap{{"id", "kokoro-hd"}, {"label", tr("Kokoro HD")}, {"size", 380},
                    {"about", tr("The same 28 voices at full quality: the most natural, a little slower to make.")},
                    {"speakers", kokoro}},
        QVariantMap{{"id", "lessac-hd"}, {"label", tr("Lessac (light)")}, {"size", 145},
                    {"about", tr("A clear, even reader; lighter and quicker, for older computers.")},
                    {"speakers", speakers(1, [](int) { return tr("Lessac (US, woman)"); })}},
    };
}

QString Voice::currentModel() const { return m_voice.section('/', 0, 0); }

QString Voice::speechDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/speech";
}

// sherpa-onnx and its models live in voice/kokoro/ (the folder Shelf used
// when Kokoro was its only sherpa-onnx voice, so nothing is fetched again).
QString Voice::sherpaExecutable() const {
#ifdef Q_OS_WIN
    return m_directory + "/kokoro/" + sherpaFolder(platform()) + "/bin/sherpa-onnx-offline-tts.exe";
#else
    return m_directory + "/kokoro/" + sherpaFolder(platform()) + "/bin/sherpa-onnx-offline-tts";
#endif
}

QString Voice::modelDirectory(const QString &model) const {
    const Model *m = modelFor(model);
    return m ? m_directory + "/kokoro/" + m->folder : QString();
}

bool Voice::installed(const QString &voiceOrModel) const {
    const Model *m = modelFor(voiceOrModel);
    return m && QFileInfo::exists(sherpaExecutable()) && QFileInfo::exists(modelDirectory(m->id) + "/" + m->file);
}

QList<Voice::Download> Voice::downloadsFor(const QString &voiceId, const QString &platform) {
    QList<Download> out;
    // Only "kokoro-<name>" ids are carried over here, so an unknown voice
    // is refused rather than quietly swapped for another.
    const Model *m = modelFor(voiceId.startsWith("kokoro-") && !voiceId.contains('/') ? migrate(voiceId) : voiceId);
    const Download program = sherpaProgram(platform);
    if (!m || program.url.isEmpty())
        return {};
    out << program
        << Download{QString(modelRelease) + m->folder + ".tar.bz2", m->sha, QStringLiteral("kokoro/%1.tar.bz2").arg(m->id),
                    m->size};
    return out;
}

QStringList Voice::arguments(const QString &voiceId, const QString &text, const QString &output) const {
    const Model *m = modelFor(voiceId);
    if (!m)
        return {};
    const QString dir = modelDirectory(m->id);
    const int sid = speakerOf(voiceId);
    QStringList args{"--debug=0"};
    switch (m->kind) {
    case Kind::Kokoro: {
        // British voices read better with the British lexicon.
        const bool british = sid >= 20 && sid < int(std::size(kokoroSpeakers));
        args << "--kokoro-model=" + dir + "/" + m->file << "--kokoro-voices=" + dir + "/voices.bin"
             << "--kokoro-tokens=" + dir + "/tokens.txt" << "--kokoro-data-dir=" + dir + "/espeak-ng-data"
             << "--kokoro-lexicon=" + dir + (british ? "/lexicon-gb-en.txt" : "/lexicon-us-en.txt");
        break;
    }
    case Kind::Vits:
        args << "--vits-model=" + dir + "/" + m->file << "--vits-tokens=" + dir + "/tokens.txt"
             << "--vits-data-dir=" + dir + "/espeak-ng-data";
        break;
    }
    // Two threads at idle priority: a scene is ready before it's needed,
    // and anything else the computer does comes first.
    args << "--num-threads=2" << "--sid=" + QString::number(sid) << "--output-filename=" + output << text;
    return args;
}

void Voice::setStatus(const QString &status, const QString &error) {
    m_status = status;
    m_error = error;
    emit changed();
}

void Voice::install(const QString &voiceIdIn) {
    const QString voiceId = migrate(voiceIdIn);
    if (m_status == "downloading" || m_status == "unsupported")
        return;
    const QList<Download> all = downloadsFor(voiceId, platform());
    if (all.isEmpty()) {
        setStatus("failed", tr("Shelf doesn't know that voice."));
        return;
    }
    stop();
    // What's already here isn't fetched again.
    m_downloads.clear();
    for (const Download &d : all) {
        bool present = QFileInfo::exists(m_directory + "/" + d.file);
        if (d.file == "kokoro/program.tar.bz2")
            present = QFileInfo::exists(sherpaExecutable());
        else if (d.file.startsWith("kokoro/"))
            present = installed(voiceId);
        if (!present)
            m_downloads << d;
    }
    m_voice = voiceId;
    if (m_library)
        m_library->setSetting("voiceId", voiceId);
    m_downloadIndex = 0;
    m_downloadedBytes = 0;
    m_totalBytes = 0;
    for (const Download &d : m_downloads)
        m_totalBytes += d.size;
    m_progress = 0;
    QDir().mkpath(m_directory + "/kokoro");
    if (m_downloads.isEmpty()) {
        setStatus("ready");
        return;
    }
    setStatus("downloading");
    downloadNext();
}

void Voice::downloadNext() {
    if (m_downloadIndex >= m_downloads.size()) {
        if (installed(m_voice))
            setStatus("ready");
        else
            setStatus("failed", tr("The voice didn't install completely. Try again."));
        return;
    }
    const Download d = m_downloads[m_downloadIndex];
    const QString target = m_directory + "/" + d.file;
    auto file = std::make_shared<QFile>(target + ".part");
    if (!file->open(QIODevice::WriteOnly)) {
        setStatus("failed", tr("Could not save the voice: %1").arg(file->errorString()));
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    QNetworkRequest request{QUrl(d.url)};
    request.setRawHeader("User-Agent", "Shelf");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    const qint64 before = m_downloadedBytes;
    // Written as it arrives, so a big voice never sits in memory.
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, hash]() {
        const QByteArray chunk = reply->readAll();
        hash->addData(chunk);
        file->write(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, before](qint64 got, qint64) {
        m_progress = m_totalBytes > 0 ? qMin(1.0, double(before + got) / double(m_totalBytes)) : 0;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, d, target]() {
        reply->deleteLater();
        m_reply = nullptr;
        const QByteArray rest = reply->readAll();
        hash->addData(rest);
        file->write(rest);
        file->close();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(file->fileName());
            if (reply->error() == QNetworkReply::OperationCanceledError)
                setStatus("none");
            else
                setStatus("failed", tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        if (QString::fromLatin1(hash->result().toHex()) != d.sha256) {
            QFile::remove(file->fileName());
            setStatus("failed", tr("A downloaded file didn't match its checksum, so it was thrown away."));
            return;
        }
        QFile::remove(target);
        QFile::rename(file->fileName(), target);
        m_downloadedBytes += d.size;
        if (d.file.endsWith(".tar.gz") || d.file.endsWith(".tar.bz2") || d.file.endsWith(".zip")) {
            unpack();
        } else {
            ++m_downloadIndex;
            downloadNext();
        }
    });
}

void Voice::unpack() {
    // tar unpacks them all: GNU tar the Linux archives, and Windows' own tar
    // (bsdtar, in every Windows 10 and 11) the .zip and .tar.bz2.
    const Download d = m_downloads[m_downloadIndex];
    const QString archive = m_directory + "/" + d.file;
    auto *process = new QProcess(this);
    process->setWorkingDirectory(QFileInfo(archive).absolutePath());
    connect(process, &QProcess::finished, this, [this, process, archive](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        QFile::remove(archive);
        if (status != QProcess::NormalExit || code != 0) {
            setStatus("failed", tr("Could not unpack the voice: %1")
                                    .arg(QString::fromLocal8Bit(process->readAllStandardError()).trimmed()));
            return;
        }
        ++m_downloadIndex;
        downloadNext();
    });
    const QString flags = d.file.endsWith(".zip") ? "-xf" : d.file.endsWith(".tar.bz2") ? "-xjf" : "-xzf";
    process->start("tar", {flags, QFileInfo(archive).fileName()});
}

void Voice::cancelInstall() {
    if (auto *reply = qobject_cast<QNetworkReply *>(m_reply))
        reply->abort();
}

void Voice::remove() {
    stop();
    if (m_status == "downloading")
        cancelInstall();
    QDir(m_directory).removeRecursively();
    QDir(speechDirectory()).removeRecursively();
    setStatus(platform().isEmpty() ? "unsupported" : "none");
}

void Voice::say(const QString &key, int scene, int part, const QString &textIn, bool now) {
    const QString text = textIn.simplified();
    if (!ready() || text.isEmpty()) {
        if (now)
            emit spoken(key, scene, part, {});
        return;
    }
    // Speech is made at the voice's own pace; the lesson plays it faster or
    // slower. ("|1|" keeps the names speech made by earlier versions had.)
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData((m_voice + "|1|" + text).toUtf8());
    const QString name = QString::fromLatin1(hash.result().toHex()) + ".wav";
    const QString file = speechDirectory() + "/" + name;
    if (QFileInfo::exists(file)) {
        if (now)
            emit spoken(key, scene, part, "shelf://app/voice/" + name);
        return;
    }
    // Waiting on another lesson's speech is pointless now.
    if (now)
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&](const Job &j) { return j.key != key; }),
                      m_queue.end());
    for (Job &j : m_queue)
        if (j.file == file) {
            j.now = j.now || now;
            j.key = key;
            j.scene = scene;
            j.part = part;
            return;
        }
    if (m_process && m_running.file == file) {
        m_running.now = m_running.now || now;
        m_running.key = key;
        m_running.scene = scene;
        m_running.part = part;
        return;
    }
    const Job job{key, scene, part, text, file, now};
    if (now)
        m_queue.prepend(job);
    else
        m_queue.append(job);
    runNext();
}

void Voice::stop() {
    m_queue.clear();
}

void Voice::runNext() {
    if (m_process || m_queue.isEmpty())
        return;
    m_running = m_queue.takeFirst();
    QDir().mkpath(speechDirectory());
    m_process = new QProcess(this);
#if defined(Q_OS_UNIX)
    m_process->setChildProcessModifier([] { (void)::nice(19); });
#elif defined(Q_OS_WIN)
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= IDLE_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
    const QString program = sherpaExecutable();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("OMP_NUM_THREADS", "2");
#ifdef Q_OS_LINUX
    // sherpa-onnx's libraries sit in lib/ beside bin/.
    env.insert("LD_LIBRARY_PATH", QFileInfo(program).absolutePath() + "/../lib");
#endif
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(QFileInfo(program).absolutePath());
    const QString part = m_running.file + ".part.wav";
    connect(m_process, &QProcess::finished, this, [this, part](int code, QProcess::ExitStatus status) {
        const Job job = m_running;
        const QString errors = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
        m_process->deleteLater();
        m_process = nullptr;
        const bool made = status == QProcess::NormalExit && code == 0 && QFileInfo(part).size() > 44;
        if (made) {
            QFile::remove(job.file);
            QFile::rename(part, job.file);
        } else {
            QFile::remove(part);
            if (!errors.isEmpty())
                qWarning("voice: %s", qPrintable(errors.left(400)));
        }
        if (job.now)
            emit spoken(job.key, job.scene, job.part,
                        made ? "shelf://app/voice/" + QFileInfo(job.file).fileName() : QString());
        runNext();
    });
    m_process->start(program, arguments(m_voice, m_running.text, part));
    m_process->closeWriteChannel();
}
