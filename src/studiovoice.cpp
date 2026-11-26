#include "studiovoice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// uv makes the private Python environment. Checksums as GitHub publishes
// them for the release.
struct UvBuild {
    const char *platform, *url, *sha;
};
const UvBuild uvBuilds[] = {
    {"linux-x86_64", "https://github.com/astral-sh/uv/releases/download/0.13.10/uv-x86_64-unknown-linux-gnu.tar.gz",
     "bf65ac1a0af10f92c522d91ac096c7c0a39657647b155577e7836c2cbbb4526c"},
    {"linux-aarch64", "https://github.com/astral-sh/uv/releases/download/0.13.10/uv-aarch64-unknown-linux-gnu.tar.gz",
     "14590f644805a6234ffe071154340669e38137f38e60aa9629690119704107d2"},
    {"windows-x86_64", "https://github.com/astral-sh/uv/releases/download/0.13.10/uv-x86_64-pc-windows-msvc.zip",
     "297b3881fe1adfb922becc2c1613202abb41dc83ea5ff43d472aa087ca1f8683"},
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

void lowPriority(QProcess *process) {
#if defined(Q_OS_UNIX)
    process->setChildProcessModifier([] { (void)::nice(10); });
#elif defined(Q_OS_WIN)
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
#endif
}

// XTTS-v2's built-in speakers, used until the helper reports its own list.
const char *xttsDefaultSpeakers[] = {
    "Claribel Dervla", "Daisy Studious", "Gracie Wise", "Tammie Ema", "Alison Dietlinde", "Ana Florence",
    "Annmarie Nele", "Asya Anara", "Brenda Stern", "Gitta Nikolina", "Henriette Usha", "Sofia Hellen",
    "Tammy Grit", "Tanja Adelina", "Vjollca Johnnie", "Andrew Chipper", "Badr Odhiambo", "Dionisio Schuyler",
    "Royston Min", "Viktor Eka", "Abrahan Mack", "Adde Michal", "Baldur Sanjin", "Craig Gutsy", "Damien Black",
    "Gilberto Mathias", "Ilkin Urbano", "Kazuhiko Atallah", "Ludvig Milivoj", "Suad Qasim", "Torcull Diarmuid",
    "Viktor Menelaos", "Zacharie Aimilios", "Nova Hogarth", "Maja Ruoho", "Uta Obando", "Lidiya Szekeres",
    "Chandra MacFarland", "Szofi Granger", "Camilla Holmström", "Lilya Stainthorpe", "Zofija Kendrick",
    "Narelle Moon", "Barbora MacLean", "Alexandra Hisakawa", "Alma María", "Rosemary Okafor", "Ige Behringer",
    "Filip Traverse", "Damjan Chapman", "Wulf Carlevaro", "Aaron Dreschner", "Kumar Dahl", "Eugenio Mataracı",
    "Ferran Simen", "Xavier Hayasaka", "Luis Moray", "Marcos Rudaski",
};

} // namespace

StudioVoice::StudioVoice(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory + "/studio"), m_network(new QNetworkAccessManager(this)) {}

StudioVoice::~StudioVoice() {
    for (QProcess *p : {m_installer, m_helper})
        if (p) {
            p->kill();
            p->waitForFinished(2000);
        }
}

QByteArray StudioVoice::helperScript() {
    // Kept small and plain: it reads one JSON request per line and answers
    // with one JSON line, so Shelf never opens a network port for it.
    return R"PY(# Shelf's studio voice helper. Started by Shelf; reads JSON lines on stdin.
import json, os, sys

engine = sys.argv[1]
warmup = "--warmup" in sys.argv

def say(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()

try:
    import torch, soundfile
    device = "cuda" if torch.cuda.is_available() else "cpu"
    speakers = []
    if engine == "chatterbox":
        from chatterbox.tts import ChatterboxTTS
        model = ChatterboxTTS.from_pretrained(device=device)
        def speak(text, voice, out):
            extra = {}
            if voice and voice != "default" and os.path.exists(voice):
                extra["audio_prompt_path"] = voice
            wav = model.generate(text, **extra)
            soundfile.write(out, wav.squeeze(0).detach().cpu().numpy(), model.sr)
    else:
        from TTS.api import TTS
        model = TTS("tts_models/multilingual/multi-dataset/xtts_v2").to(device)
        speakers = list(getattr(model, "speakers", None) or [])
        def speak(text, voice, out):
            name = voice if voice in speakers else (speakers[0] if speakers else None)
            model.tts_to_file(text=text, speaker=name, language="en", file_path=out)
    say({"ready": True, "device": device, "speakers": speakers})
except Exception as error:
    say({"ready": False, "error": "%s: %s" % (type(error).__name__, error)})
    sys.exit(1)

if warmup:
    sys.exit(0)

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = json.loads(line)
    try:
        speak(request["text"], request.get("voice", ""), request["out"])
        say({"id": request["id"], "ok": True})
    except Exception as error:
        say({"id": request["id"], "ok": False, "error": "%s: %s" % (type(error).__name__, error)})
)PY";
}

