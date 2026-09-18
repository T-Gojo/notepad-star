#include "json_panel.h"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace star {
namespace {
constexpr int cardLimit = 400;
constexpr qreal cardWidth = 310;
constexpr qreal headerHeight = 28;
constexpr qreal rowHeight = 23;
constexpr qreal cardPadding = 9;
constexpr qreal columnGap = 120;
constexpr qreal verticalGap = 28;

struct Scheme {
    QColor background, card, edge, header, headerText, key, string, number, literal, badge, link, divider;
};

Scheme scheme(bool dark) {
    if (dark)
        return {QColor("#171a21"), QColor("#232834"), QColor("#3c4657"), QColor("#2b3140"), QColor("#e7ecf7"),
            QColor("#7fb2ff"), QColor("#8fd694"), QColor("#d7a4f2"), QColor("#f0a878"), QColor("#9aa7bd"),
            QColor("#5a7ea8"), QColor("#333b49")};
    return {QColor("#f7f9fd"), QColor("#ffffff"), QColor("#c9d5e6"), QColor("#eef3fb"), QColor("#1f2c3f"),
        QColor("#1f5fbf"), QColor("#1f7a45"), QColor("#7a3fb5"), QColor("#b4531a"), QColor("#5a6a80"),
        QColor("#9bb2cd"), QColor("#e2e9f3")};
}

QString oneLine(const QString& value) {
    return QString(value).replace('\n', "\\n").replace('\r', "\\r").replace('\t', "\\t");
}

class GraphView final : public QGraphicsView {
public:
    explicit GraphView(QGraphicsScene* graphScene, QWidget* parent) : QGraphicsView(graphScene, parent) {
        setDragMode(ScrollHandDrag);
        setTransformationAnchor(AnchorUnderMouse);
        setRenderHint(QPainter::Antialiasing);
        setAccessibleName("JSON graph. Drag to pan; use the mouse wheel or the zoom buttons to zoom.");
    }
    void wheelEvent(QWheelEvent* event) override {
        const auto factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const auto zoom = transform().m11() * factor;
        if (zoom >= 0.1 && zoom <= 3.0) scale(factor, factor);
        event->accept();
    }
};

class ClickItem final : public QGraphicsRectItem {
public:
    std::function<void()> activate;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        if (activate) activate();
        event->accept();
    }
};
}

class JsonHighlighter final : public QSyntaxHighlighter {
public:
    explicit JsonHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
    void setScheme(const Scheme& colors) {
        key.setForeground(colors.key);
        key.setFontWeight(QFont::DemiBold);
        string.setForeground(colors.string);
        number.setForeground(colors.number);
        literal.setForeground(colors.literal);
        literal.setFontWeight(QFont::DemiBold);
        punctuation.setForeground(colors.badge);
        rehighlight();
    }
protected:
    void highlightBlock(const QString& text) override {
        static const QRegularExpression strings(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\""));
        static const QRegularExpression numbers(
            QStringLiteral("-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?"));
        static const QRegularExpression literals(QStringLiteral("\\b(?:true|false|null)\\b"));
        for (qsizetype index = 0; index < text.size(); ++index)
            if (QStringLiteral("{}[],:").contains(text.at(index))) setFormat(static_cast<int>(index), 1, punctuation);
        QVector<QPair<int, int>> quoted;
        auto stringMatches = strings.globalMatch(text);
        while (stringMatches.hasNext()) {
            const auto match = stringMatches.next();
            const int start = static_cast<int>(match.capturedStart());
            const int length = static_cast<int>(match.capturedLength());
            quoted.append({start, length});
            // A quoted run followed by a colon is an object key, not a string value.
            qsizetype after = match.capturedEnd();
            while (after < text.size() && text.at(after).isSpace()) ++after;
            setFormat(start, length, after < text.size() && text.at(after) == ':' ? key : string);
        }
        const auto outsideStrings = [&quoted](int start, int length) {
            for (const auto& range : quoted)
                if (start < range.first + range.second && range.first < start + length) return false;
            return true;
        };
        for (const auto* expression : {&numbers, &literals}) {
            auto matches = expression->globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                const int start = static_cast<int>(match.capturedStart());
                const int length = static_cast<int>(match.capturedLength());
                if (outsideStrings(start, length))
                    setFormat(start, length, expression == &numbers ? number : literal);
            }
        }
    }
private:
    QTextCharFormat key, string, number, literal, punctuation;
};

