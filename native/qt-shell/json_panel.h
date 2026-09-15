#pragma once
#include <QWidget>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QSet>
#include <QVector>
#include <functional>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QStackedWidget;
class QGraphicsScene;
class QGraphicsView;
class QPushButton;

namespace star {
class JsonHighlighter;

// Tree/Graph/Pretty selection lives in the main-window toolbar, not inside this panel.
enum class JsonView { Tree = 0, Graph = 1, Pretty = 2 };

class JsonPanel final : public QWidget {
public:
    explicit JsonPanel(QWidget* parent = nullptr);
    void setPreview(const QJsonObject& model, const QString& title, const QByteArray& source, bool reset);
    void setError(const QString& error);
    void setView(JsonView view);
    JsonView view() const { return current; }
    void applyTheme(bool dark);
    int nodeCount() const { return static_cast<int>(nodes.size()); }
    int graphCardCount() const { return cards.size(); }
    int graphRowCount() const;
    QString graphLayoutProblem() const;
    std::function<void(qint64, qint64)> selectSource;
    void selectNode(int index);
    void toggleNode(int index);
private:
    struct Row {
        int node = 0;
        bool container = false;
    };
    QLabel* status;
    QLabel* graphStatus;
    QLineEdit* filter;
    QLineEdit* pointer;
    QTreeWidget* tree;
    QPlainTextEdit* pretty;
    JsonHighlighter* highlighter;
    QStackedWidget* pages;
    QGraphicsScene* scene;
    QGraphicsView* graph;
    QPushButton* copyPretty;
    QPushButton* copyPath;
    QPushButton* copyValue;
    QJsonArray nodes;
    QByteArray source;
    QVector<QTreeWidgetItem*> items;
    QVector<QVector<int>> children;
    QSet<int> expanded;
    QHash<int, QRectF> cards;
    JsonView current = JsonView::Tree;
    bool selecting = false;
    bool darkTheme = false;
    void filterTree();
    void drawGraph(bool fit);
    void fitGraph();
    void expandWithinBudget();
    QVector<Row> rowsOf(int index) const;
    int countCards(const QSet<int>& open) const;
};
}
