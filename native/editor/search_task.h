#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>

namespace star {
class SearchTask final : public QObject {
public:
    explicit SearchTask(QObject* parent = nullptr);
    ~SearchTask() override;
    void start(QByteArray request, std::function<void(QByteArray)> success, std::function<void(QString)> failure,
        const QString& worker = "--search-worker", qsizetype outputLimit = 128 * 1024 * 1024);
    void cancel();
private:
    void fail(const QString& message);
    QProcess process;
    QTimer deadline;
    QByteArray output;
    QByteArray errors;
    bool done = false;
    qsizetype outputBudget = 128 * 1024 * 1024;
    std::function<void(QByteArray)> onSuccess;
    std::function<void(QString)> onFailure;
};
}