namespace {
// One-line status text: the panel is narrow, so wrapping would spend several rows of
// preview height on chrome. The full text stays available through text() and the tooltip.
class CompactLabel final : public QLabel {
public:
    explicit CompactLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
        setTextFormat(Qt::PlainText);
        setWordWrap(false);
        setToolTip(text);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        QFont smaller = font();
        if (smaller.pointSizeF() > 0) {
            smaller.setPointSizeF(std::max(7.0, smaller.pointSizeF() - 0.5));
            setFont(smaller);
        }
    }
    QSize sizeHint() const override { return {0, fontMetrics().height() + 2}; }
    QSize minimumSizeHint() const override { return {0, fontMetrics().height() + 2}; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        const QString flat = QString(text()).replace('\n', QStringLiteral(" \xE2\x80\xA2 "));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
            fontMetrics().elidedText(flat, Qt::ElideRight, rect().width()));
    }
};

CompactLabel* plainLabel(const QString& text, QWidget* parent) {
    return new CompactLabel(text, parent);
}

void showStatus(QLabel* label, const QString& text) {
    label->setText(text);
    label->setToolTip(text);
}

QPushButton* compactButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setMaximumHeight(22);
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return button;
}
}

JsonPanel::JsonPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("json-inspector");
    setMinimumWidth(320);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    status = plainLabel("JSON stays on this device. Edit the active tab to refresh the preview.", this);
    status->setObjectName("json-status");
    layout->addWidget(status);
    pages = new QStackedWidget(this);
    pages->setObjectName("json-pages");
    layout->addWidget(pages, 1);

    auto* treePage = new QWidget;
    auto* treeLayout = new QVBoxLayout(treePage);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(4);
    filter = new QLineEdit;
    filter->setPlaceholderText("Find a key, value, or JSON pointer");
    filter->setAccessibleName("Filter JSON tree");
    filter->setClearButtonEnabled(true);
    filter->setMaximumHeight(24);
    treeLayout->addWidget(filter);
    tree = new QTreeWidget;
    tree->setObjectName("json-tree");
    tree->setHeaderLabels({"Key / index", "Type", "Value"});
    tree->setColumnWidth(0, 160);
    tree->setColumnWidth(1, 70);
    tree->setUniformRowHeights(true);
    treeLayout->addWidget(tree, 1);
    pages->addWidget(treePage);

    auto* graphPage = new QWidget;
    auto* graphLayout = new QVBoxLayout(graphPage);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    graphLayout->setSpacing(4);
    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(4);
    scene = new QGraphicsScene(this);
    graph = new GraphView(scene, graphPage);
    graph->setObjectName("json-graph");
    for (const auto& title : {QString("Zoom in"), QString("Zoom out"), QString("Fit"), QString("Collapse")}) {
        auto* button = compactButton(title, graphPage);
        button->setFlat(true);
        controls->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, title] {
            if (title == "Fit") fitGraph();
            else if (title == "Collapse") {
                expanded.clear();
                for (const int child : children.isEmpty() ? QVector<int>() : children[0])
                    if (!children[child].isEmpty()) expanded.insert(child);
                drawGraph(true);
            } else {
                const auto factor = title == "Zoom in" ? 1.2 : 1.0 / 1.2;
                const auto zoom = graph->transform().m11() * factor;
                if (zoom >= 0.1 && zoom <= 3.0) graph->scale(factor, factor);
            }
        });
    }
    // The graph hint shares the zoom row instead of claiming a row of its own.
    graphStatus = plainLabel("Double-click a nested key to expand or collapse it. Drag empty space to pan.", graphPage);
    controls->addWidget(graphStatus, 1);
    graphLayout->addLayout(controls);
    graphLayout->addWidget(graph, 1);
    pages->addWidget(graphPage);

    auto* prettyPage = new QWidget;
    auto* prettyLayout = new QVBoxLayout(prettyPage);
    prettyLayout->setContentsMargins(0, 0, 0, 0);
    prettyLayout->setSpacing(4);
    pretty = new QPlainTextEdit;
    pretty->setObjectName("json-pretty-preview");
    pretty->setReadOnly(true);
    pretty->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pretty->setLineWrapMode(QPlainTextEdit::NoWrap);
    highlighter = new JsonHighlighter(pretty->document());
    prettyLayout->addWidget(pretty, 1);
    auto* prettyRow = new QHBoxLayout;
    prettyRow->setContentsMargins(0, 0, 0, 0);
    prettyRow->setSpacing(4);
    prettyRow->addStretch();
    copyPretty = compactButton("Copy pretty JSON", prettyPage);
    copyPretty->setObjectName("json-copy-pretty");
    copyPretty->setEnabled(false);
    prettyRow->addWidget(copyPretty);
    prettyLayout->addLayout(prettyRow);
    connect(copyPretty, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(pretty->toPlainText()); });
    pages->addWidget(prettyPage);

    // Pointer and its copy actions share one compact footer row.
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(4);
    pointer = new QLineEdit;
    pointer->setObjectName("json-pointer");
    pointer->setReadOnly(true);
    pointer->setPlaceholderText("Select a node to locate its source");
    pointer->setAccessibleName("Selected JSON pointer");
    pointer->setMaximumHeight(24);
    footer->addWidget(pointer, 1);
    copyPath = compactButton("Copy pointer", this);
    copyPath->setObjectName("json-copy-pointer");
    copyValue = compactButton("Copy value", this);
    copyValue->setObjectName("json-copy-value");
    copyValue->setToolTip("Copy the original JSON of the selected value");
    copyPath->setEnabled(false);
    copyValue->setEnabled(false);
    footer->addWidget(copyPath);
    footer->addWidget(copyValue);
    layout->addLayout(footer);
    connect(copyPath, &QPushButton::clicked, this, [this] {
        if (auto* item = tree->currentItem())
            QApplication::clipboard()->setText(nodes[item->data(0, Qt::UserRole).toInt()].toObject()["path"].toString());
    });
    connect(copyValue, &QPushButton::clicked, this, [this] {
        if (auto* item = tree->currentItem()) {
            const auto node = nodes[item->data(0, Qt::UserRole).toInt()].toObject();
            const auto start = node["start"].toInteger();
            QApplication::clipboard()->setText(QString::fromUtf8(source.mid(start, node["end"].toInteger() - start)));
        }
    });
    connect(filter, &QLineEdit::textChanged, this, [this] { filterTree(); });
    connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item && !selecting) selectNode(item->data(0, Qt::UserRole).toInt());
    });
    connect(scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (selecting || scene->selectedItems().isEmpty()) return;
        selectNode(scene->selectedItems().first()->data(0).toInt());
    });
    applyTheme(false);
}

