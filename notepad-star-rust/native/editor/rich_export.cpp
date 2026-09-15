#include "rich_export.h"
#include <QMap>
#include <stdexcept>

namespace star {
QString exportHtml(const QList<StyledRun>& runs, int fontSize, int tabWidth) {
    QString result = QString("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Notepad Star export</title></head>"
        "<body><pre style=\"font-family:monospace;font-size:%1pt;tab-size:%2\">").arg(fontSize).arg(tabWidth);
    for (const auto& run : runs) {
        result += QString("<span style=\"color:%1;background-color:%2;font-weight:%3;font-style:%4;text-decoration:%5\">%6</span>")
            .arg(run.foreground.name(), run.background.name(), run.bold ? "bold" : "normal",
                run.italic ? "italic" : "normal", run.underline ? "underline" : "none", run.text.toHtmlEscaped());
        if (result.size() > 16 * 1024 * 1024) throw std::runtime_error("Styled HTML export exceeds its size limit.");
    }
    return result + "</pre></body></html>";
}
QByteArray exportRtf(const QList<StyledRun>& runs, int fontSize, int tabWidth) {
    QMap<QRgb, int> colors;
    QList<QColor> table;
    for (const auto& run : runs) for (const auto& color : {run.foreground, run.background})
        if (!colors.contains(color.rgb())) { table.push_back(color); colors.insert(color.rgb(), static_cast<int>(table.size())); }
    QByteArray result = "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 Courier New;}}{\\colortbl;";
    for (const auto& color : table)
        result += "\\red" + QByteArray::number(color.red()) + "\\green" + QByteArray::number(color.green()) +
            "\\blue" + QByteArray::number(color.blue()) + ";";
    result += "}\\f0\\fs" + QByteArray::number(fontSize * 2) + "\\deftab" + QByteArray::number(fontSize * tabWidth * 12) + " ";
    bool carriageReturn = false;
    for (const auto& run : runs) {
        result += "\\cf" + QByteArray::number(colors.value(run.foreground.rgb())) +
            "\\highlight" + QByteArray::number(colors.value(run.background.rgb())) +
            (run.bold ? "\\b" : "\\b0") + (run.italic ? "\\i" : "\\i0") +
            (run.underline ? "\\ul " : "\\ul0 ");
        for (const auto ch : run.text) {
            const int unit = ch.unicode();
            if (unit == '\r') { result += "\\par\n"; carriageReturn = true; continue; }
            if (unit == '\n') { if (!carriageReturn) result += "\\par\n"; carriageReturn = false; continue; }
            carriageReturn = false;
            if (unit == '\\' || unit == '{' || unit == '}') { result += '\\'; result += static_cast<char>(unit); }
            else if (unit == '\t') result += "\\tab ";
            else if (unit >= 32 && unit < 127) result += static_cast<char>(unit);
            else result += "\\u" + QByteArray::number(unit <= 32767 ? unit : unit - 65536) + "?";
        }
        if (result.size() > 16 * 1024 * 1024) throw std::runtime_error("RTF export exceeds its size limit.");
    }
    return result + "}";
}
}
