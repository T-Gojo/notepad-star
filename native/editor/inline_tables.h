#pragma once
#include "ScintillaEditBase.h"

#include <QAction>
#include <QByteArray>
#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocale>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QRect>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

namespace star {

// A table is stored as a Markdown pipe table, so documents stay plain text that any
// Markdown tool understands; the editor draws it as a grid and edits it cell by cell.
enum class TableAlign { None, Left, Center, Right };

struct TextTable {
    QList<QStringList> rows;  // rows[0] is the header row
    QList<TableAlign> align;  // one entry per column
    QList<int> widths;        // per column, in character widths; 0 sizes the column to its contents
    int columns() const { return static_cast<int>(align.size()); }
    int rowCount() const { return static_cast<int>(rows.size()); }
};

constexpr int kTableMaxColumns = 64;
constexpr int kTableMaxRows = 2000;
constexpr int kTableMaxCell = 2000;

// Splits one "| a | b |" row; "\|" is a literal pipe inside a cell.
inline std::optional<QStringList> splitTableRow(const QString& raw) {
    const auto line = raw.trimmed();
    if (line.size() < 2 || line.size() > 16384 || !line.startsWith('|')) return std::nullopt;
    QStringList cells;
    QString cell;
    bool closed = false;
    for (qsizetype i = 1; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == '\\' && i + 1 < line.size() && line.at(i + 1) == '|') {
            cell += '|';
            ++i;
            closed = false;
        } else if (c == '|') {
            cells.append(cell.trimmed());
            cell.clear();
            closed = true;
        } else {
            cell += c;
            closed = false;
        }
    }
    if (!closed || cells.isEmpty() || cells.size() > kTableMaxColumns) return std::nullopt;
    return cells;
}

inline bool isTableRowText(const QString& raw) {
    const auto line = raw.trimmed();
    return line.size() >= 2 && line.startsWith('|') && line.endsWith('|') && !line.endsWith("\\|");
}

inline std::optional<TextTable> parseTable(const QStringList& lines) {
    if (lines.size() < 2 || lines.size() > kTableMaxRows + 1) return std::nullopt;
    const auto header = splitTableRow(lines.at(0));
    const auto separator = splitTableRow(lines.at(1));
    if (!header || !separator || header->size() != separator->size()) return std::nullopt;
    static const QRegularExpression rule(QStringLiteral(R"(^(:?)-+(:?)$)"));
    TextTable table;
    for (const auto& cell : *separator) {
        const auto match = rule.match(cell);
        if (!match.hasMatch()) return std::nullopt;
        const bool left = !match.captured(1).isEmpty();
        const bool right = !match.captured(2).isEmpty();
        table.align.append(left && right ? TableAlign::Center : right ? TableAlign::Right
                                                           : left ? TableAlign::Left : TableAlign::None);
    }
    table.widths = QList<int>(table.columns(), 0);
    table.rows.append(*header);
    for (qsizetype index = 2; index < lines.size(); ++index) {
        auto cells = splitTableRow(lines.at(index));
        // A row with more cells than the header is not a table this editor can rewrite
        // without losing text, so the block stays plain text.
        if (!cells || cells->size() > table.columns()) return std::nullopt;
        while (cells->size() < table.columns()) cells->append(QString());
        table.rows.append(*cells);
    }
    for (const auto& row : table.rows)
        for (const auto& cell : row)
            if (cell.size() > kTableMaxCell) return std::nullopt;
    return table;
}

inline QString tableCellText(QString cell) {
    cell.replace('\r', ' ').replace('\n', ' ').replace('\t', ' ');
    return cell.trimmed();
}

// Writes an aligned pipe table, so the plain text stays readable in any other editor.
inline QString formatTable(const TextTable& table, const QString& eol = QStringLiteral("\n")) {
    const int columns = table.columns();
    QList<QStringList> escaped;
    QList<int> widths(columns, 3);
    for (const auto& row : table.rows) {
        QStringList cells;
        for (int column = 0; column < columns; ++column) {
            auto cell = column < row.size() ? tableCellText(row.at(column)) : QString();
            cell.replace("|", "\\|");
            widths[column] = std::max(widths[column], static_cast<int>(cell.size()));
            cells.append(cell);
        }
        escaped.append(cells);
    }
    const auto pad = [&](const QString& cell, int column) {
        const int space = widths[column] - static_cast<int>(cell.size());
        switch (table.align[column]) {
        case TableAlign::Right: return QString(space, ' ') + cell;
        case TableAlign::Center: return QString(space / 2, ' ') + cell + QString(space - space / 2, ' ');
        default: return cell + QString(space, ' ');
        }
    };
    QStringList lines;
    for (qsizetype index = 0; index < escaped.size(); ++index) {
        QString line = "|";
        for (int column = 0; column < columns; ++column) line += " " + pad(escaped[index][column], column) + " |";
        lines.append(line);
        if (index != 0) continue;
        QString rule = "|";
        for (int column = 0; column < columns; ++column) {
            const int width = widths[column];
            switch (table.align[column]) {
            case TableAlign::Left: rule += " :" + QString(width - 1, '-') + " |"; break;
            case TableAlign::Center: rule += " :" + QString(width - 2, '-') + ": |"; break;
            case TableAlign::Right: rule += " " + QString(width - 1, '-') + ": |"; break;
            default: rule += " " + QString(width, '-') + " |"; break;
            }
        }
        lines.append(rule);
    }
    return lines.join(eol);
}

// Column widths chosen by dragging are kept in an HTML comment directly above the table:
// Markdown viewers hide it, and the table itself stays standard.
inline std::optional<QList<int>> parseTableWidths(const QString& raw) {
    static const QRegularExpression pattern(QStringLiteral(R"(^<!--\s*column-widths:((?:\s+\d{1,3}){1,64})\s*-->$)"));
    const auto match = pattern.match(raw.trimmed());
    if (!match.hasMatch()) return std::nullopt;
    QList<int> widths;
    for (const auto& part : match.captured(1).split(' ', Qt::SkipEmptyParts)) widths.append(part.toInt());
    return widths;
}

inline bool tableHasWidths(const TextTable& table) {
    return std::any_of(table.widths.begin(), table.widths.end(), [](int width) { return width > 0; });
}

