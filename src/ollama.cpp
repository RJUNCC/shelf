#include "ollama.h"

#include <QTimer>

#include <QProcess>

#include <algorithm>
#include <QThread>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <memory>

Ollama::Ollama(const QString &url, QObject *parent)
    : QObject(parent),
      m_url(!url.isEmpty() ? url
            : qEnvironmentVariableIsSet("SHELF_OLLAMA_URL") ? qEnvironmentVariable("SHELF_OLLAMA_URL")
                                                           : QStringLiteral("http://127.0.0.1:11434")) {}

void Ollama::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Ollama::setModel(const QString &model) {
    if (m_model == model)
        return;
    m_model = model;
    emit modelChanged();
}

void Ollama::setUseGpu(bool useGpu) {
    if (m_useGpu == useGpu)
        return;
    m_useGpu = useGpu;
    m_resident.clear();
    m_testResult.clear();
    emit useGpuChanged();
    emit testChanged();
}

bool Ollama::installed() const {
    if (!QStandardPaths::findExecutable("ollama").isEmpty())
        return true;
#ifdef Q_OS_WIN
    // The Windows installer puts it here without always updating PATH for
    // programs that are already running.
    return QFileInfo::exists(QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
                             + "/Programs/Ollama/ollama.exe");
#else
    return false;
#endif
}

void Ollama::start() {
#ifdef Q_OS_WIN
    const QString app = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + "/Programs/Ollama/ollama app.exe";
    if (QFileInfo::exists(app))
        QProcess::startDetached(app, {});
    else
#endif
    {
        QString program = QStandardPaths::findExecutable("ollama");
#ifdef Q_OS_WIN
        if (program.isEmpty())
            program = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + "/Programs/Ollama/ollama.exe";
#endif
        if (!QFileInfo::exists(program))
            return;
        QProcess::startDetached(program, {"serve"});
    }
    QTimer::singleShot(2500, this, &Ollama::refresh);
}

QJsonObject Ollama::options(const QJsonObject &extra) const {
    QJsonObject out = extra;
    // No layers on the graphics card means the processor does it all.
    if (!m_useGpu)
        out["num_gpu"] = 0;
    // Ollama's default takes every core, which leaves a laptop hot and slow
    // to use. Answers can be slower; the computer must stay pleasant. All
    // of it on the graphics card: two threads keep up. Otherwise a quarter
    // of the cores, never fewer than two.
    if (m_useGpu && m_resident.value(m_model))
        out["num_thread"] = 2;
    else
        out["num_thread"] = std::max(2, QThread::idealThreadCount() / 4);
    return out;
}

void Ollama::checkResidency() {
    if (!m_useGpu || m_model.isEmpty() || m_resident.contains(m_model) || m_checkingResident)
        return;
    m_checkingResident = true;
    QNetworkReply *ps = m_network.get(QNetworkRequest(QUrl(m_url + "/api/ps")));
    connect(ps, &QNetworkReply::finished, this, [this, ps]() {
        ps->deleteLater();
        m_checkingResident = false;
        for (const QJsonValue &m : QJsonDocument::fromJson(ps->readAll()).object().value("models").toArray()) {
            const QJsonObject o = m.toObject();
            if (o.value("name").toString() != m_model && o.value("model").toString() != m_model)
                continue;
            const double size = o.value("size").toDouble(), vram = o.value("size_vram").toDouble();
            m_resident.insert(m_model, size > 0 && vram >= size * 0.99);
        }
    });
}

void Ollama::test() {
    if (m_testing)
        return;
    if (m_model.isEmpty()) {
        m_testResult = {{"ok", false}, {"error", tr("No model is installed in Ollama.")}};
        emit testChanged();
        return;
    }
    m_testing = true;
    m_testResult.clear();
    emit testChanged();
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    const QJsonObject body{{"model", m_model}, {"prompt", "Reply with the word ready."}, {"stream", false},
                           {"think", false}, {"options", options({{"num_predict", 4}})}};
    post("/api/generate", body, [this, timer](const QString &, const QString &error) {
        const double seconds = timer->elapsed() / 1000.0;
        if (!error.isEmpty()) {
            m_testing = false;
            m_testResult = {{"ok", false}, {"error", error}};
            emit testChanged();
            return;
        }
        // Ask where the model was loaded: all, some, or none of it on the
        // graphics card.
        QNetworkReply *ps = m_network.get(QNetworkRequest(QUrl(m_url + "/api/ps")));
        connect(ps, &QNetworkReply::finished, this, [this, ps, seconds]() {
            ps->deleteLater();
            QString processor;
            for (const QJsonValue &m : QJsonDocument::fromJson(ps->readAll()).object().value("models").toArray()) {
                const QJsonObject o = m.toObject();
                if (o.value("name").toString() != m_model && o.value("model").toString() != m_model)
                    continue;
                const double size = o.value("size").toDouble(), vram = o.value("size_vram").toDouble();
                processor = vram <= 0 ? "cpu" : vram >= size * 0.99 ? "gpu" : "mixed";
            }
            m_testing = false;
            if (!processor.isEmpty() && m_useGpu)
                m_resident.insert(m_model, processor == "gpu");
            m_testResult = {{"ok", true}, {"seconds", seconds}, {"processor", processor}};
            emit testChanged();
        });
    }, false);
}

