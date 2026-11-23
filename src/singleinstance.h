#pragma once

#include <QLocalServer>
#include <QObject>
#include <QStringList>

// Keeps one Shelf window. A second launch (opening a book from the file
// manager while Shelf runs) hands its files to the first and exits.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(const QString &name, QObject *parent = nullptr);

    // True when another instance took the files; the caller should exit.
    bool forwardToRunning(const QStringList &paths);
    // Starts listening for later launches.
    bool listen();

signals:
    void filesReceived(const QStringList &paths);

private:
    QString m_name;
    QLocalServer m_server;
};