// The full text of a table block: the widths comment (when any width is set) and the table.
inline QString formatTableBlock(const TextTable& table, const QString& eol = QStringLiteral("\n")) {
    if (!tableHasWidths(table)) return formatTable(table, eol);
    QStringList widths;
    for (const int width : table.widths) widths.append(QString::number(std::clamp(width, 0, 999)));
    return "<!-- column-widths: " + widths.join(' ') + " -->" + eol + formatTable(table, eol);
}

// Splits one line of delimited text; double quotes may wrap fields that hold the delimiter.
inline QStringList splitDelimited(const QString& line, QChar delimiter) {
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line.at(i + 1) == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"' && field.trimmed().isEmpty()) {
            field.clear();
            quoted = true;
        } else if (c == delimiter) {
            fields.append(field.trimmed());
            field.clear();
        } else {
            field += c;
        }
    }
    fields.append(field.trimmed());
    return fields;
}

// Builds a table from tab-, comma- or semicolon-separated text; the first line is the header.
inline std::optional<TextTable> tableFromDelimited(const QString& text) {
    QStringList lines = QString(text).replace("\r\n", "\n").replace('\r', '\n').split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
    if (lines.isEmpty() || lines.size() > kTableMaxRows) return std::nullopt;
    QChar delimiter;
    for (const QChar candidate : {QChar('\t'), QChar(','), QChar(';')})
        if (lines.first().contains(candidate)) { delimiter = candidate; break; }
    if (delimiter.isNull()) return std::nullopt;
    QList<QStringList> rows;
    int columns = 0;
    for (const auto& line : lines) {
        auto fields = splitDelimited(line, delimiter);
        for (auto& field : fields) field = tableCellText(field).left(kTableMaxCell);
        columns = std::max(columns, static_cast<int>(fields.size()));
        rows.append(fields);
    }
    if (columns < 1 || columns > kTableMaxColumns) return std::nullopt;
    TextTable table;
    for (int column = 0; column < columns; ++column) {
        table.align.append(TableAlign::None);
        table.widths.append(0);
    }
    for (int row = 0; row < rows.size(); ++row) {
        auto cells = rows[row];
        while (cells.size() < columns) cells.append(row == 0 ? "Column " + QString::number(cells.size() + 1) : QString());
        table.rows.append(cells);
    }
    return table;
}

// Numbers sort as numbers, ignoring thousands separators, currency signs and percent.
inline std::optional<double> tableNumber(QString cell) {
    cell = cell.trimmed();
    cell.remove(',').remove(' ');
    while (!cell.isEmpty() && QStringLiteral("$\u20ac\u00a3\u00a5\u20b9").contains(cell.front())) cell.remove(0, 1);
    if (cell.endsWith('%')) cell.chop(1);
    bool ok = false;
    const double value = QLocale::c().toDouble(cell, &ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

// Sorts the body rows by one column; empty cells always go last, the header never moves.
inline void sortTableRows(TextTable& table, int column, bool descending) {
    if (table.rowCount() < 3 || column < 0 || column >= table.columns()) return;
    std::stable_sort(table.rows.begin() + 1, table.rows.end(), [&](const QStringList& a, const QStringList& b) {
        const auto& left = a[column];
        const auto& right = b[column];
        if (left.isEmpty() != right.isEmpty()) return right.isEmpty();
        if (left.isEmpty()) return false;
        const auto x = tableNumber(left);
        const auto y = tableNumber(right);
        bool less;
        if (x && y) {
            if (*x == *y) return false;
            less = *x < *y;
        } else if (x.has_value() != y.has_value()) {
            less = x.has_value();
        } else {
            const int order = left.compare(right, Qt::CaseInsensitive);
            if (order == 0) return false;
            less = order < 0;
        }
        return descending ? !less : less;
    });
}

inline TextTable blankTable(int columns, int bodyRows) {
    TextTable table;
    columns = std::clamp(columns, 1, kTableMaxColumns);
    bodyRows = std::clamp(bodyRows, 0, kTableMaxRows - 1);
    QStringList header;
    for (int column = 0; column < columns; ++column) {
        header.append("Column " + QString::number(column + 1));
        table.align.append(TableAlign::None);
        table.widths.append(0);
    }
    table.rows.append(header);
    for (int row = 0; row < bodyRows; ++row) table.rows.append(QStringList(columns, QString()));
    return table;
}

inline QString tableAsHtml(const TextTable& table) {
    QString html = "<table>";
    for (int row = 0; row < table.rowCount(); ++row) {
        html += "<tr>";
        for (const auto& cell : table.rows[row])
            html += (row == 0 ? "<th>" : "<td>") + cell.toHtmlEscaped() + (row == 0 ? "</th>" : "</td>");
        html += "</tr>";
    }
    return html + "</table>";
}

inline QString tableAsTabSeparated(const TextTable& table) {
    QStringList lines;
    for (const auto& row : table.rows) lines.append(row.join('\t'));
    return lines.join('\n');
}

// The in-place editor for one cell; key handling belongs to the table view.
class TableCellEdit final : public QLineEdit {
public:
    explicit TableCellEdit(QWidget* parent) : QLineEdit(parent) { setFrame(false); }
    std::function<bool(QKeyEvent*)> keys;
    std::function<void()> left;

protected:
    bool event(QEvent* event) override {
        // Cell navigation keys belong to the cell, not to application shortcuts such as Esc.
        if (event->type() == QEvent::ShortcutOverride) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab ||
                key == Qt::Key_Backtab || key == Qt::Key_Up || key == Qt::Key_Down) {
                event->accept();
                return true;
            }
        }
        // Tab must reach keyPressEvent instead of moving focus to another widget.
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) && keys && keys(key)) return true;
        }
        return QLineEdit::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (keys && keys(event)) return;
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if (left && event->reason() != Qt::PopupFocusReason) left();
    }
};

