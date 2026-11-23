#include "updater.h"

#include "library.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QVersionNumber>

#ifndef SHELF_VERSION
#define SHELF_VERSION "0.0.0"
#endif
#ifndef SHELF_REPO
#define SHELF_REPO ""
#endif

namespace {
constexpr qint64 day = 24 * 60 * 60 * 1000;

QString apiBase() {
    // A stand-in server can be used for trying it out.
    const QString override = qEnvironmentVariable("SHELF_UPDATE_API");
    return override.isEmpty() ? QStringLiteral("https://api.github.com") : override;
}
} // namespace

Updater::Updater(Library *library, QObject *parent)
    : QObject(parent), m_library(library), m_network(new QNetworkAccessManager(this)) {
    // Once a day at most, a little after starting, and only if allowed.
    if (autoCheck()) {
        const qint64 last = m_library ? m_library->setting("updateLastCheck", 0).toLongLong() : 0;
        if (QDateTime::currentMSecsSinceEpoch() - last > day)
            QTimer::singleShot(20000, this, [this]() { check(false); });
    }
}

QString Updater::version() const { return QStringLiteral(SHELF_VERSION); }

bool Updater::autoCheck() const { return m_library && m_library->setting("updateAutoCheck", false).toBool(); }

void Updater::setAutoCheck(bool on) {
    if (!m_library || on == autoCheck())
        return;
    m_library->setSetting("updateAutoCheck", on);
    emit autoCheckChanged();
    if (on && m_state.isEmpty())
        check(false);
}

QString Updater::system() const {
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#else
    return QStandardPaths::findExecutable("pacman").isEmpty() ? QString() : QStringLiteral("arch");
#endif
}

bool Updater::canInstall() const {
    const QString cpu = QSysInfo::currentCpuArchitecture();
    return pickAsset(m_release.assets, system(), cpu == "arm64" ? QStringLiteral("aarch64") : cpu) >= 0;
}

int Updater::compareVersions(const QString &a, const QString &b) {
    const auto clean = [](QString v) {
        v = v.trimmed();
        if (v.startsWith('v') || v.startsWith('V'))
            v.remove(0, 1);
        return QVersionNumber::fromString(v).normalized();   // 1.2 is 1.2.0
    };
    return QVersionNumber::compare(clean(a), clean(b));
}

Updater::Release Updater::parseRelease(const QByteArray &json) {
    Release r;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    QString tag = o.value("tag_name").toString().trimmed();
    if (tag.startsWith('v') || tag.startsWith('V'))
        tag.remove(0, 1);
    if (tag.isEmpty() || QVersionNumber::fromString(tag).isNull() || o.value("draft").toBool())
        return r;
    r.version = tag;
    r.notes = o.value("body").toString();
    r.page = o.value("html_url").toString();
    for (const QJsonValue &v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        r.assets << Asset{a.value("name").toString(), a.value("browser_download_url").toString(),
                          qint64(a.value("size").toDouble())};
    }
    r.valid = true;
    return r;
}

QHash<QString, QString> Updater::parseChecksums(const QByteArray &text) {
    // sha256sum's format: "<64 hex>  <name>", or "<hex> *<name>" in binary mode.
    QHash<QString, QString> out;
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(.+?)\\s*$"));
    for (const QString &l : QString::fromUtf8(text).split('\n')) {
        const auto m = line.match(l);
        if (m.hasMatch())
            out.insert(m.captured(2).section('/', -1), m.captured(1).toLower());
    }
    return out;
}

int Updater::pickAsset(const QList<Asset> &assets, const QString &system, const QString &cpu) {
    for (int i = 0; i < assets.size(); ++i) {
        const QString name = assets[i].name.toLower();
        if (name.contains("-debug-"))   // makepkg's separate debug symbols
            continue;
        if (system == "arch" && name.endsWith(".pkg.tar.zst") && (name.contains(cpu) || name.contains("-any.")))
            return i;
        if (system == "windows" && name.endsWith(".exe"))
            return i;
    }
    return -1;
}

void Updater::setState(const QString &state, const QString &error) {
    m_state = state;
    m_error = error;
    emit changed();
}

