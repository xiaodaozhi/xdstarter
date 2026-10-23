// src/services/session_instance.cpp

#include "session_instance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

namespace xdstarter {
SessionInstance::SessionInstance(const QString& key, const QString& directory, QObject* parent)
    : QObject(parent),
      endpoint_(QStringLiteral("xdstarter-") +
                QString::fromLatin1(
                    QCryptographicHash::hash((directory + key).toUtf8(), QCryptographicHash::Sha256)
                        .toHex())),
      lock_(QDir(directory).filePath(endpoint_ + QStringLiteral(".lock"))) {
    if (directory.isEmpty() || !QDir().mkpath(directory))
        error_ = QStringLiteral("无法建立当前用户的单例通信目录。");
    // A live owner never becomes stale because it has been idle for 30 seconds.
    lock_.setStaleLockTime(0);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(4);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = server_.nextPendingConnection()) {
            socket->setReadBufferSize(64);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(2000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
            const auto read = [this, socket] {
                if (!socket->canReadLine())
                    return;
                const auto command = socket->readLine(64);
                if (command == QByteArrayLiteral("activate\n")) {
                    emit activationRequested();
                    socket->write("ok\n");
                    socket->flush();
                }
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, socket, read);
            read();
        }
    });
}

SessionInstance::Result SessionInstance::acquire() {
    if (!error_.isEmpty())
        return Result::Failed;
    if (server_.isListening())
        return Result::Primary;
    if (lock_.tryLock(0)) {
        QLocalServer::removeServer(endpoint_);
        if (server_.listen(endpoint_))
            return Result::Primary;
        error_ = server_.errorString();
        lock_.unlock();
        return Result::Failed;
    }
    if (lock_.error() != QLockFile::LockFailedError) {
        error_ = QStringLiteral("无法取得当前用户会话的单例锁。");
        return Result::Failed;
    }
    // The first process can hold the lock while it is still constructing its
    // server. Retry a bounded handshake; failure must never start a second UI.
    for (int attempt = 0; attempt < 20; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(endpoint_);
        if (socket.waitForConnected(100)) {
            qint64 ownerPid = 0;
            QString ownerHost;
            QString ownerApplication;
            if (foregroundGrant_ && lock_.getLockInfo(&ownerPid, &ownerHost, &ownerApplication) &&
                ownerPid > 0)
                foregroundGrant_(ownerPid);
            socket.write("activate\n");
            socket.flush();
            if (socket.waitForReadyRead(2000) && socket.readLine() == QByteArrayLiteral("ok\n"))
                return Result::ActivatedExisting;
            break;
        }
        QThread::msleep(50);
    }
    error_ = QStringLiteral("已有实例尚未响应，未启动第二个实例。");
    return Result::Failed;
}
QString SessionInstance::errorString() const { return error_; }
} // namespace xdstarter