void JsonPanel::applyTheme(bool dark) {
    darkTheme = dark;
    highlighter->setScheme(scheme(dark));
    if (!nodes.isEmpty()) drawGraph(false);
}

void JsonPanel::setView(JsonView requested) {
    current = requested;
    pages->setCurrentIndex(static_cast<int>(requested));
    if (requested == JsonView::Graph) QTimer::singleShot(0, this, [this] { fitGraph(); });
}

void JsonPanel::setError(const QString& error) {
    const QScopedValueRollback<bool> guard(selecting, true);
    nodes = {};
    source.clear();
    items.clear();
    children.clear();
    cards.clear();
    tree->clear();
    scene->clear();
    pretty->clear();
    pointer->clear();
    copyPretty->setEnabled(false);
    copyPath->setEnabled(false);
    copyValue->setEnabled(false);
    showStatus(status, error + "\nThe source document was not changed.");
}

void JsonPanel::setPreview(const QJsonObject& model, const QString& title, const QByteArray& text, bool reset) {
    const QScopedValueRollback<bool> guard(selecting, true);
    nodes = model["nodes"].toArray();
    source = text;
    tree->clear();
    items.clear();
    children.clear();
    children.resize(nodes.size());
    for (int index = 0; index < nodes.size(); ++index) {
        const auto node = nodes[index].toObject();
        auto* item = new QTreeWidgetItem(QStringList{node["name"].toString(), node["kind"].toString(), node["value"].toString()});
        item->setData(0, Qt::UserRole, index);
        if (node["parent"].isNull()) tree->addTopLevelItem(item);
        else {
            const auto parent = node["parent"].toInt();
            items[parent]->addChild(item);
            children[parent].append(index);
        }
        items.append(item);
    }
    tree->expandToDepth(1);
    pretty->setPlainText(model["pretty"].toString());
    pointer->clear();
    copyPretty->setEnabled(!nodes.isEmpty());
    copyPath->setEnabled(false);
    copyValue->setEnabled(false);
    const auto duplicates = model["duplicate_keys"].toInt();
    showStatus(status, QString("%1 | %2 JSON nodes | Local live preview").arg(title).arg(nodes.size()) +
        (duplicates ? QString("\n%1 duplicate key(s) are shown separately; their JSON pointers may be ambiguous.").arg(duplicates) : QString()));
    filterTree();
    bool stale = reset;
    for (const int index : expanded)
        if (index >= nodes.size() || children[index].isEmpty()) { stale = true; break; }
    if (stale || countCards(expanded) > cardLimit) expandWithinBudget();
    drawGraph(reset);
}