void Updater::check(bool manual) {
    if (m_state == "checking" || m_state == "downloading" || m_state == "installing")
        return;
    const QString repo = QStringLiteral(SHELF_REPO);
    if (repo.isEmpty()) {
        setState("failed", tr("This build of Shelf doesn't know where its releases are."));
        return;
    }
    setState("checking");
    QNetworkRequest request(QUrl(apiBase() + "/repos/" + repo + "/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Shelf-updater");
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
        reply->deleteLater();
        if (m_library)
            m_library->setSetting("updateLastCheck", QDateTime::currentMSecsSinceEpoch());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // No release published yet.
            setState("upToDate");
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setState(manual ? "failed" : "", tr("Could not reach GitHub: %1").arg(reply->errorString()));
            return;
        }
        const Release r = parseRelease(reply->readAll());
        if (!r.valid) {
            setState(manual ? "failed" : "", tr("GitHub's answer didn't describe a release."));
            return;
        }
        m_release = r;
        const bool newer = compareVersions(r.version, version()) > 0;
        const bool skipped = m_library && m_library->setting("updateSkipped").toString() == r.version;
        if (!newer || (skipped && !manual)) {
            setState("upToDate");
            return;
        }
        setState("available");
        if (!manual)
            emit found(r.version);
    });
}

void Updater::skip() {
    if (m_library)
        m_library->setSetting("updateSkipped", m_release.version);
    setState("upToDate");
}

void Updater::download(const QString &url, const std::function<void(const QByteArray &)> &done, bool track) {
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "Shelf-updater");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    if (track)
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            m_progress = total > 0 ? double(got) / double(total) : 0;
            emit changed();
        });
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setState("failed", tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        done(reply->readAll());
    });
}

void Updater::install() {
    const QString cpu = QSysInfo::currentCpuArchitecture();
    const int index = pickAsset(m_release.assets, system(), cpu == "arm64" ? QStringLiteral("aarch64") : cpu);
    if (index < 0) {
        // Not a system Shelf can update itself on: the release page has
        // the downloads.
        QDesktopServices::openUrl(QUrl(m_release.page));
        return;
    }
    const Asset asset = m_release.assets[index];
    int sums = -1;
    for (int i = 0; i < m_release.assets.size(); ++i)
        if (m_release.assets[i].name == "SHA256SUMS")
            sums = i;
    if (sums < 0) {
        setState("failed", tr("This release has no checksums, so Shelf won't install it. Download it from the "
                              "release page if you trust it."));
        return;
    }
    m_progress = 0;
    setState("downloading");
    download(m_release.assets[sums].url, [this, asset](const QByteArray &text) {
        const QString expected = parseChecksums(text).value(asset.name);
        if (expected.isEmpty()) {
            setState("failed", tr("The checksums don't list %1.").arg(asset.name));
            return;
        }
        download(asset.url, [this, asset, expected](const QByteArray &data) {
            const QString actual = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual != expected) {
                setState("failed", tr("The download doesn't match its checksum, so it was thrown away."));
                return;
            }
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/updates";
            QDir().mkpath(dir);
            const QString path = dir + "/" + asset.name.section('/', -1);
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
                setState("failed", tr("Could not save the download: %1").arg(file.errorString()));
                return;
            }
            installFile(path);
        }, true);
    }, false);
}

void Updater::installFile(const QString &path) {
    setState("installing");
    if (qEnvironmentVariableIsSet("SHELF_UPDATE_DRY_RUN")) {
        // For trying the whole flow without installing anything.
        setState("installed");
        return;
    }
#ifdef Q_OS_WIN
    // The installer replaces Shelf's files, so Shelf closes first; the
    // installer starts it again when it's done.
    if (!QProcess::startDetached(path, {"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART"})) {
        setState("failed", tr("Could not start the installer."));
        return;
    }
    QCoreApplication::quit();
#else
    // pkexec asks for the password in the desktop's own dialog.
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            setState("installed");
        } else {
            const QString output = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            setState("failed", code == 126 || code == 127 ? tr("Installing was cancelled.")
                                                          : tr("pacman could not install it: %1").arg(output));
        }
    });
    process->start("pkexec", {"pacman", "-U", "--noconfirm", path});
#endif
}

void Updater::restart() {
#ifdef Q_OS_WIN
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
#else
    // Started a moment after this one has gone, or it would hand itself
    // over to this instance.
    QProcess::startDetached("sh", {"-c", "sleep 1; exec \"$0\"", QCoreApplication::applicationFilePath()});
#endif
    QCoreApplication::quit();
}
