#include "editor_tabs.h"
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QTabBar>
#include <stdexcept>

namespace star {
namespace {
constexpr auto tabMime = "application/x-notepad-star-tab";
class DragBar final : public QTabBar {
public:
    DragBar(EditorTabs* tabsOwner, QWidget* parent) : QTabBar(parent), owner(tabsOwner) {}
protected:
    void mousePressEvent(QMouseEvent* event) override {
        start = event->pos();
        const int index = tabAt(start);
        document = index >= 0 ? tabData(index).toULongLong() : 0;
        QTabBar::mousePressEvent(event);
        if (index >= 0) owner->setCurrentWidget(static_cast<QTabWidget*>(parentWidget())->widget(index));
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if ((event->buttons() & Qt::LeftButton) && document &&
            (event->pos() - start).manhattanLength() >= QApplication::startDragDistance() &&
            (event->pos().y() > height() + 12 || event->pos().y() < -12 ||
                event->pos().x() > width() + 12 || event->pos().x() < -12)) {
            const auto id = document;
            document = 0;
            QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                Qt::LeftButton, Qt::NoButton, event->modifiers());
            QTabBar::mouseReleaseEvent(&release);
            owner->revealDropTargets();
            QDrag drag(this);
            auto* mime = new QMimeData;
            mime->setData(tabMime, QByteArray::number(id));
            drag.setMimeData(mime);
            drag.exec(Qt::MoveAction);
            owner->finishDrag();
            return;
        }
        QTabBar::mouseMoveEvent(event);
    }
private:
    EditorTabs* owner;
    QPoint start;
    quint64 document = 0;
};
class Group final : public QTabWidget {
public:
    Group(EditorTabs* tabsOwner, int groupNumber) : QTabWidget(tabsOwner), owner(tabsOwner), number(groupNumber) {
        setTabBar(new DragBar(tabsOwner, this));
        setAcceptDrops(true);
    }
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (owner->ownsDragSource(event->source()) && event->mimeData()->hasFormat(tabMime)) event->acceptProposedAction();
        else event->ignore();
    }
    void dropEvent(QDropEvent* event) override {
        if (owner->dropTab(event->source(), event->mimeData()->data(tabMime), number)) event->acceptProposedAction();
        else event->ignore();
    }
private:
    EditorTabs* owner;
    int number;
};
}