// Draws Markdown pipe tables as grids over the lines that hold them and edits them in place.
class InlineTableView {
public:
    struct Block {
        sptr_t first = 0;  // first line of the block (the widths comment, when present)
        sptr_t last = 0;
        TextTable table;
    };
    struct Placement {
        Block block;
        QRect cover;
        QRect grid;
        QList<int> columnX;  // left edge of each column, plus the right edge of the grid
        QList<int> rowY;     // top edge of each row, plus the bottom edge of the grid
        QRect cell(int row, int column) const {
            return QRect(columnX[column], rowY[row], columnX[column + 1] - columnX[column], rowY[row + 1] - rowY[row]);
        }
    };

    explicit InlineTableView(ScintillaEditBase* owner) : editor(owner) {}

    bool enabled = true;
    // Set by the shell: false while a macro records or plays back.
    std::function<bool()> editable;

    // ---- Blocks ----------------------------------------------------------------------
    QString lineText(sptr_t line) const {
        if (line < 0 || line >= editor->send(SCI_GETLINECOUNT)) return {};
        const sptr_t length = editor->send(SCI_LINELENGTH, static_cast<uptr_t>(line));
        if (length <= 0 || length > 16384) return {};
        QByteArray bytes(static_cast<qsizetype>(length) + 1, '\0');
        editor->sends(SCI_GETLINE, static_cast<uptr_t>(line), bytes.data());
        bytes.resize(static_cast<qsizetype>(length));
        return QString::fromUtf8(bytes).trimmed();
    }
    bool isRow(sptr_t line) const { return isTableRowText(lineText(line)); }

    bool isWidths(sptr_t line) const { return parseTableWidths(lineText(line)).has_value(); }

    std::optional<Block> blockAt(sptr_t line) const {
        if (!enabled) return std::nullopt;
        if (!isRow(line)) {
            if (!isWidths(line) || !isRow(line + 1)) return std::nullopt;
            ++line;
        }
        sptr_t first = line;
        while (first > 0 && line - first < kTableMaxRows && isRow(first - 1)) --first;
        const sptr_t count = editor->send(SCI_GETLINECOUNT);
        sptr_t last = line;
        while (last + 1 < count && last - first < kTableMaxRows && isRow(last + 1)) ++last;
        QStringList lines;
        for (sptr_t index = first; index <= last; ++index) lines.append(lineText(index));
        auto table = parseTable(lines);
        if (!table) return std::nullopt;
        if (first > 0)
            if (const auto widths = parseTableWidths(lineText(first - 1)); widths && widths->size() == table->columns()) {
                table->widths = *widths;
                --first;
            }
        return Block{first, last, *table};
    }

    std::optional<Block> caretBlock() const {
        if (!enabled || editor->send(SCI_GETSELECTIONS) != 1 || !editor->send(SCI_GETSELECTIONEMPTY)) return std::nullopt;
        return blockAt(editor->send(SCI_LINEFROMPOSITION, editor->send(SCI_GETCURRENTPOS)));
    }

