#pragma once
#include "ScintillaEditBase.h"
#include <QByteArray>
#include <algorithm>

namespace star {
inline void compactGutter(ScintillaEditBase* editor, bool numbers = true, bool bookmarks = true, bool folds = true) {
    const auto digits = std::max<qsizetype>(2, QByteArray::number(static_cast<qlonglong>(editor->send(SCI_GETLINECOUNT))).size());
    const QByteArray sample(digits, '9');
    const auto width = numbers ? editor->sends(SCI_TEXTWIDTH, STYLE_LINENUMBER, sample.constData()) + 8 : 0;
    if (editor->send(SCI_GETMARGINWIDTHN, 0) != width) editor->send(SCI_SETMARGINWIDTHN, 0, width);
    editor->send(SCI_SETMARGINWIDTHN, 1, bookmarks ? 10 : 0);
    editor->send(SCI_SETMARGINWIDTHN, 2, folds ? 12 : 0);
    editor->send(SCI_SETMARGINLEFT, 0, 3);
    editor->send(SCI_SETMARGINRIGHT, 0, 3);
}
}