void JsonPanel::filterTree() {
    const auto query = filter->text();
    for (qsizetype index = items.size(); index-- > 0;) {
        auto* item = items[index];
        bool visible = query.isEmpty() || (item->text(0) + " " + item->text(2) + " " +
            nodes[index].toObject()["path"].toString()).contains(query, Qt::CaseInsensitive);
        for (int child = 0; child < item->childCount(); ++child)
            visible = visible || !item->child(child)->isHidden();
        item->setHidden(!visible);
        if (visible && !query.isEmpty()) item->setExpanded(true);
    }
}

QVector<JsonPanel::Row> JsonPanel::rowsOf(int index) const {
    QVector<Row> rows;
    if (index < 0 || index >= children.size()) return rows;
    for (const int child : children[index]) {
        const auto kind = nodes[child].toObject()["kind"].toString();
        rows.append({child, kind == "object" || kind == "array"});
    }
    return rows;
}

int JsonPanel::countCards(const QSet<int>& open) const {
    if (nodes.isEmpty()) return 0;
    int count = 0;
    QVector<int> pending{0};
    while (!pending.isEmpty()) {
        const int index = pending.takeLast();
        if (++count > cardLimit) return count;
        for (const int child : children[index])
            if (!children[child].isEmpty() && open.contains(child)) pending.append(child);
    }
    return count;
}

void JsonPanel::expandWithinBudget() {
    expanded.clear();
    if (nodes.isEmpty()) return;
    QVector<int> depths(nodes.size(), 0);
    QSet<int> containers;
    for (int index = 0; index < nodes.size(); ++index) {
        const auto parent = nodes[index].toObject()["parent"];
        depths[index] = parent.isNull() ? 0 : depths[parent.toInt()] + 1;
        if (!children[index].isEmpty()) containers.insert(index);
    }
    if (countCards(containers) <= cardLimit) { expanded = containers; return; }
    QSet<int> open;
    for (int depth = 0; depth < 64; ++depth) {
        QSet<int> next = open;
        bool added = false;
        for (const int index : containers)
            if (depths[index] == depth) { next.insert(index); added = true; }
        if (!added) break;
        if (countCards(next) > cardLimit) break;
        open = next;
    }
    expanded = open;
}

void JsonPanel::toggleNode(int index) {
    if (index < 0 || index >= nodes.size() || children[index].isEmpty()) return;
    const bool open = expanded.contains(index);
    if (open) expanded.remove(index);
    else expanded.insert(index);
    if (!open && countCards(expanded) > cardLimit) {
        expanded.remove(index);
        showStatus(graphStatus, QString("Expanding this branch exceeds %1 visible graph cards. "
            "Collapse another branch, or use the Tree view.").arg(cardLimit));
        return;
    }
    drawGraph(false);
}

int JsonPanel::graphRowCount() const {
    int count = 0;
    for (auto entry = cards.constBegin(); entry != cards.constEnd(); ++entry)
        count += std::max<int>(1, rowsOf(entry.key()).size());
    return count;
}

QString JsonPanel::graphLayoutProblem() const {
    for (auto card = cards.constBegin(); card != cards.constEnd(); ++card) {
        const auto parent = nodes[card.key()].toObject()["parent"];
        if (!parent.isNull() && cards.contains(parent.toInt()) &&
            card.value().left() <= cards.value(parent.toInt()).right())
            return QStringLiteral("A child card is not placed right of its parent.");
        for (auto other = cards.constBegin(); other != cards.constEnd(); ++other)
            if (other.key() != card.key() && card.value().intersects(other.value()))
                return QStringLiteral("Two graph cards overlap.");
    }
    return {};
}