void Ollama::refresh() {
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    QNetworkRequest request(QUrl(m_url + "/api/tags"));
    request.setTransferTimeout(4000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_busy = false;
        m_checked = QDateTime::currentDateTime();
        emit busyChanged();
        if (reply->error() != QNetworkReply::NoError) {
            m_models.clear();
            setStatus(installed() ? "notRunning" : "notInstalled");
            emit statusChanged();
            return;
        }
        QStringList models;
        for (const QJsonValue &m : QJsonDocument::fromJson(reply->readAll()).object().value("models").toArray()) {
            const QString name = m.toObject().value("name").toString();
            // Embedding models cannot answer questions.
            if (!name.isEmpty() && !name.contains("embed", Qt::CaseInsensitive))
                models << name;
        }
        m_models = models;
        // Keep the user's choice if it is still installed; otherwise prefer
        // the suggested model, then whatever is there.
        if (!models.contains(m_model)) {
            QString pick = models.isEmpty() ? QString() : models.first();
            for (const QString &name : std::as_const(models))
                if (name == suggestedModel || name.startsWith(QString(suggestedModel) + "-"))
                    pick = name;
            setModel(pick);
        }
        setStatus(models.isEmpty() ? "noModels" : "ready");
        emit statusChanged();
    });
}

void Ollama::ask(const QString &system, const QString &prompt, const QJsonObject &schema, const Reply &done) {
    if (m_model.isEmpty()) {
        done({}, tr("No model is installed in Ollama."));
        return;
    }
    const QJsonObject body{
        {"model", m_model},
        {"stream", false},
        // Thinking out loud is slow and the answer is all Shelf needs.
        {"think", false},
        {"format", schema},
        {"options", options({{"temperature", 0.1}, {"num_ctx", 8192}})},
        {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}},
                                QJsonObject{{"role", "user"}, {"content", prompt}}}},
    };
    post("/api/chat", body, done, false);
}

void Ollama::post(const QString &path, const QJsonObject &body, const Reply &done, bool retried) {
    QNetworkRequest request(QUrl(m_url + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Loading a model the first time can take a while.
    request.setTransferTimeout(5 * 60 * 1000);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, body, done, retried]() {
        reply->deleteLater();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        const QString error = answer.value("error").toString();
        // Models that cannot think refuse the "think" option; ask again
        // without it.
        if (!retried && error.contains("think", Qt::CaseInsensitive)) {
            QJsonObject plain = body;
            plain.remove("think");
            post(path, plain, done, true);
            return;
        }
        if (reply->error() == QNetworkReply::ConnectionRefusedError
            || reply->error() == QNetworkReply::HostNotFoundError) {
            setStatus(installed() ? "notRunning" : "notInstalled");
            done({}, tr("Ollama is not running."));
            return;
        }
        if (!error.isEmpty() || reply->error() != QNetworkReply::NoError) {
            done({}, !error.isEmpty() ? error : reply->errorString());
            return;
        }
        // /api/chat answers in a message; /api/generate in a response.
        const QString content = answer.contains("message") ? answer.value("message").toObject().value("content").toString()
                                                           : answer.value("response").toString();
        checkResidency();
        done(content, {});
    });
}

QNetworkReply *Ollama::stream(const QString &system, const QString &prompt, const Chunk &chunk, const Reply &done) {
    if (m_model.isEmpty()) {
        done({}, tr("No model is installed in Ollama."));
        return nullptr;
    }
    const QJsonObject body{
        {"model", m_model},
        {"stream", true},
        {"think", false},
        {"options", options({{"temperature", 0.3}, {"num_ctx", 8192}})},
        {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}},
                                QJsonObject{{"role", "user"}, {"content", prompt}}}},
    };
    QNetworkRequest request(QUrl(m_url + "/api/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(5 * 60 * 1000);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // Each line is a JSON object with the next piece of the message.
    auto buffer = std::make_shared<QByteArray>();
    auto answer = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    auto readLines = [reply, buffer, answer, error, chunk]() {
        *buffer += reply->readAll();
        qsizetype end;
        while ((end = buffer->indexOf('\n')) >= 0) {
            const QJsonObject line = QJsonDocument::fromJson(buffer->left(end)).object();
            buffer->remove(0, end + 1);
            if (line.contains("error"))
                *error = line.value("error").toString();
            const QString piece = line.value("message").toObject().value("content").toString();
            if (!piece.isEmpty()) {
                *answer += piece;
                chunk(piece);
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, readLines);
    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, answer, error, readLines, done]() {
        reply->deleteLater();
        readLines();
        if (!buffer->trimmed().isEmpty()) {
            const QJsonObject last = QJsonDocument::fromJson(*buffer).object();
            if (last.contains("error"))
                *error = last.value("error").toString();
        }
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            done(*answer, tr("Stopped."));
            return;
        }
        if (reply->error() == QNetworkReply::ConnectionRefusedError) {
            setStatus(installed() ? "notRunning" : "notInstalled");
            done({}, tr("Ollama is not running."));
            return;
        }
        if (!error->isEmpty() || (reply->error() != QNetworkReply::NoError && answer->isEmpty())) {
            done(*answer, !error->isEmpty() ? *error : reply->errorString());
            return;
        }
        checkResidency();
        done(*answer, {});
    });
    return reply;
}