QString StudioVoice::envDirectory(const QString &engine) const { return m_directory + "/" + engine + "-env"; }

QString StudioVoice::python(const QString &engine) const {
#ifdef Q_OS_WIN
    return envDirectory(engine) + "/Scripts/python.exe";
#else
    return envDirectory(engine) + "/bin/python";
#endif
}

QString StudioVoice::uv() const {
#ifdef Q_OS_WIN
    const QString name = "uv.exe";
#else
    const QString name = "uv";
#endif
    // The archive may hold uv at its top or in a folder of its own.
    QDirIterator it(m_directory + "/uv", {name}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

bool StudioVoice::installed(const QString &engine) const {
    return QFileInfo::exists(envDirectory(engine) + "/.shelf-ready") && QFileInfo::exists(python(engine));
}

QProcessEnvironment StudioVoice::environment(const QString &engine) const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Everything stays inside Shelf's folder.
    env.insert("UV_CACHE_DIR", m_directory + "/cache");
    env.insert("UV_PYTHON_INSTALL_DIR", m_directory + "/python");
    env.insert("UV_PYTHON_PREFERENCE", "only-managed");
    env.insert("HF_HOME", m_directory + "/models");
    env.insert("TTS_HOME", m_directory + "/models");
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("HF_HUB_DISABLE_TELEMETRY", "1");
    env.insert("GRADIO_ANALYTICS_ENABLED", "False");
    // The user agreed to Coqui's model licence before XTTS was installed.
    if (engine == "xtts")
        env.insert("COQUI_TOS_AGREED", "1");
    return env;
}

void StudioVoice::fail(const QString &error) {
    const QString engine = m_installing;
    m_installing.clear();
    m_step.clear();
    emit changed();
    emit finished(engine, error);
}

void StudioVoice::runStep(const QString &program, const QStringList &args, const QString &label,
                          const std::function<void()> &next, const QString &workDir,
                          const std::function<void(const QString &)> &onError) {
    m_step = label;
    m_progress = -1;
    emit changed();
    auto *process = new QProcess(this);
    m_installer = process;
    lowPriority(process);
    process->setProcessEnvironment(environment(m_installing));
    process->setWorkingDirectory(workDir.isEmpty() ? m_directory : workDir);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto tail = std::make_shared<QString>();
    // The last line it printed shows what it's doing.
    connect(process, &QProcess::readyRead, this, [this, process, label, tail]() {
        const QString out = QString::fromUtf8(process->readAll());
        *tail = (*tail + out).right(4000);
        const QString last = out.trimmed().section('\n', -1).trimmed();
        if (!last.isEmpty()) {
            m_step = label + "  " + last.left(90);
            emit changed();
        }
    });
    connect(process, &QProcess::finished, this, [this, process, label, tail, next, onError](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (m_installer == process)
            m_installer = nullptr;
        if (m_installing.isEmpty())
            return;   // cancelled
        if (status != QProcess::NormalExit || code != 0) {
            const QString error = tr("%1 failed. The last thing it said: %2").arg(label, tail->trimmed().right(500));
            if (onError)
                onError(error);
            else
                fail(error);
            return;
        }
        next();
    });
    process->start(program, args);
}

void StudioVoice::install(const QString &engine) {
    if (busy() || !isStudio(engine))
        return;
    const QString plat = platform();
    const UvBuild *build = nullptr;
    for (const UvBuild &b : uvBuilds)
        if (plat == QLatin1String(b.platform))
            build = &b;
    if (!build) {
        emit finished(engine, tr("Studio voices aren't available for this kind of computer."));
        return;
    }
    m_installing = engine;
    QDir().mkpath(m_directory + "/uv");

    // The steps, last first, each starting the next.
    const auto warmUp = [this, engine]() {
        QFile script(m_directory + "/shelf_voice_helper.py");
        if (!script.open(QIODevice::WriteOnly) || script.write(helperScript()) < 0) {
            fail(tr("Could not write the voice helper."));
            return;
        }
        script.close();
        runStep(python(engine), {m_directory + "/shelf_voice_helper.py", engine, "--warmup"},
                tr("Downloading the voice model (a few GB, once)…"), [this, engine]() {
                    QFile marker(envDirectory(engine) + "/.shelf-ready");
                    marker.open(QIODevice::WriteOnly);
                    marker.close();
                    m_installing.clear();
                    m_step.clear();
                    emit changed();
                    emit finished(engine, {});
                });
    };
    // Chatterbox runs PyTorch 2.9 or later (which knows every recent NVIDIA
    // card, the RTX 50 series included) only on Python 3.14; if its other
    // parts won't install there, Python 3.12 with PyTorch 2.6 still covers
    // the RTX 40 series and older. XTTS is happiest on 3.12.
    auto environmentStep = std::make_shared<std::function<void(const QString &)>>();
    *environmentStep = [this, engine, warmUp, environmentStep](const QString &version) {
        const auto packages = [this, engine, warmUp, version, environmentStep]() {
            QStringList args{"pip", "install", "--python", python(engine), "--torch-backend=auto"};
            if (engine == "chatterbox")
                args << "chatterbox-tts==0.1.7" << "soundfile";
            else
                args << "coqui-tts==0.28.0" << "torch" << "torchaudio" << "soundfile";
            const bool canRetry = engine == "chatterbox" && version == "3.14";
            runStep(uv(), args, tr("Installing PyTorch and the voice (several GB)…"), warmUp, {},
                    canRetry ? std::function<void(const QString &)>([environmentStep](const QString &) {
                        (*environmentStep)("3.12");
                    })
                             : std::function<void(const QString &)>());
        };
        runStep(uv(), {"venv", "--python", version, "--clear", envDirectory(engine)},
                tr("Setting up Python %1…").arg(version), packages);
    };
    const QString firstPython = engine == "chatterbox" ? "3.14" : "3.12";
    if (!uv().isEmpty()) {
        (*environmentStep)(firstPython);
        return;
    }
    // uv first, checked against its checksum.
    m_step = tr("Downloading uv…");
    m_progress = 0;
    emit changed();
    const QString archive = m_directory + "/uv/" + QUrl(build->url).fileName();
    auto file = std::make_shared<QFile>(archive);
    if (!file->open(QIODevice::WriteOnly)) {
        fail(tr("Could not save uv: %1").arg(file->errorString()));
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    QNetworkRequest request{QUrl(build->url)};
    request.setRawHeader("User-Agent", "Shelf");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    m_download = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, hash]() {
        const QByteArray chunk = reply->readAll();
        hash->addData(chunk);
        file->write(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? double(got) / double(total) : -1;
        emit changed();
    });
    const QString sha = build->sha;
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, archive, sha, environmentStep, firstPython]() {
        reply->deleteLater();
        m_download = nullptr;
        const QByteArray rest = reply->readAll();
        hash->addData(rest);
        file->write(rest);
        file->close();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(archive);
            fail(reply->error() == QNetworkReply::OperationCanceledError ? tr("Stopped.")
                                                                          : tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        if (QString::fromLatin1(hash->result().toHex()) != sha) {
            QFile::remove(archive);
            fail(tr("uv didn't match its checksum, so it was thrown away."));
            return;
        }
        runStep("tar", {archive.endsWith(".zip") ? "-xf" : "-xzf", QFileInfo(archive).fileName()},
                tr("Unpacking uv…"), [archive, environmentStep, firstPython, this]() {
                    QFile::remove(archive);
                    if (uv().isEmpty()) {
                        fail(tr("uv wasn't in its archive."));
                        return;
                    }
                    (*environmentStep)(firstPython);
                }, m_directory + "/uv");
    });
}