void JsonPanel::drawGraph(bool fit) {
    const QScopedValueRollback<bool> guard(selecting, true);
    scene->clear();
    cards.clear();
    const auto colors = scheme(darkTheme);
    graph->setBackgroundBrush(colors.background);
    if (nodes.isEmpty()) {
        showStatus(graphStatus, "No JSON to graph.");
        return;
    }
    showStatus(graphStatus, QString("Double-click a nested key to expand or collapse it. Drag empty space to pan. "
        "Up to %1 cards.").arg(cardLimit));

    QVector<int> order;
    qreal nextY = 0;
    std::function<void(int, int)> place = [&](int index, int depth) {
        const auto rows = rowsOf(index);
        const qreal height = headerHeight + std::max<qreal>(1, rows.size()) * rowHeight + cardPadding;
        QVector<int> nested;
        for (const auto& row : rows)
            if (row.container && !children[row.node].isEmpty() && expanded.contains(row.node)) nested.append(row.node);
        const int mark = order.size();
        order.append(index);
        const qreal x = depth * (cardWidth + columnGap);
        if (nested.isEmpty()) {
            cards.insert(index, QRectF(x, nextY, cardWidth, height));
            nextY += height + verticalGap;
            return;
        }
        const qreal start = nextY;
        for (const int child : nested) place(child, depth + 1);
        const qreal blockHeight = nextY - verticalGap - start;
        qreal y = start + (blockHeight - height) / 2;
        if (y < start) {
            const qreal shift = start - y;
            for (int entry = mark + 1; entry < order.size(); ++entry) cards[order[entry]].translate(0, shift);
            nextY += shift;
            y = start;
        }
        cards.insert(index, QRectF(x, y, cardWidth, height));
        nextY = std::max(nextY, y + height + verticalGap);
    };
    place(0, 0);

    auto rowFont = font();
    auto keyFont = font();
    keyFont.setBold(true);
    auto headerFont = font();
    headerFont.setBold(true);
    const QFontMetrics rowMetrics(rowFont);
    const QFontMetrics keyMetrics(keyFont);

    for (const int index : order) {
        const auto rect = cards.value(index);
        const auto node = nodes[index].toObject();
        const auto rows = rowsOf(index);

        auto* body = new QGraphicsPathItem;
        QPainterPath shape;
        shape.addRoundedRect(rect, 9, 9);
        body->setPath(shape);
        body->setPen(QPen(colors.edge, 1.2));
        body->setBrush(colors.card);
        body->setZValue(1);
        scene->addItem(body);

        QPainterPath headerShape;
        headerShape.addRoundedRect(QRectF(rect.left(), rect.top(), rect.width(), headerHeight + 9), 9, 9);
        headerShape.addRect(QRectF(rect.left(), rect.top() + headerHeight - 1, rect.width(), 10));
        auto* headerItem = new ClickItem;
        headerItem->setRect(QRectF(rect.left(), rect.top(), rect.width(), headerHeight));
        headerItem->setPen(Qt::NoPen);
        headerItem->setBrush(Qt::NoBrush);
        headerItem->setFlag(QGraphicsItem::ItemIsSelectable);
        headerItem->setData(0, index);
        headerItem->setToolTip(node["path"].toString().toHtmlEscaped());
        headerItem->setZValue(3);
        scene->addPath(headerShape.simplified(), QPen(Qt::NoPen), colors.header)->setZValue(2);
        scene->addItem(headerItem);

        auto* heading = new QGraphicsSimpleTextItem;
        heading->setFont(headerFont);
        const auto name = node["parent"].isNull() ? QStringLiteral("$") : oneLine(node["name"].toString());
        heading->setText(QFontMetrics(headerFont).elidedText(
            name + "  " + node["value"].toString(), Qt::ElideRight, static_cast<int>(cardWidth) - 22));
        heading->setBrush(colors.headerText);
        heading->setPos(rect.left() + 11, rect.top() + (headerHeight - QFontMetrics(headerFont).height()) / 2);
        heading->setZValue(3);
        scene->addItem(heading);

        if (rows.isEmpty()) {
            auto* value = new QGraphicsSimpleTextItem;
            value->setFont(rowFont);
            value->setText(rowMetrics.elidedText(oneLine(node["value"].toString()), Qt::ElideRight,
                static_cast<int>(cardWidth) - 24));
            value->setBrush(node["kind"].toString() == "string" ? colors.string :
                node["kind"].toString() == "number" ? colors.number : colors.literal);
            value->setPos(rect.left() + 12, rect.top() + headerHeight + 4);
            value->setZValue(3);
            scene->addItem(value);
            continue;
        }

        for (int position = 0; position < rows.size(); ++position) {
            const auto& row = rows[position];
            const auto child = nodes[row.node].toObject();
            const qreal top = rect.top() + headerHeight + position * rowHeight;
            const bool nested = row.container && !children[row.node].isEmpty();
            const bool open = nested && expanded.contains(row.node);
            if (position > 0)
                scene->addLine(rect.left() + 8, top, rect.right() - 8, top, QPen(colors.divider, 1))->setZValue(2);

            auto* hit = new ClickItem;
            hit->setRect(QRectF(rect.left() + 1, top, rect.width() - 2, rowHeight));
            hit->setPen(Qt::NoPen);
            hit->setBrush(Qt::NoBrush);
            hit->setFlag(QGraphicsItem::ItemIsSelectable);
            hit->setData(0, row.node);
            hit->setToolTip(child["path"].toString().toHtmlEscaped());
            hit->setZValue(4);
            if (nested) hit->activate = [this, node = row.node] {
                QTimer::singleShot(0, this, [this, node] { toggleNode(node); });
            };
            scene->addItem(hit);

            auto* keyItem = new QGraphicsSimpleTextItem;
            keyItem->setFont(keyFont);
            const auto label = oneLine(child["name"].toString());
            const int keyWidth = std::min(keyMetrics.horizontalAdvance(label), static_cast<int>(cardWidth * 0.5));
            keyItem->setText(keyMetrics.elidedText(label, Qt::ElideRight, keyWidth));
            keyItem->setBrush(colors.key);
            keyItem->setPos(rect.left() + 12, top + (rowHeight - rowMetrics.height()) / 2);
            keyItem->setZValue(3);
            scene->addItem(keyItem);

            auto* valueItem = new QGraphicsSimpleTextItem;
            valueItem->setFont(rowFont);
            const auto kind = child["kind"].toString();
            valueItem->setBrush(kind == "string" ? colors.string : kind == "number" ? colors.number :
                row.container ? colors.badge : colors.literal);
            const qreal valueLeft = rect.left() + 20 + keyWidth;
            const qreal valueRoom = rect.right() - 16 - valueLeft - (nested ? 16 : 0);
            valueItem->setText(rowMetrics.elidedText(oneLine(child["value"].toString()), Qt::ElideRight,
                std::max(20, static_cast<int>(valueRoom))));
            valueItem->setPos(valueLeft, top + (rowHeight - rowMetrics.height()) / 2);
            valueItem->setZValue(3);
            scene->addItem(valueItem);

            if (nested) {
                auto* marker = new QGraphicsSimpleTextItem;
                marker->setFont(keyFont);
                marker->setText(open ? QStringLiteral("\u2212") : QStringLiteral("+"));
                marker->setBrush(colors.link);
                marker->setPos(rect.right() - 16, top + (rowHeight - rowMetrics.height()) / 2);
                marker->setZValue(3);
                scene->addItem(marker);
            }
            if (open && cards.contains(row.node)) {
                const auto target = cards.value(row.node);
                const QPointF from(rect.right(), top + rowHeight / 2);
                const QPointF to(target.left(), target.center().y());
                QPainterPath connection(from);
                connection.cubicTo(from + QPointF(columnGap * 0.45, 0), to - QPointF(columnGap * 0.45, 0), to);
                scene->addPath(connection, QPen(colors.link, 1.5))->setZValue(0);
            }
        }
    }
    scene->setSceneRect(scene->itemsBoundingRect().adjusted(-30, -30, 30, 30));
    if (fit) fitGraph();
}

