#pragma once
#include <QColor>
#include <QList>
#include <QString>

namespace star {
struct StyledRun {
    QString text;
    QColor foreground;
    QColor background;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};
QString exportHtml(const QList<StyledRun>& runs, int fontSize, int tabWidth);
QByteArray exportRtf(const QList<StyledRun>& runs, int fontSize, int tabWidth);
}