void StudioVoice::cancel() {
    if (m_installing.isEmpty())
        return;
    if (auto *reply = qobject_cast<QNetworkReply *>(m_download))
        reply->abort();
    if (m_installer)
        m_installer->kill();
    fail(tr("Stopped."));
}

void StudioVoice::removeAll() {
    cancel();
    stopSpeaking();
    if (m_helper) {
        m_helper->kill();
        m_helper->waitForFinished(2000);
    }
    QDir(m_directory).removeRecursively();
    emit changed();
}

QStringList StudioVoice::xttsSpeakers() const {
    QFile f(m_directory + "/xtts-speakers.json");
    if (f.open(QIODevice::ReadOnly)) {
        QStringList out;
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array())
            out << v.toString();
        if (!out.isEmpty())
            return out;
    }
    QStringList out;
    for (const char *name : xttsDefaultSpeakers)
        out << QString::fromUtf8(name);
    return out;
}

void StudioVoice::startHelper(const QString &engine) {
    if (m_helper) {
        m_helper->kill();
        m_helper->waitForFinished(2000);
        m_helper->deleteLater();
    }
    m_helper = new QProcess(this);
    m_helperEngine = engine;
    m_helperReady = false;
    m_helperBuffer.clear();
    lowPriority(m_helper);
    m_helper->setProcessEnvironment(environment(engine));
    m_helper->setWorkingDirectory(m_directory);
    QProcess *helper = m_helper;
    connect(helper, &QProcess::readyReadStandardOutput, this, [this, helper]() {
        if (helper != m_helper)
            return;
        m_helperBuffer += helper->readAllStandardOutput();
        qsizetype end;
        while ((end = m_helperBuffer.indexOf('\n')) >= 0) {
            const QJsonObject line = QJsonDocument::fromJson(m_helperBuffer.left(end)).object();
            m_helperBuffer.remove(0, end + 1);
            if (line.contains("ready")) {
                if (!line.value("ready").toBool()) {
                    const QString error = line.value("error").toString();
                    QList<Request> waiting = m_requests;
                    m_requests.clear();
                    for (const Request &r : waiting)
                        r.done(error);
                    continue;
                }
                m_helperReady = true;
                const QJsonArray speakers = line.value("speakers").toArray();
                if (!speakers.isEmpty()) {
                    QSaveFile f(m_directory + "/xtts-speakers.json");
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(QJsonDocument(speakers).toJson(QJsonDocument::Compact));
                        f.commit();
                    }
                }
                sendNext();
            } else if (line.contains("id") && m_sending && line.value("id").toInt() == m_current.id) {
                m_sending = false;
                const Request done = m_current;
                done.done(line.value("ok").toBool() ? QString() : line.value("error").toString());
                sendNext();
            }
        }
    });
    connect(helper, &QProcess::finished, this, [this, helper]() {
        helper->deleteLater();
        if (helper != m_helper)
            return;
        m_helper = nullptr;
        m_helperReady = false;
        const QString errors = QString::fromUtf8(helper->readAllStandardError()).trimmed().right(400);
        if (m_sending) {
            m_sending = false;
            m_current.done(errors.isEmpty() ? tr("The voice stopped.") : errors);
        }
        QList<Request> waiting = m_requests;
        m_requests.clear();
        for (const Request &r : waiting)
            r.done(tr("The voice stopped."));
    });
    helper->start(python(engine), {m_directory + "/shelf_voice_helper.py", engine});
}

