#pragma once
#include <QDockWidget>
#include <QTreeWidget>
#include <QByteArray>
#include <functional>

namespace star {
class ProjectPanel final : public QDockWidget {
public:
    ProjectPanel(const QString& title, QWidget* parent);
    std::function<void(const QString&)> openFile;
    std::function<bool(const QString&)> canSave;
    bool confirmClose();
    void importXml(const QByteArray& xml, const QString& workspacePath);
    QByteArray exportXml(const QString& workspacePath) const;
    QTreeWidget* tree() const { return items; }
private:
    bool save(bool saveAs = false);
    void report(const std::function<void()>& operation);
    void changed();
    QTreeWidget* items = nullptr;
    QString baseTitle;
    QString filename;
    QByteArray expectedHash;
    bool dirty = false;
    bool updating = false;
};
}
