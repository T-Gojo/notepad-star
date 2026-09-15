#include "search_task.h"
#include <QCoreApplication>
#include <QProcessEnvironment>

namespace star {
SearchTask::SearchTask(QObject* parent) : QObject(parent) {
    deadline.setSingleShot(true);
    deadline.setInterval(5000);
    connect(&deadline, &QTimer::timeout, this, [this] { fail("Background operation exceeded its 5-second limit."); });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        output += process.readAllStandardOutput();
        if (output.size() > outputBudget) fail("Worker exceeded its output budget.");
    });
    connect(&process, &QProcess::readyReadStandardError, this, [this] {
        errors += process.readAllStandardError();
        if (errors.size() > 65536) fail("Search worker produced excessive diagnostics.");
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!done) fail("Search worker failed: " + process.errorString());
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        if (done) return;
        output += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (code != 0 || status != QProcess::NormalExit) {
            fail("Search failed: " + QString::fromUtf8(errors.left(8192)));
            return;
        }
        if (output.size() > outputBudget) { fail("Worker output exceeds its budget."); return; }
        done = true;
        deadline.stop();
        onSuccess(std::move(output));
    });
}
SearchTask::~SearchTask() {
    done = true;
    process.blockSignals(true);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(2000);
    }
}
void SearchTask::start(QByteArray request, std::function<void(QByteArray)> success, std::function<void(QString)> failure, const QString& worker, qsizetype outputLimit) {
    outputBudget = outputLimit;
    onSuccess = std::move(success);
    onFailure = std::move(failure);
    connect(&process, &QProcess::started, this, [this, request = std::move(request)] {
        if (process.write(request) != request.size()) { fail("Could not send the complete search snapshot."); return; }
        process.closeWriteChannel();
    });
    deadline.start();
    QProcessEnvironment environment;
    const auto inherited = QProcessEnvironment::systemEnvironment();
    for (const auto* key : {"PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "TMPDIR", "HOME", "USERPROFILE",
        "LANG", "LC_ALL", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "XDG_RUNTIME_DIR"})
        if (inherited.contains(key)) environment.insert(key, inherited.value(key));
    process.setProcessEnvironment(environment);
    process.start(QCoreApplication::applicationFilePath(), {worker});
}
void SearchTask::fail(const QString& message) {
    if (done) return;
    done = true;
    deadline.stop();
    if (process.state() != QProcess::NotRunning) process.kill();
    onFailure(message);
}
void SearchTask::cancel() { fail("Search cancelled. No document changes were applied."); }
}
