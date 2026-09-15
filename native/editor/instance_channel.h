#pragma once
#include <QByteArray>
#include <QLocalServer>
#include <QLockFile>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>

class QLocalSocket;
namespace star {
class InstanceChannel final : public QObject {
public:
    explicit InstanceChannel(const QString& profile, QObject* parent = nullptr);
    ~InstanceChannel() override;
    bool primary() const { return primary_; }
    const QString& endpoint() const { return endpoint_; }
    void setHandler(std::function<QByteArray(const QByteArray&)> handler) { handler_ = std::move(handler); }
    void close();
    static QByteArray exchange(const QString& endpoint, const QByteArray& request);
private:
    QLocalServer server_;
    std::unique_ptr<QLockFile> lock_;
    QSet<QLocalSocket*> clients_;
    QString endpoint_;
    QString privateDirectory_;
    bool primary_ = false;
    bool closing_ = false;
    std::function<QByteArray(const QByteArray&)> handler_;
    void accept();
};
}