QVariantList Ollama::recommended() const {
    return {
        QVariantMap{{"name", "qwen3:4b"}, {"size", "2.5 GB"},
                    {"about", tr("Small and quick. For laptops and graphics cards with less than 8 GB.")}},
        QVariantMap{{"name", "qwen3:8b"}, {"size", "5.2 GB"},
                    {"about", tr("Recommended. Good answers on graphics cards with 8 GB or more.")}},
        QVariantMap{{"name", "qwen3:14b"}, {"size", "9.3 GB"},
                    {"about", tr("Best answers, slower. For graphics cards with 12 GB or more.")}},
    };
}

void Ollama::pull(const QString &name) {
    if (!m_pulling.isEmpty() || name.isEmpty())
        return;
    m_pulling = name;
    m_pullProgress = -1;
    m_pullStatus = tr("Starting…");
    m_pullError.clear();
    emit pullChanged();

    QNetworkRequest request(QUrl(m_url + "/api/pull"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_pullReply = m_network.post(request, QJsonDocument(QJsonObject{{"model", name}, {"stream", true}}).toJson());
    auto buffer = std::make_shared<QByteArray>();
    QNetworkReply *reply = m_pullReply;
    // Ollama reports each layer as it downloads: {status, total, completed}.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, buffer]() {
        *buffer += reply->readAll();
        qsizetype end;
        while ((end = buffer->indexOf('\n')) >= 0) {
            const QJsonObject line = QJsonDocument::fromJson(buffer->left(end)).object();
            buffer->remove(0, end + 1);
            if (line.contains("error")) {
                m_pullError = line.value("error").toString();
                continue;
            }
            const QString status = line.value("status").toString();
            const double total = line.value("total").toDouble(), completed = line.value("completed").toDouble();
            m_pullProgress = total > 0 ? completed / total : (status == "success" ? 1 : -1);
            m_pullStatus = status.startsWith("pulling") && total > 0
                ? tr("Downloading… %1 of %2 GB").arg(completed / 1e9, 0, 'f', 1).arg(total / 1e9, 0, 'f', 1)
                : status == "verifying sha256 digest" ? tr("Checking the download…")
                : status == "success" ? tr("Installed.")
                : status.isEmpty() ? m_pullStatus : tr("Preparing…");
            emit pullChanged();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            m_pullError = tr("Download stopped.");
        else if (reply->error() != QNetworkReply::NoError && m_pullError.isEmpty())
            m_pullError = reply->error() == QNetworkReply::ConnectionRefusedError ? tr("Ollama is not running.")
                                                                                   : reply->errorString();
        const bool ok = m_pullError.isEmpty();
        m_pulling.clear();
        emit pullChanged();
        if (ok && m_model.isEmpty())
            setModel(name);
        refresh();
    });
}

void Ollama::cancelPull() {
    if (m_pullReply)
        m_pullReply->abort();
}

QString Ollama::repairLatex(const QString &text) {
    // Form feeds and backspaces never belong in an answer: always \f and \b.
    QString out;
    out.reserve(text.size() + 8);
    // Tabs, returns and newlines are kept unless they sit inside $…$ and
    // run straight into a command's name.
    static const QStringList tCommands{"times", "theta", "tau", "text", "textbf", "textit", "tan", "tanh", "tfrac",
                                       "top", "to", "triangle", "tilde", "therefore"};
    static const QStringList rCommands{"rho", "right", "rightarrow", "rangle", "rm", "rbrace", "rfloor", "rceil"};
    static const QStringList nCommands{"nu", "nabla", "neq", "ne", "neg", "not", "nolimits", "ni", "nmid", "newline"};
    bool inMath = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == '$' && (i == 0 || text[i - 1] != '\\'))
            inMath = !inMath;
        if (c == QChar(0x0c)) {
            out += "\\f";
            continue;
        }
        if (c == QChar(0x08)) {
            out += "\\b";
            continue;
        }
        if (inMath && (c == '\t' || c == '\r' || c == '\n')) {
            int end = i + 1;
            while (end < text.size() && text[end].isLetter() && text[end].unicode() < 128)
                ++end;
            const QString word = text.mid(i + 1, end - i - 1);
            const QStringList &known = c == '\t' ? tCommands : c == '\r' ? rCommands : nCommands;
            const QChar letter = c == '\t' ? 't' : c == '\r' ? 'r' : 'n';
            if (std::any_of(known.begin(), known.end(), [&](const QString &k) { return letter + word == k; })) {
                out += '\\';
                out += letter;
                continue;
            }
        }
        out += c;
    }
    return out;
}