    // ---- Selection model (mirrors inline images) --------------------------------------
    void syncSelection() {
        const auto block = caretBlock();
        sptr_t line = -1;
        if (block) {
            line = block->first;
            const auto start = lineStart(block->first);
            if (editor->send(SCI_GETCURRENTPOS) != start) editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(start));
        }
        if (line == shown) return;
        shown = line;
        editor->viewport()->update();
    }

    void widenSelectionOverTables() {
        if (!enabled || editor->send(SCI_GETSELECTIONS) != 1 || editor->send(SCI_SELECTIONISRECTANGLE)) return;
        const auto anchor = editor->send(SCI_GETANCHOR);
        const auto caret = editor->send(SCI_GETCURRENTPOS);
        auto start = std::min(anchor, caret);
        auto end = std::max(anchor, caret);
        if (start == end) return;
        if (const auto first = blockAt(editor->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(start))))
            start = lineStart(first->first);
        if (const auto last = blockAt(editor->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(end))))
            if (end > lineStart(last->first)) end = afterBlock(*last);
        if (start == std::min(anchor, caret) && end == std::max(anchor, caret)) return;
        if (anchor <= caret) editor->send(SCI_SETSEL, static_cast<uptr_t>(start), end);
        else editor->send(SCI_SETSEL, static_cast<uptr_t>(end), start);
    }

    bool removeTable(const Block& block) {
        if (!canEdit()) return false;
        auto start = lineStart(block.first);
        const auto end = afterBlock(block);
        if (block.last + 1 >= editor->send(SCI_GETLINECOUNT) && block.first > 0)
            start = editor->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(block.first - 1));
        editor->send(SCI_BEGINUNDOACTION);
        editor->send(SCI_DELETERANGE, static_cast<uptr_t>(start), end - start);
        editor->send(SCI_ENDUNDOACTION);
        editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(start));
        return true;
    }

    // Opens an empty line above a selected table for text typed or pasted at it.
    bool openLineAboveCaretTable() {
        const auto block = caretBlock();
        if (!block || !canEdit()) return false;
        const auto start = lineStart(block->first);
        editor->sends(SCI_INSERTTEXT, static_cast<uptr_t>(start), eol().constData());
        editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(start));
        return true;
    }

    // Copies a table as Markdown text, an HTML table for word processors and tab-separated
    // values for spreadsheets.
    bool copyTable(const Block& block, bool cut) {
        auto* mime = new QMimeData;
        mime->setText(formatTable(block.table, QString::fromLatin1(eol())) + QString::fromLatin1(eol()));
        mime->setHtml(tableAsHtml(block.table));
        mime->setData("text/tab-separated-values", tableAsTabSeparated(block.table).toUtf8());
        QGuiApplication::clipboard()->setMimeData(mime);
        if (cut) removeTable(block);
        return true;
    }
    bool copyCaretTable(bool cut) {
        const auto block = caretBlock();
        return block && copyTable(*block, cut);
    }

    // ---- Keyboard on a selected table ----------------------------------------------------
    bool keyPress(QKeyEvent* event) {
        if (!enabled || editor->send(SCI_GETSELECTIONS) != 1 || editor->send(SCI_SELECTIONISRECTANGLE)) return false;
        const auto key = event->key();
        const bool erase = key == Qt::Key_Backspace || key == Qt::Key_Delete;
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        const bool command = event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
        const auto text = event->text();
        const bool typed = !command && !text.isEmpty() && (text.at(0).isPrint() || key == Qt::Key_Tab);
        const bool paste = event->matches(QKeySequence::Paste);
        if (!editor->send(SCI_GETSELECTIONEMPTY)) {
            if (erase || typed || paste || event->matches(QKeySequence::Cut)) widenSelectionOverTables();
            return false;
        }
        const auto position = editor->send(SCI_GETCURRENTPOS);
        const auto line = editor->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(position));
        if (const auto block = blockAt(line)) {
            if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut))
                return copyTable(*block, event->matches(QKeySequence::Cut));
            if (erase) { removeTable(*block); return true; }
            if ((key == Qt::Key_Right || key == Qt::Key_End || key == Qt::Key_Down || key == Qt::Key_PageDown) && !shift) {
                stepPast(*block);
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_F2) {
                beginEdit(block->first, 0, 0);
                return true;
            }
            if (typed || paste) openLineAboveCaretTable();
            return false;
        }
        // Erasing toward a table from a neighbouring line of text selects the table instead
        // of merging that text into its rows.
        const auto start = lineStart(line);
        const auto end = editor->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(line));
        if (key == Qt::Key_Backspace && position == start && end > start && line > 0)
            if (const auto above = blockAt(line - 1)) { select(*above); return true; }
        if (key == Qt::Key_Delete && position == end && end > start)
            if (const auto below = blockAt(line + 1)) { select(*below); return true; }
        return false;
    }

    // ---- Mouse ------------------------------------------------------------------------
    bool mousePress(QMouseEvent* event) {
        if (!enabled || event->button() != Qt::LeftButton) return false;
        const auto point = event->position().toPoint();
        for (const auto& placement : placements()) {
            if (!placement.cover.contains(point)) continue;
            if (const int border = borderAt(placement, point); border >= 0 && canEdit()) {
                finishEdit(true);
                resizing = true;
                resizeFirst = placement.block.first;
                resizeColumn = border;
                resizeOrigin = point.x();
                resizeStart = placement.columnX[border + 1] - placement.columnX[border];
                resizeWidth = resizeStart;
                return true;
            }
            const auto [row, column] = cellAt(placement, point);
            select(placement.block);
            if (row >= 0 && canEdit()) beginEdit(placement.block.first, row, column);
            return true;
        }
        return false;
    }

    // Dragging a column's right border resizes it; hovering a border shows the resize cursor.
    bool mouseMove(QMouseEvent* event) {
        if (!enabled) return false;
        const auto point = event->position().toPoint();
        if (resizing) {
            resizeWidth = std::clamp(resizeStart + point.x() - resizeOrigin, 2 * kPadding + characterWidth() * 2, 1600);
            editor->viewport()->update();
            return true;
        }
        bool overBorder = false;
        for (const auto& placement : placements())
            if (borderAt(placement, point) >= 0) overBorder = true;
        if (overBorder && canEdit()) {
            editor->viewport()->setCursor(Qt::SplitHCursor);
            hovering = true;
            return true;
        }
        if (hovering) {
            editor->viewport()->unsetCursor();
            hovering = false;
        }
        return false;
    }

    bool mouseRelease(QMouseEvent* event) {
        if (!resizing || event->button() != Qt::LeftButton) return false;
        resizing = false;
        const int characters = std::max(2, static_cast<int>(std::lround(
            static_cast<double>(resizeWidth - 2 * kPadding - 2) / characterWidth())));
        if (resizeWidth != resizeStart) setColumnWidth(resizeFirst, resizeColumn, characters);
        editor->viewport()->update();
        return true;
    }

    // Double-clicking a column border fits that column to its contents again.
    bool mouseDoubleClick(QMouseEvent* event) {
        if (!enabled || event->button() != Qt::LeftButton) return false;
        const auto point = event->position().toPoint();
        for (const auto& placement : placements()) {
            const int border = borderAt(placement, point);
            if (border < 0) continue;
            resizing = false;
            if (canEdit()) setColumnWidth(placement.block.first, border, 0);
            return true;
        }
        return mousePress(event);
    }

    bool contextMenu(QContextMenuEvent* event) {
        if (!enabled) return false;
        const auto point = event->pos();
        for (const auto& placement : placements()) {
            if (!placement.grid.contains(point)) continue;
            auto [row, column] = cellAt(placement, point);
            if (row < 0) { row = 0; column = 0; }
            finishEdit(true);
            showMenu(placement.block, row, column, event->globalPos());
            return true;
        }
        return false;
    }

    // ---- Painting -----------------------------------------------------------------------
    void paint() {
        const auto found = placements();
        hadTables = !found.empty();
        // Painting never edits the document: a cell whose table scrolled away or changed
        // under the editor is committed once this paint has finished.
        if (editing() && !committing &&
            std::none_of(found.begin(), found.end(), [this](const Placement& p) { return p.block.first == editFirst; }))
            QTimer::singleShot(0, cellEditor.data(), [this] { if (editing()) finishEdit(true); });
        if (found.empty()) return;
        QPainter painter(editor->viewport());
        painter.setClipRect(textArea());
        const auto background = colorOf(editor->send(SCI_STYLEGETBACK, STYLE_DEFAULT));
        const auto foreground = colorOf(editor->send(SCI_STYLEGETFORE, STYLE_DEFAULT));
        const auto header = blend(background, foreground, 0.08);
        const auto rule = blend(background, foreground, 0.28);
        const auto selected = caretBlock();
        const auto plain = font();
        auto bold = plain;
        bold.setBold(true);
        for (const auto& placement : found) {
            painter.fillRect(placement.cover, background);
            const auto& table = placement.block.table;
            painter.fillRect(QRect(placement.grid.left(), placement.rowY[0], placement.grid.width(),
                placement.rowY[1] - placement.rowY[0]), header);
            painter.setPen(QPen(rule, 1));
            for (const int y : placement.rowY) painter.drawLine(placement.grid.left(), y, placement.grid.right(), y);
            for (const int x : placement.columnX) painter.drawLine(x, placement.grid.top(), x, placement.grid.bottom());
            painter.setPen(foreground);
            for (int row = 0; row < table.rowCount(); ++row) {
                painter.setFont(row == 0 ? bold : plain);
                const QFontMetrics metrics(row == 0 ? bold : plain);
                for (int column = 0; column < table.columns(); ++column) {
                    const auto box = placement.cell(row, column).adjusted(kPadding, 0, -kPadding, 0);
                    const auto label = metrics.elidedText(table.rows[row][column], Qt::ElideRight, box.width());
                    painter.drawText(box, alignmentOf(table.align[column]) | Qt::AlignVCenter, label);
                }
            }
            if (selected && selected->first == placement.block.first && !editingBlock(placement.block)) {
                painter.setPen(QPen(QColor(0, 122, 204), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(placement.grid.adjusted(0, 0, -1, -1));
            }
        }
        painter.end();
        reposition(found);
    }

    // True when the last paint drew a table, so edits elsewhere know to repaint the grid.
    bool hadTables = false;

    std::vector<Placement> placements() const {
        std::vector<Placement> found;
        if (!enabled || editor->send(SCI_GETLINECOUNT) <= 0) return found;
        const sptr_t lines = editor->send(SCI_GETLINECOUNT);
        const sptr_t firstRow = editor->send(SCI_GETFIRSTVISIBLELINE);
        const sptr_t rows = editor->send(SCI_LINESONSCREEN) + 2;
        const auto selectionStart = editor->send(SCI_GETSELECTIONSTART);
        const auto selectionEnd = editor->send(SCI_GETSELECTIONEND);
        sptr_t covered = -1;
        for (sptr_t row = firstRow; row <= firstRow + rows; ++row) {
            const sptr_t line = editor->send(SCI_DOCLINEFROMVISIBLE, static_cast<uptr_t>(std::max<sptr_t>(row, 0)));
            if (line < 0 || line >= lines || line <= covered) continue;
            const auto block = blockAt(line);
            if (!block) continue;
            covered = block->last;
            // While a text selection reaches into a table, show its text so the selection is visible.
            if (selectionEnd > selectionStart && selectionStart < afterBlock(*block) && selectionEnd > lineStart(block->first))
                continue;
            found.push_back(layout(*block));
        }
        return found;
    }

    // ---- Cell editing ---------------------------------------------------------------------
    bool editing() const { return cellEditor && cellEditor->isVisible(); }
    QLineEdit* activeCellEditor() const { return editing() ? cellEditor.data() : nullptr; }
    int editingRow() const { return editRow; }
    int editingColumn() const { return editColumn; }

    bool beginEdit(sptr_t firstLine, int row, int column) {
        const auto block = blockAt(firstLine);
        if (!block || !canEdit()) return false;
        row = std::clamp(row, 0, block->table.rowCount() - 1);
        column = std::clamp(column, 0, block->table.columns() - 1);
        if (block->first != firstLine) return false;
        if (!cellEditor) {
            cellEditor = new TableCellEdit(editor->viewport());
            cellEditor->keys = [this](QKeyEvent* event) { return cellKey(event); };
            cellEditor->left = [this] { if (!committing) finishEdit(true); };
        }
        editFirst = block->first;
        editRow = row;
        editColumn = column;
        auto cellFont = font();
        cellFont.setBold(row == 0);
        cellEditor->setFont(cellFont);
        cellEditor->setAlignment(alignmentOf(block->table.align[column]) | Qt::AlignVCenter);
        cellEditor->setText(block->table.rows[row][column]);
        cellEditor->selectAll();
        editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(lineStart(block->first)));
        editor->send(SCI_SCROLLCARET);
        const auto layoutNow = layout(*block);
        cellEditor->setGeometry(layoutNow.cell(row, column).adjusted(1, 1, 0, 0));
        cellEditor->show();
        cellEditor->setFocus(Qt::OtherFocusReason);
        editor->viewport()->update();
        return true;
    }

    // Writes the cell being edited back into the document; optionally keeps the editor open.
    void finishEdit(bool commit) {
        if (!editing() || committing) return;
        if (commit) writeCell(editRow, editColumn, cellEditor->text());
        committing = true;
        cellEditor->hide();
        committing = false;
        editor->setFocus(Qt::OtherFocusReason);
        if (const auto block = blockAt(editFirst)) editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(lineStart(block->first)));
        editor->viewport()->update();
    }

    // Structural edits used by the context menu (and by tests).
    bool insertRow(sptr_t firstLine, int row) {
        return mutate(firstLine, [&](TextTable& table) {
            row = std::clamp(row, 1, table.rowCount());
            if (table.rowCount() >= kTableMaxRows) return false;
            table.rows.insert(row, QStringList(table.columns(), QString()));
            return true;
        });
    }
    bool deleteRow(sptr_t firstLine, int row) {
        return mutate(firstLine, [&](TextTable& table) {
            if (row <= 0 || row >= table.rowCount()) return false;
            table.rows.removeAt(row);
            return true;
        });
    }
    bool insertColumn(sptr_t firstLine, int column) {
        return mutate(firstLine, [&](TextTable& table) {
            column = std::clamp(column, 0, table.columns());
            if (table.columns() >= kTableMaxColumns) return false;
            for (int row = 0; row < table.rowCount(); ++row)
                table.rows[row].insert(column, row == 0 ? "Column " + QString::number(table.columns() + 1) : QString());
            table.align.insert(column, TableAlign::None);
            table.widths.insert(column, 0);
            return true;
        });
    }
    bool deleteColumn(sptr_t firstLine, int column) {
        const auto block = blockAt(firstLine);
        if (!block) return false;
        if (block->table.columns() <= 1) return removeTable(*block);
        return mutate(firstLine, [&](TextTable& table) {
            if (column < 0 || column >= table.columns()) return false;
            for (auto& row : table.rows) row.removeAt(column);
            table.align.removeAt(column);
            table.widths.removeAt(column);
            return true;
        });
    }
    bool alignColumn(sptr_t firstLine, int column, TableAlign align) {
        return mutate(firstLine, [&](TextTable& table) {
            if (column < 0 || column >= table.columns()) return false;
            table.align[column] = align;
            return true;
        });
    }
    // Fills cells from tab-separated text (as copied from a spreadsheet), growing the table.
    bool pasteCells(sptr_t firstLine, int row, int column, const QString& text) {
        QStringList lines = QString(text).replace("\r\n", "\n").replace('\r', '\n').split('\n');
        while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
        if (lines.isEmpty()) return false;
        return mutate(firstLine, [&](TextTable& table) {
            for (qsizetype offset = 0; offset < lines.size(); ++offset) {
                const auto values = lines[offset].split('\t');
                const int target = row + static_cast<int>(offset);
                if (target >= kTableMaxRows) break;
                while (table.rowCount() <= target) table.rows.append(QStringList(table.columns(), QString()));
                for (qsizetype index = 0; index < values.size(); ++index) {
                    const int cell = column + static_cast<int>(index);
                    if (cell >= kTableMaxColumns) break;
                    while (table.columns() <= cell) {
                        for (int r = 0; r < table.rowCount(); ++r)
                            table.rows[r].append(r == 0 ? "Column " + QString::number(table.columns() + 1) : QString());
                        table.align.append(TableAlign::None);
                        table.widths.append(0);
                    }
                    table.rows[target][cell] = tableCellText(values[index]).left(kTableMaxCell);
                }
            }
            return true;
        });
    }
    bool sortRows(sptr_t firstLine, int column, bool descending) {
        return mutate(firstLine, [&](TextTable& table) {
            if (table.rowCount() < 3 || column < 0 || column >= table.columns()) return false;
            sortTableRows(table, column, descending);
            return true;
        });
    }
    bool moveRow(sptr_t firstLine, int row, int delta) {
        return mutate(firstLine, [&](TextTable& table) {
            const int target = row + delta;
            if (row < 1 || target < 1 || row >= table.rowCount() || target >= table.rowCount()) return false;
            table.rows.swapItemsAt(row, target);
            return true;
        });
    }
    bool moveColumn(sptr_t firstLine, int column, int delta) {
        return mutate(firstLine, [&](TextTable& table) {
            const int target = column + delta;
            if (column < 0 || target < 0 || column >= table.columns() || target >= table.columns()) return false;
            for (auto& row : table.rows) row.swapItemsAt(column, target);
            table.align.swapItemsAt(column, target);
            table.widths.swapItemsAt(column, target);
            return true;
        });
    }
    bool duplicateRow(sptr_t firstLine, int row) {
        return mutate(firstLine, [&](TextTable& table) {
            if (row < 1 || row >= table.rowCount() || table.rowCount() >= kTableMaxRows) return false;
            table.rows.insert(row + 1, table.rows[row]);
            return true;
        });
    }
    // Sets a column width in character widths; 0 (or column -1 for every column) fits contents.
    bool setColumnWidth(sptr_t firstLine, int column, int characters) {
        return mutate(firstLine, [&](TextTable& table) {
            if (column < -1 || column >= table.columns()) return false;
            const auto before = table.widths;
            for (int index = 0; index < table.columns(); ++index)
                if (column == -1 || index == column) table.widths[index] = std::clamp(characters, 0, 999);
            return table.widths != before;
        });
    }
    // The cell table commands act on: the one being edited, else the last edited cell of the
    // selected table, else its first cell.
    std::optional<std::tuple<sptr_t, int, int>> currentCell() const {
        if (editing()) return std::tuple<sptr_t, int, int>{editFirst, editRow, editColumn};
        const auto block = caretBlock();
        if (!block) return std::nullopt;
        if (block->first == editFirst)
            return std::tuple<sptr_t, int, int>{editFirst, std::min(editRow, block->table.rowCount() - 1),
                std::min(editColumn, block->table.columns() - 1)};
        return std::tuple<sptr_t, int, int>{block->first, block->table.rowCount() > 1 ? 1 : 0, 0};
    }
    bool writeCell(int row, int column, const QString& value) {
        return mutate(editFirst, [&](TextTable& table) {
            if (row < 0 || row >= table.rowCount() || column < 0 || column >= table.columns()) return false;
            const auto cleaned = tableCellText(value).left(kTableMaxCell);
            if (table.rows[row][column] == cleaned) return false;
            table.rows[row][column] = cleaned;
            return true;
        });
    }

