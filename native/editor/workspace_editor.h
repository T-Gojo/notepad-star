#pragma once
#include "ScintillaEditBase.h"
#include <QResizeEvent>
#include <QScrollBar>
#include <algorithm>

namespace star {
class WorkspaceEditor final : public ScintillaEditBase {
public:
    explicit WorkspaceEditor(QWidget* parent) : ScintillaEditBase(parent) {}
    int leadingRows = 0;
    int remainingLeadingRows = 0;
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
};
}
