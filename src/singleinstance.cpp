#include "singleinstance.h"

#include <QLocalSocket>
#include <memory>

SingleInstance::SingleInstance(const QString &name, QObject *parent) : QObject(parent), m_name(name) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            // One message: the paths, newline-separated, ending in a blank
            // line. Collect until then so long lists arrive whole.
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (!buffer->endsWith("\n\n"))
                    return;
                const QString text = QString::fromUtf8(*buffer).trimmed();
                socket->disconnectFromServer();
                emit filesReceived(text.isEmpty() ? QStringList() : text.split('\n'));
            });
        }
    });
}

bool SingleInstance::forwardToRunning(const QStringList &paths) {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(500))
        return false;
    socket.write((paths.join('\n') + "\n\n").toUtf8());
    socket.flush();
    socket.waitForBytesWritten(1000);
    socket.waitForDisconnected(1000);
    return true;
}

bool SingleInstance::isRunning() {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    const bool answered = socket.waitForConnected(300);
    socket.abort();
    return answered;
}

bool SingleInstance::listen() {
    // A crashed instance leaves its socket file behind; nobody answered on
    // it, so it is safe to remove.
    QLocalServer::removeServer(m_name);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    return m_server.listen(m_name);
}