void JsonPanel::fitGraph() {
    if (scene->items().isEmpty()) return;
    graph->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    if (graph->transform().m11() < 0.15) {
        graph->resetTransform();
        graph->scale(0.55, 0.55);
        graph->centerOn(scene->sceneRect().topLeft() + QPointF(cardWidth / 2, 80));
    }
    if (graph->transform().m11() > 1.2) {
        graph->resetTransform();
        graph->scale(1.2, 1.2);
        graph->centerOn(scene->sceneRect().topLeft() + QPointF(cardWidth / 2, 80));
    }
}

void JsonPanel::selectNode(int index) {
    if (index < 0 || index >= nodes.size()) return;
    const QScopedValueRollback<bool> guard(selecting, true);
    const auto node = nodes[index].toObject();
    tree->setCurrentItem(items[index]);
    copyPath->setEnabled(true);
    copyValue->setEnabled(true);
    tree->scrollToItem(items[index]);
    pointer->setText(node["path"].toString().isEmpty() ? "$ (root)" : node["path"].toString());
    scene->clearSelection();
    for (auto* item : scene->items()) {
        if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable) && item->data(0).toInt() == index) {
            item->setSelected(true);
            graph->ensureVisible(item);
            break;
        }
    }
    if (selectSource) selectSource(node["start"].toInteger(), node["end"].toInteger());
}
}