private:
    static constexpr int kPadding = 8;

    sptr_t lineStart(sptr_t line) const { return editor->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line)); }
    sptr_t afterBlock(const Block& block) const {
        return block.last + 1 < editor->send(SCI_GETLINECOUNT) ? lineStart(block.last + 1)
                                                               : editor->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(block.last));
    }
    QByteArray eol() const {
        const auto mode = editor->send(SCI_GETEOLMODE);
        return mode == SC_EOL_CR ? QByteArray("\r") : mode == SC_EOL_CRLF ? QByteArray("\r\n") : QByteArray("\n");
    }
    bool canEdit() const { return enabled && !editor->send(SCI_GETREADONLY) && (!editable || editable()); }
    bool editingBlock(const Block& block) const { return editing() && editFirst == block.first; }

    void select(const Block& block) {
        editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(lineStart(block.first)));
        syncSelection();
    }
    void stepPast(const Block& block) {
        if (block.last + 1 < editor->send(SCI_GETLINECOUNT)) {
            editor->send(SCI_SETEMPTYSELECTION, static_cast<uptr_t>(lineStart(block.last + 1)));
        } else if (canEdit()) {
            editor->send(SCI_DOCUMENTEND);
            editor->sends(SCI_ADDTEXT, static_cast<uptr_t>(eol().size()), eol().constData());
        }
    }

    // Rewrites a table block as one undoable step; the first line keeps its position.
    template <typename Change>
    bool mutate(sptr_t firstLine, Change change) {
        if (!canEdit()) return false;
        const auto block = blockAt(firstLine);
        if (!block || block->first != firstLine) return false;
        auto table = block->table;
        if (!change(table)) return false;
        const auto text = formatTableBlock(table, QString::fromLatin1(eol())).toUtf8();
        const auto start = lineStart(block->first);
        const auto end = editor->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(block->last));
        const auto targetStart = editor->send(SCI_GETTARGETSTART);
        const auto targetEnd = editor->send(SCI_GETTARGETEND);
        editor->send(SCI_BEGINUNDOACTION);
        editor->send(SCI_SETTARGETRANGE, static_cast<uptr_t>(start), end);
        editor->sends(SCI_REPLACETARGET, static_cast<uptr_t>(text.size()), text.constData());
        editor->send(SCI_ENDUNDOACTION);
        editor->send(SCI_SETTARGETRANGE, static_cast<uptr_t>(targetStart), targetEnd);
        editor->viewport()->update();
        return true;
    }

    bool cellKey(QKeyEvent* event) {
        const auto key = event->key();
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        const auto block = blockAt(editFirst);
        if (!block) { finishEdit(false); return true; }
        const int rows = block->table.rowCount();
        const int columns = block->table.columns();
        if (key == Qt::Key_Escape) { finishEdit(false); return true; }
        if (event->matches(QKeySequence::Paste)) {
            const auto* mime = QGuiApplication::clipboard()->mimeData();
            const auto text = mime ? mime->text() : QString();
            if (!text.contains('\t') && !text.contains('\n') && !text.contains('\r')) return false;
            writeCell(editRow, editColumn, cellEditor->text());
            pasteCells(editFirst, editRow, editColumn, text);
            beginEdit(editFirst, editRow, editColumn);
            return true;
        }
        int row = editRow;
        int column = editColumn;
        bool grow = false;
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            if (key == Qt::Key_Backtab || shift) {
                if (column > 0) --column;
                else if (row > 0) { --row; column = columns - 1; }
            } else if (column + 1 < columns) {
                ++column;
            } else {
                column = 0;
                ++row;
                grow = row >= rows;
            }
        } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (shift) row = std::max(0, row - 1);
            else { ++row; grow = row >= rows; }
        } else if (key == Qt::Key_Up) {
            row = std::max(0, row - 1);
        } else if (key == Qt::Key_Down) {
            row = std::min(rows - 1, row + 1);
        } else if ((key == Qt::Key_Left || key == Qt::Key_Right) && !shift && !cellEditor->hasSelectedText() &&
            cellEditor->cursorPosition() == (key == Qt::Key_Left ? 0 : static_cast<int>(cellEditor->text().size()))) {
            // At the edge of a cell's text the arrow keys continue into the neighbouring cell.
            if (key == Qt::Key_Left) {
                if (column > 0) --column;
                else if (row > 0) { --row; column = columns - 1; }
                else return true;
            } else {
                if (column + 1 < columns) ++column;
                else if (row + 1 < rows) { ++row; column = 0; }
                else return true;
            }
        } else {
            return false;
        }
        committing = true;
        writeCell(editRow, editColumn, cellEditor->text());
        if (grow) insertRow(editFirst, rows);
        committing = false;
        beginEdit(editFirst, row, column);
        return true;
    }

    void showMenu(const Block& block, int row, int column, QPoint global) {
        QMenu menu;
        const bool writable = canEdit();
        const auto first = block.first;
        const int rows = block.table.rowCount();
        const int columns = block.table.columns();
        const auto add = [&](QMenu* target, const QString& label, bool allowed, std::function<void()> action) {
            auto* item = target->addAction(label);
            item->setEnabled(allowed);
            QObject::connect(item, &QAction::triggered, item, [action] { action(); });
        };
        add(&menu, "Edit Cell", writable, [=] { beginEdit(first, row, column); });
        menu.addSeparator();
        auto* rowMenu = menu.addMenu("Row");
        add(rowMenu, "Insert Row Above", writable && row > 0, [=] { insertRow(first, row); });
        add(rowMenu, "Insert Row Below", writable, [=] { insertRow(first, row + 1); });
        add(rowMenu, "Duplicate Row", writable && row > 0, [=] { duplicateRow(first, row); });
        rowMenu->addSeparator();
        add(rowMenu, "Move Row Up", writable && row > 1, [=] { moveRow(first, row, -1); });
        add(rowMenu, "Move Row Down", writable && row > 0 && row + 1 < rows, [=] { moveRow(first, row, 1); });
        rowMenu->addSeparator();
        add(rowMenu, "Delete Row", writable && row > 0, [=] { deleteRow(first, row); });
        auto* columnMenu = menu.addMenu("Column");
        add(columnMenu, "Insert Column Left", writable, [=] { insertColumn(first, column); });
        add(columnMenu, "Insert Column Right", writable, [=] { insertColumn(first, column + 1); });
        columnMenu->addSeparator();
        add(columnMenu, "Move Column Left", writable && column > 0, [=] { moveColumn(first, column, -1); });
        add(columnMenu, "Move Column Right", writable && column + 1 < columns, [=] { moveColumn(first, column, 1); });
        columnMenu->addSeparator();
        add(columnMenu, "Fit Column to Contents", writable && block.table.widths.value(column) > 0,
            [=] { setColumnWidth(first, column, 0); });
        add(columnMenu, "Fit All Columns", writable && tableHasWidths(block.table), [=] { setColumnWidth(first, -1, 0); });
        columnMenu->addSeparator();
        add(columnMenu, "Delete Column", writable, [=] { deleteColumn(first, column); });
        auto* alignMenu = menu.addMenu("Align Column");
        add(alignMenu, "Left", writable, [=] { alignColumn(first, column, TableAlign::Left); });
        add(alignMenu, "Center", writable, [=] { alignColumn(first, column, TableAlign::Center); });
        add(alignMenu, "Right", writable, [=] { alignColumn(first, column, TableAlign::Right); });
        auto* sortMenu = menu.addMenu("Sort by This Column");
        add(sortMenu, "Ascending (A \u2192 Z, 0 \u2192 9)", writable && rows > 2, [=] { sortRows(first, column, false); });
        add(sortMenu, "Descending (Z \u2192 A, 9 \u2192 0)", writable && rows > 2, [=] { sortRows(first, column, true); });
        menu.addSeparator();
        add(&menu, "Copy Table", true, [=] { if (const auto current = blockAt(first)) copyTable(*current, false); });
        add(&menu, "Delete Table", writable, [=] { if (const auto current = blockAt(first)) removeTable(*current); });
        menu.exec(global);
    }

    // The column whose right border is under the point, within a few pixels.
    int borderAt(const Placement& placement, QPoint point) const {
        if (point.y() < placement.grid.top() || point.y() > placement.grid.bottom()) return -1;
        for (int column = 0; column + 1 < placement.columnX.size(); ++column)
            if (std::abs(point.x() - placement.columnX[column + 1]) <= 4) return column;
        return -1;
    }
    int characterWidth() const { return std::max(1, QFontMetrics(font()).horizontalAdvance(QLatin1Char('0'))); }

    std::pair<int, int> cellAt(const Placement& placement, QPoint point) const {
        if (!placement.grid.contains(point)) return {-1, -1};
        int row = -1;
        int column = -1;
        for (int index = 0; index + 1 < placement.rowY.size(); ++index)
            if (point.y() >= placement.rowY[index] && point.y() < placement.rowY[index + 1]) row = index;
        for (int index = 0; index + 1 < placement.columnX.size(); ++index)
            if (point.x() >= placement.columnX[index] && point.x() < placement.columnX[index + 1]) column = index;
        return row < 0 || column < 0 ? std::pair<int, int>{-1, -1} : std::pair<int, int>{row, column};
    }

    Placement layout(const Block& block) const {
        Placement placement;
        placement.block = block;
        const int textHeight = std::max(1, static_cast<int>(editor->send(SCI_TEXTHEIGHT, static_cast<uptr_t>(block.first))));
        const auto start = lineStart(block.first);
        const int x = static_cast<int>(editor->send(SCI_POINTXFROMPOSITION, 0, start));
        const int top = static_cast<int>(editor->send(SCI_POINTYFROMPOSITION, 0, start));
        const auto lastStart = lineStart(block.last);
        const int lastRows = std::max(1, static_cast<int>(editor->send(SCI_WRAPCOUNT, static_cast<uptr_t>(block.last)))) +
            std::max(0, static_cast<int>(editor->send(SCI_ANNOTATIONGETLINES, static_cast<uptr_t>(block.last))));
        const int bottom = static_cast<int>(editor->send(SCI_POINTYFROMPOSITION, 0, lastStart)) + lastRows * textHeight;
        const auto& table = block.table;
        const auto plain = font();
        auto bold = plain;
        bold.setBold(true);
        const QFontMetrics plainMetrics(plain);
        const QFontMetrics boldMetrics(bold);
        placement.columnX.append(x);
        const int characters = std::max(1, plainMetrics.horizontalAdvance(QLatin1Char('0')));
        for (int column = 0; column < table.columns(); ++column) {
            int width = 48;
            if (resizing && resizeFirst == block.first && resizeColumn == column) {
                width = resizeWidth;
            } else if (column < table.widths.size() && table.widths[column] > 0) {
                width = table.widths[column] * characters + 2 * kPadding + 2;
            } else {
                for (int row = 0; row < table.rowCount(); ++row)
                    width = std::max(width, (row == 0 ? boldMetrics : plainMetrics).horizontalAdvance(table.rows[row][column]) + 2 * kPadding + 2);
                width = std::min(width, 560);
            }
            placement.columnX.append(placement.columnX.last() + width);
        }
        const int rows = table.rowCount();
        const double height = std::max(bottom - top, rows * textHeight);
        for (int row = 0; row <= rows; ++row) placement.rowY.append(top + static_cast<int>(std::lround(height * row / rows)));
        placement.grid = QRect(QPoint(x, top), QPoint(placement.columnX.last(), placement.rowY.last()));
        placement.cover = QRect(x, top, std::max(editor->viewport()->width() - x, placement.grid.width() + 2),
            std::max(bottom - top, placement.grid.height() + 1));
        return placement;
    }

    void reposition(const std::vector<Placement>& found) {
        if (!editing() || committing) return;
        for (const auto& placement : found) {
            if (placement.block.first != editFirst) continue;
            if (editRow >= placement.block.table.rowCount() || editColumn >= placement.block.table.columns()) break;
            const auto target = placement.cell(editRow, editColumn).adjusted(1, 1, 0, 0);
            if (cellEditor->geometry() != target) cellEditor->setGeometry(target);
            return;
        }
    }

    QRect textArea() const {
        int left = 0;
        const int margins = static_cast<int>(editor->send(SCI_GETMARGINS));
        for (int margin = 0; margin < margins; ++margin) left += static_cast<int>(editor->send(SCI_GETMARGINWIDTHN, static_cast<uptr_t>(margin)));
        left += static_cast<int>(editor->send(SCI_GETMARGINLEFT));
        return QRect(left, 0, std::max(0, editor->viewport()->width() - left), editor->viewport()->height());
    }

    QFont font() const {
        QByteArray family(256, '\0');
        const auto length = editor->sends(SCI_STYLEGETFONT, STYLE_DEFAULT, family.data());
        family.resize(std::clamp<qsizetype>(length, 0, 255));
        QFont result(QString::fromUtf8(family));
        const double size = editor->send(SCI_STYLEGETSIZEFRACTIONAL, STYLE_DEFAULT) / static_cast<double>(SC_FONT_SIZE_MULTIPLIER) +
            static_cast<double>(editor->send(SCI_GETZOOM));
        result.setPointSizeF(std::max(4.0, size));
        return result;
    }

    static Qt::Alignment alignmentOf(TableAlign align) {
        return align == TableAlign::Right ? Qt::AlignRight : align == TableAlign::Center ? Qt::AlignHCenter : Qt::AlignLeft;
    }
    static QColor colorOf(sptr_t value) {
        return QColor(static_cast<int>(value & 0xff), static_cast<int>((value >> 8) & 0xff), static_cast<int>((value >> 16) & 0xff));
    }
    static QColor blend(const QColor& base, const QColor& over, double amount) {
        return QColor(static_cast<int>(base.red() + (over.red() - base.red()) * amount),
            static_cast<int>(base.green() + (over.green() - base.green()) * amount),
            static_cast<int>(base.blue() + (over.blue() - base.blue()) * amount));
    }

    ScintillaEditBase* editor;
    QPointer<TableCellEdit> cellEditor;
    sptr_t editFirst = -1;
    int editRow = 0;
    int editColumn = 0;
    sptr_t shown = -1;
    bool committing = false;
    bool resizing = false;
    bool hovering = false;
    sptr_t resizeFirst = -1;
    int resizeColumn = -1;
    int resizeOrigin = 0;
    int resizeStart = 0;
    int resizeWidth = 0;
};
}