EditorTabs::EditorTabs(QWidget* parent) : QSplitter(Qt::Horizontal, parent) {
    setHandleWidth(4);
    for (int group = 0; group < 2; ++group) {
        groups[group] = new Group(this, group);
        groups[group]->setObjectName(group == 0 ? "document-tabs-left" : "document-tabs-right");
        addWidget(groups[group]);
        setStretchFactor(group, 1);
        connect(groups[group], &QTabWidget::currentChanged, this, [this, group](int index) {
            if (changing || index < 0) return;
            active = group;
            emit currentChanged(groupStart(group) + index);
        });
        connect(groups[group], &QTabWidget::tabCloseRequested, this, [this, group](int index) {
            emit tabCloseRequested(groupStart(group) + index);
        });
        connect(bar(group), &QTabBar::tabMoved, this, [this] { if (!changing) emit tabOrderChanged(); });
        bar(group)->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar(group), &QTabBar::customContextMenuRequested, this, [this, group](const QPoint& point) {
            const int index = bar(group)->tabAt(point);
            if (index >= 0) emit tabContextRequested(groupStart(group) + index, bar(group)->mapToGlobal(point));
        });
    }
    updateVisibility();
}
std::pair<int, int> EditorTabs::locate(int index) const {
    if (index < 0 || index >= count()) throw std::runtime_error("Tab index is out of range.");
    return index < groupCount(0) ? std::make_pair(0, index) : std::make_pair(1, index - groupCount(0));
}
int EditorTabs::count() const { return groupCount(0) + groupCount(1); }
int EditorTabs::groupStart(int group) const { return group == 0 ? 0 : groupCount(0); }
int EditorTabs::groupCount(int group) const { return groups.at(group)->count(); }
QWidget* EditorTabs::currentInGroup(int group) const { return groups.at(group)->currentWidget(); }
QTabBar* EditorTabs::bar(int group) const { return groups.at(group)->tabBar(); }
QWidget* EditorTabs::currentWidget() const { return currentInGroup(active); }
int EditorTabs::currentIndex() const { return currentWidget() ? indexOf(currentWidget()) : -1; }
QWidget* EditorTabs::widget(int index) const { const auto [group, local] = locate(index); return groups[group]->widget(local); }
int EditorTabs::groupOf(QWidget* page) const {
    for (int group = 0; group < 2; ++group) if (groups[group]->indexOf(page) >= 0) return group;
    return -1;
}
int EditorTabs::indexOf(QWidget* page) const {
    const int group = groupOf(page);
    return group < 0 ? -1 : groupStart(group) + groups[group]->indexOf(page);
}
void EditorTabs::activatePage(QWidget* page) {
    if (changing) return;
    const int group = groupOf(page);
    if (group < 0) return;
    const bool changed = active != group || groups[group]->currentWidget() != page;
    {
        const QScopedValueRollback<bool> guard(changing, true);
        active = group;
        groups[group]->setCurrentWidget(page);
    }
    if (changed) emit currentChanged(currentIndex());
}
void EditorTabs::setCurrentWidget(QWidget* page) {
    activatePage(page);
    if (page && page->focusWidget()) page->focusWidget()->setFocus();
}
void EditorTabs::setCurrentIndex(int index) { setCurrentWidget(widget(index)); }
void EditorTabs::selectInGroup(int group, QWidget* page) {
    if (groupOf(page) != group) throw std::runtime_error("The tab does not belong to this group.");
    const QScopedValueRollback<bool> guard(changing, true);
    groups[group]->setCurrentWidget(page);
}
int EditorTabs::addTab(QWidget* page, const QString& title) {
    int index;
    {
        const QScopedValueRollback<bool> guard(changing, true);
        index = groups[active]->addTab(page, title);
        groups[active]->setCurrentIndex(index);
        updateVisibility();
    }
    emit currentChanged(currentIndex());
    return groupStart(active) + index;
}
void EditorTabs::removeTab(int index) {
    const auto [group, local] = locate(index);
    {
        const QScopedValueRollback<bool> guard(changing, true);
        groups[group]->removeTab(local);
        updateVisibility();
    }
    emit currentChanged(currentIndex());
}
void EditorTabs::setTabText(int index, const QString& title) { const auto [group, local] = locate(index); groups[group]->setTabText(local, title); }
QString EditorTabs::tabText(int index) const { const auto [group, local] = locate(index); return groups[group]->tabText(local); }
void EditorTabs::setTabData(int index, const QVariant& payload) { const auto [group, local] = locate(index); bar(group)->setTabData(local, payload); }
QVariant EditorTabs::tabData(int index) const { const auto [group, local] = locate(index); return bar(group)->tabData(local); }
void EditorTabs::setTabTextColor(int index, const QColor& color) { const auto [group, local] = locate(index); bar(group)->setTabTextColor(local, color); }
void EditorTabs::setTabsClosable(bool enabled) { for (auto* group : groups) group->setTabsClosable(enabled); }
void EditorTabs::setMovable(bool enabled) { for (auto* group : groups) group->setMovable(enabled); }
void EditorTabs::setDocumentMode(bool enabled) { for (auto* group : groups) group->setDocumentMode(enabled); }
void EditorTabs::moveTab(int from, int to) {
    const auto [source, localFrom] = locate(from);
    const auto [destination, localTo] = locate(to);
    if (source != destination) throw std::runtime_error("Use Move to Other View to move between tab groups.");
    {
        const QScopedValueRollback<bool> guard(changing, true);
        bar(source)->moveTab(localFrom, localTo);
    }
    emit tabOrderChanged();
}
void EditorTabs::movePage(QWidget* page, int destination) {
    if (destination < 0 || destination > 1) throw std::runtime_error("Invalid destination view.");
    const int source = groupOf(page);
    if (source < 0) throw std::runtime_error("The tab is no longer open.");
    if (source == destination) { setCurrentWidget(page); return; }
    const int local = groups[source]->indexOf(page);
    const auto title = groups[source]->tabText(local);
    const auto color = bar(source)->tabTextColor(local);
    const auto payload = bar(source)->tabData(local);
    {
        const QScopedValueRollback<bool> guard(changing, true);
        groups[source]->removeTab(local);
        const int moved = groups[destination]->addTab(page, title);
        bar(destination)->setTabData(moved, payload);
        bar(destination)->setTabTextColor(moved, color);
        groups[destination]->setCurrentIndex(moved);
        active = destination;
        updateVisibility();
    }
    emit tabOrderChanged();
    emit currentChanged(currentIndex());
}
void EditorTabs::updateVisibility() {
    groups[0]->setVisible(dragging || groupCount(0) > 0 || count() == 0);
    groups[1]->setVisible(dragging || groupCount(1) > 0);
    if (!groupCount(active)) active = groupCount(0) ? 0 : groupCount(1) ? 1 : 0;
    if (!groups[0]->isHidden() && !groups[1]->isHidden() && (sizes()[0] == 0 || sizes()[1] == 0))
        setSizes({width() / 2, width() / 2});
}
void EditorTabs::revealDropTargets() { dragging = true; updateVisibility(); }
void EditorTabs::finishDrag() { dragging = false; updateVisibility(); }
bool EditorTabs::ownsDragSource(QObject* source) const { return source && (source == bar(0) || source == bar(1)); }
bool EditorTabs::dropTab(QObject* source, const QByteArray& payload, int destination) {
    if (!ownsDragSource(source) || payload.isEmpty() || payload.size() > 20 ||
        destination < 0 || destination > 1 || !tabDropped) return false;
    bool valid = false;
    const auto id = payload.toULongLong(&valid);
    if (!valid || !id) return false;
    tabDropped(id, destination);
    return true;
}
}
