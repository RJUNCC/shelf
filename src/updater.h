#pragma once

#include <QHash>
#include <QObject>
#include <QVariant>
#include <functional>

class Library;
class QNetworkAccessManager;
class QNetworkReply;

// Keeps Shelf up to date from its GitHub releases.
//
// Only when the user allows it (autoCheck), and at most once a day, Shelf
// asks GitHub for the latest release; a check can also be started by hand.
// Nothing about the user is sent. Updating downloads the release's package
// for this system and the SHA256SUMS file published with it, refuses a
// download whose checksum doesn't match, then installs it: on Arch with
// pacman (through pkexec, which asks for the password), on Windows by
// running the installer after Shelf closes. Elsewhere it opens the release
// page instead.
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString latest READ latest NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString pageUrl READ pageUrl NOTIFY changed)
    // "", "checking", "upToDate", "available", "downloading", "installing",
    // "installed", "failed"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // Whether this system can install the update itself (else: release page).
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)

public:
    struct Asset {
        QString name, url;
        qint64 size = 0;
    };
    struct Release {
        QString version, notes, page;
        QList<Asset> assets;
        bool valid = false;
    };

    explicit Updater(Library *library, QObject *parent = nullptr);

    QString version() const;
    QString latest() const { return m_release.version; }
    QString notes() const { return m_release.notes; }
    QString pageUrl() const { return m_release.page; }
    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    bool canInstall() const;
    bool autoCheck() const;
    void setAutoCheck(bool on);

    // `manual`: asked for by the user, so a skipped version still shows.
    Q_INVOKABLE void check(bool manual = true);
    Q_INVOKABLE void install();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void restart();

    // The pieces, for tests.
    static int compareVersions(const QString &a, const QString &b);
    static Release parseRelease(const QByteArray &json);
    static QHash<QString, QString> parseChecksums(const QByteArray &text);
    // The asset to install on this kind of system: "arch" (pacman
    // packages for `cpu`) or "windows" (the installer).
    static int pickAsset(const QList<Asset> &assets, const QString &system, const QString &cpu);

signals:
    void changed();
    void autoCheckChanged();
    // An automatic check found a version the user hasn't skipped.
    void found(const QString &version);

private:
    QString system() const;
    void setState(const QString &state, const QString &error = {});
    void download(const QString &url, const std::function<void(const QByteArray &)> &done, bool track);
    void installFile(const QString &path);

    Library *m_library;
    QNetworkAccessManager *m_network;
    Release m_release;
    QString m_state, m_error;
    double m_progress = 0;
};
