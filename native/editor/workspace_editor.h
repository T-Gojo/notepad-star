#pragma once
#include "ScintillaEditBase.h"
#include "inline_images.h"
#include "inline_tables.h"
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <algorithm>
#include <functional>

namespace star {
class WorkspaceEditor final : public ScintillaEditBase {
public:
    explicit WorkspaceEditor(QWidget* parent) : ScintillaEditBase(parent) {
        // A cell edit can resize every column, so a drawn table repaints as a whole.
        connect(this, &ScintillaEditBase::modified, this, [this] { if (tables.hadTables) viewport()->update(); });
    }
    int leadingRows = 0;
    int remainingLeadingRows = 0;
    InlineImageView images{this};
    InlineTableView tables{this};
    void syncObjects() {
        tables.syncSelection();
        images.syncSelection();
    }
    // Dropped files and images go to the shell, which inserts images inline and opens
    // other files. Returns true when it handled the drop.
    std::function<bool(const QMimeData*, QPoint)> mediaDropped;
    void showLeadingRows(int rows) {
        remainingLeadingRows = std::max(0, rows);
        const auto pixels = remainingLeadingRows * send(SCI_TEXTHEIGHT, 0);
        const int available = std::max(0, height() - horizontalScrollBar()->sizeHint().height() - 4);
        setViewportMargins(0, static_cast<int>(std::min<sptr_t>(pixels, available)), 0, 0);
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        ScintillaEditBase::resizeEvent(event);
        showLeadingRows(remainingLeadingRows);
    }
    // Scintilla paints the text first; inline images then cover the lines that reference them.
    void paintEvent(QPaintEvent* event) override {
        ScintillaEditBase::paintEvent(event);
        tables.paint();
        images.paint();
    }
    // Files and dragged-in pictures are accepted here; dragged text keeps Scintilla's behavior.
    static bool carriesMedia(const QMimeData* mime) {
        return mime && (mime->hasUrls() || (mime->hasImage() && !mime->hasText()));
    }
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (carriesMedia(event->mimeData())) { event->acceptProposedAction(); return; }
        ScintillaEditBase::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (carriesMedia(event->mimeData())) { event->acceptProposedAction(); return; }
        ScintillaEditBase::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent* event) override {
        if (carriesMedia(event->mimeData()) && mediaDropped && mediaDropped(event->mimeData(), event->position().toPoint())) {
            event->acceptProposedAction();
            return;
        }
        ScintillaEditBase::dropEvent(event);
    }
    // Images behave as single objects; see InlineImageView::keyPress.
    void keyPressEvent(QKeyEvent* event) override {
        if (tables.keyPress(event) || images.keyPress(event)) { syncObjects(); event->accept(); return; }
        ScintillaEditBase::keyPressEvent(event);
        syncObjects();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (tables.mousePress(event) || images.mousePress(event)) { event->accept(); return; }
        ScintillaEditBase::mousePressEvent(event);
        syncObjects();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (tables.mouseMove(event) || images.mouseMove(event)) { event->accept(); return; }
        ScintillaEditBase::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (tables.mouseRelease(event) || images.mouseRelease(event)) { event->accept(); return; }
        ScintillaEditBase::mouseReleaseEvent(event);
        syncObjects();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (tables.mouseDoubleClick(event)) { event->accept(); return; }
        ScintillaEditBase::mouseDoubleClickEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (tables.contextMenu(event)) { event->accept(); return; }
        ScintillaEditBase::contextMenuEvent(event);
    }
};
}