void StudioVoice::sendNext() {
    if (m_sending || !m_helper || !m_helperReady || m_requests.isEmpty())
        return;
    m_current = m_requests.takeFirst();
    m_sending = true;
    const QJsonObject request{{"id", m_current.id}, {"text", m_current.text}, {"voice", m_current.voice},
                              {"out", m_current.output}};
    m_helper->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
}

void StudioVoice::speak(const QString &engine, const QString &voice, const QString &text, const QString &output,
                        const std::function<void(const QString &)> &done) {
    if (!installed(engine)) {
        done(tr("This voice isn't installed."));
        return;
    }
    // The helper gives the graphics card back after a few quiet minutes.
    if (!m_idle) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(5 * 60 * 1000);
        connect(timer, &QTimer::timeout, this, [this]() {
            if (m_helper && !m_sending && m_requests.isEmpty())
                m_helper->closeWriteChannel();   // it finishes reading and quits
        });
        m_idle = timer;
    }
    qobject_cast<QTimer *>(m_idle)->start();
    m_requests << Request{m_nextId++, engine, voice, text, output, done};
    if (!m_helper || m_helperEngine != engine)
        startHelper(engine);
    else
        sendNext();
}

void StudioVoice::stopSpeaking() {
    // What's being made finishes (the model can't be interrupted cleanly);
    // the rest is dropped.
    QList<Request> waiting = m_requests;
    m_requests.clear();
    for (const Request &r : waiting)
        r.done(tr("Stopped."));
}
