#pragma once
#include <QSplitter>
#include <QTabWidget>
#include <QColor>
#include <QVariant>
#include <array>
#include <functional>

namespace star {
class EditorTabs final : public QSplitter {
    Q_OBJECT
public:
    explicit EditorTabs(QWidget* parent = nullptr);
    int count() const;
    int currentIndex() const;
    QWidget* currentWidget() const;
    QWidget* widget(int index) const;
    int indexOf(QWidget* page) const;
    int addTab(QWidget* page, const QString& title);
    void removeTab(int index);
    void setCurrentIndex(int index);
    void setCurrentWidget(QWidget* page);
    void activatePage(QWidget* page);
    void setTabText(int index, const QString& title);
    QString tabText(int index) const;
    void setTabData(int index, const QVariant& payload);
    QVariant tabData(int index) const;
    void setTabTextColor(int index, const QColor& color);
    void moveTab(int from, int to);
    void movePage(QWidget* page, int destination);
    void setTabsClosable(bool enabled);
    void setMovable(bool enabled);
    void setDocumentMode(bool enabled);
    int activeGroup() const { return active; }
    int groupOf(QWidget* page) const;
    int groupStart(int group) const;
    int groupCount(int group) const;
    QWidget* currentInGroup(int group) const;
    void selectInGroup(int group, QWidget* page);
    QTabBar* bar(int group) const;
    void revealDropTargets();
    void finishDrag();
    bool ownsDragSource(QObject* source) const;
    bool dropTab(QObject* source, const QByteArray& payload, int destination);
    std::function<void(quint64, int)> tabDropped;
signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void tabOrderChanged();
    void tabContextRequested(int index, const QPoint& globalPosition);
private:
    std::array<QTabWidget*, 2> groups{};
    int active = 0;
    bool changing = false;
    bool dragging = false;
    std::pair<int, int> locate(int index) const;
    void updateVisibility();
};
}
