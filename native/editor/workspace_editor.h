#pragma once
#include "ScintillaEditBase.h"
#include "inline_images.h"
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <algorithm>

namespace star {
class WorkspaceEditor final : public ScintillaEditBase {
public:
    explicit WorkspaceEditor(QWidget* parent) : ScintillaEditBase(parent) {}
    int leadingRows = 0;
    int remainingLeadingRows = 0;
    InlineImageView images{this};
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
        images.paint();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (images.mousePress(event)) { event->accept(); return; }
        ScintillaEditBase::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (images.mouseMove(event)) { event->accept(); return; }
        ScintillaEditBase::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (images.mouseRelease(event)) { event->accept(); return; }
        ScintillaEditBase::mouseReleaseEvent(event);
    }
};
}
