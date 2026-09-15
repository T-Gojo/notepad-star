#include "instance_channel.h"
#include "../shell.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <QtEndian>
#include <stdexcept>
#include <algorithm>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace star {
namespace {
constexpr qsizetype requestLimit = 1024 * 1024;
constexpr qsizetype responseLimit = 256 * 1024;
QByteArray failure(const QString& message) {
    return QJsonDocument(QJsonObject{{"schema", 1}, {"opened", 0}, {"errors", QJsonArray{message}}}).toJson(QJsonDocument::Compact);
}
QByteArray frame(const QByteArray& payload) {
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), bytes.data());
    bytes += payload;
    return bytes;
}
}
InstanceChannel::InstanceChannel(const QString& profile, QObject* parent) : QObject(parent) {
    if (!QDir().mkpath(profile)) throw std::runtime_error("Cannot create the shared-instance profile directory.");
    const auto canonical = QDir(profile).canonicalPath();
    if (canonical.isEmpty()) throw std::runtime_error("Cannot resolve the shared-instance profile.");
    const auto digest = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString lockPath = QDir(canonical).filePath("shared-instance.lock");
#ifdef Q_OS_UNIX
    const auto temporary = QDir(QDir::tempPath()).canonicalPath();
    if (temporary.isEmpty()) throw std::runtime_error("Cannot resolve the local temporary directory.");
    const auto directory = QDir(temporary).filePath("ns-" + QString::fromLatin1(digest.left(16)));
    privateDirectory_ = directory;
    const auto directoryBytes = QFile::encodeName(directory);
    if (::mkdir(directoryBytes.constData(), 0700) != 0 && errno != EEXIST)
        throw std::runtime_error("Cannot create private instance transport storage.");
    struct stat info {};
    if (::lstat(directoryBytes.constData(), &info) != 0 || !S_ISDIR(info.st_mode) ||
        info.st_uid != ::getuid() || (info.st_mode & 0777) != 0700)
        throw std::runtime_error("Instance transport storage must be an owned, non-symlink, mode-0700 directory.");
    endpoint_ = QDir(directory).filePath("s");
    if (QFile::encodeName(endpoint_).size() >= 104)
        throw std::runtime_error("Local socket path is too long. Use a shorter private temporary directory or an independent instance.");
    lockPath = QDir(directory).filePath("lock");
#else
    endpoint_ = "notepad-star-" + QString::fromLatin1(digest.left(32));
#endif
    lock_ = std::make_unique<QLockFile>(lockPath);
    lock_->setStaleLockTime(0);
    const QFileInfo existingLock(lockPath);
    if (existingLock.isSymLink() || (existingLock.exists() && (!existingLock.isFile() || existingLock.size() > 16384)))
        throw std::runtime_error("An unexpected entry occupies the startup lock path; it was retained.");
    if (existingLock.exists()) {
        qint64 pid = 0;
        QString hostname;
        QString application;
#ifdef Q_OS_WIN
        const auto expectedApplication = QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName();
#else
        const auto expectedApplication = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
#endif
        if (!lock_->getLockInfo(&pid, &hostname, &application) || pid <= 0 || application != expectedApplication)
            throw std::runtime_error("The startup lock is incomplete or belongs to another application; it was retained. Retry after startup or inspect it manually.");
    }
    primary_ = lock_->tryLock(0);
    if (!primary_) {
        if (lock_->error() != QLockFile::LockFailedError)
            throw std::runtime_error("Cannot acquire the instance startup lock.");
        return;
    }
#ifdef Q_OS_UNIX
    const auto socketBytes = QFile::encodeName(endpoint_);
    struct stat socketInfo {};
    if (::lstat(socketBytes.constData(), &socketInfo) == 0) {
        if (!S_ISSOCK(socketInfo.st_mode) || socketInfo.st_uid != ::getuid())
            throw std::runtime_error("An unexpected entry occupies the instance socket path; it was retained.");
        if (!QLocalServer::removeServer(endpoint_))
            throw std::runtime_error("Cannot remove the stale owned instance socket.");
    } else if (errno != ENOENT) throw std::runtime_error("Cannot inspect the instance socket.");
#endif
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(8);
    connect(&server_, &QLocalServer::newConnection, this, [this] { accept(); });
    if (!server_.listen(endpoint_)) throw std::runtime_error(("Cannot listen for local launches: " + server_.errorString()).toStdString());
}
InstanceChannel::~InstanceChannel() { close(); }
void InstanceChannel::close() {
    if (closing_) return;
    closing_ = true;
    handler_ = {};
    server_.close();
    const auto clients = clients_;
    for (auto* socket : clients) { socket->abort(); delete socket; }
    clients_.clear();
    if (primary_ && lock_) lock_->unlock();
    if (primary_ && !privateDirectory_.isEmpty() && QDir(privateDirectory_).exists() &&
        !QDir().rmdir(privateDirectory_))
        qWarning("Private instance transport directory was retained because it is nonempty or in use.");
}
void InstanceChannel::accept() {
    while (auto* socket = server_.nextPendingConnection()) {
        if (closing_ || clients_.size() >= 8) { socket->abort(); socket->deleteLater(); continue; }
        clients_.insert(socket);
        socket->setReadBufferSize(requestLimit + 4);
        connect(socket, &QObject::destroyed, this, [this, socket] { clients_.remove(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        auto* timer = new QTimer(socket);
        timer->setTimerType(Qt::PreciseTimer);
        timer->setSingleShot(true); timer->start(5000);
        connect(timer, &QTimer::timeout, socket, [socket] { socket->abort(); socket->deleteLater(); });
        struct State { QByteArray bytes; bool dispatched = false; };
        auto state = std::make_shared<State>();
        auto consume = [this, socket, state] {
            if (state->dispatched || closing_) return;
            state->bytes += socket->readAll();
            QByteArray response;
            if (state->bytes.size() > requestLimit + 4) response = failure("Instance request exceeds its limit.");
            else if (state->bytes.size() < 4) return;
            else {
                const auto size = qFromBigEndian<quint32>(state->bytes.constData());
                if (size > static_cast<quint32>(requestLimit) || size == 0) response = failure("Invalid instance frame length.");
                else if (state->bytes.size() < static_cast<qsizetype>(size) + 4) return;
                else if (state->bytes.size() != static_cast<qsizetype>(size) + 4) response = failure("Only one instance request is permitted per connection.");
                else if (!handler_) response = failure("The existing instance is not ready.");
                else {
                    state->dispatched = true;
                    try { response = handler_(state->bytes.mid(4)); }
                    catch (const std::exception& error) { response = failure(QString::fromUtf8(error.what()).left(2048)); }
                }
            }
            state->dispatched = true;
            if (response.size() > responseLimit) response = failure("Instance response exceeds its limit.");
            const auto bytes = frame(response);
            if (socket->write(bytes) != bytes.size()) socket->abort();
            else socket->disconnectFromServer();
        };
        connect(socket, &QLocalSocket::readyRead, socket, consume);
        consume();
    }
}
QByteArray InstanceChannel::exchange(const QString& endpoint, const QByteArray& request) {
    if (endpoint.isEmpty() || endpoint.size() > 512 || endpoint.contains(QChar(0)) ||
        request.isEmpty() || request.size() > requestLimit)
        throw std::runtime_error("Invalid local launch request.");
    QLocalSocket socket;
    socket.setReadBufferSize(responseLimit + 4);
    socket.connectToServer(endpoint);
    if (!socket.waitForConnected(2000))
        throw std::runtime_error("The existing instance is unavailable or starting. Retry, or use --new-instance.");
    const auto outgoing = frame(request);
    if (socket.write(outgoing) != outgoing.size())
        throw std::runtime_error("Cannot send the complete launch request.");
    QElapsedTimer deadline; deadline.start();
    while (socket.bytesToWrite() > 0) {
        if (deadline.elapsed() >= 2000)
            throw std::runtime_error("Launch transmission timed out; delivery is uncertain.");
        const bool progress = socket.waitForBytesWritten(std::max(1, 2000 - static_cast<int>(deadline.elapsed())));
        // Qt can return false after the write drained and a fast peer already replied/closed.
        // A complete, validated acknowledgement is still required below.
        if (!progress && socket.bytesToWrite() > 0)
            throw std::runtime_error("Launch transmission timed out; delivery is uncertain.");
    }
    QByteArray incoming;
    while (deadline.elapsed() < 4000) {
        incoming += socket.readAll();
        if (incoming.size() > responseLimit + 4) throw std::runtime_error("Oversized instance response.");
        if (incoming.size() >= 4) {
            const auto size = qFromBigEndian<quint32>(incoming.constData());
            if (size == 0 || size > static_cast<quint32>(responseLimit)) throw std::runtime_error("Invalid instance response frame.");
            if (incoming.size() == static_cast<qsizetype>(size) + 4) return incoming.mid(4);
            if (incoming.size() > static_cast<qsizetype>(size) + 4) throw std::runtime_error("Unexpected trailing instance response.");
        }
        if (!socket.waitForReadyRead(std::max(1, 4000 - static_cast<int>(deadline.elapsed()))) && socket.bytesAvailable() == 0) break;
    }
    throw std::runtime_error("No complete acknowledgement arrived; delivery is uncertain. Check the existing window before retrying.");
}
rust::String instance_client(rust::Str endpoint, rust::Str request) {
    int argc = 1;
    char name[] = "notepad-star-instance-client";
    char* argv[] = {name, nullptr};
    std::unique_ptr<QCoreApplication> application;
    if (!QCoreApplication::instance()) application = std::make_unique<QCoreApplication>(argc, argv);
    const auto response = InstanceChannel::exchange(QString::fromUtf8(endpoint.data(), static_cast<qsizetype>(endpoint.size())),
        QByteArray(request.data(), static_cast<qsizetype>(request.size())));
    return rust::String(response.constData(), static_cast<std::size_t>(response.size()));
}
}
