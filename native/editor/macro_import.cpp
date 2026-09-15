#include "macro_import.h"
#include "safe_xml.h"
#include "Scintilla.h"
#include <QJsonObject>
#include <QSet>
#include <stdexcept>

namespace star {
QList<ImportedMacro> importNotepadMacros(const QByteArray& xml, const std::map<unsigned int, QString>& allowedCommands) {
    pugi::xml_document document;
    // Match NppXml::loadFileShortcut: preserve literal attribute whitespace and EOL bytes.
    loadSafeXml(document, xml, pugi::parse_cdata | pugi::parse_escapes | pugi::parse_comments | pugi::parse_declaration);
    const auto root = document.child("NotepadPlus");
    const auto macros = root.child("Macros");
    if (!macros) throw std::runtime_error("No Notepad++ Macros section found.");
    if (macros.next_sibling("Macros")) throw std::runtime_error("Duplicate Macros sections are unsupported.");
    QList<ImportedMacro> result;
    QSet<QString> names;
    for (const auto node : macros.children()) {
        if (node.type() != pugi::node_element) continue;
        if (QString::fromUtf8(node.name()) != "Macro") throw std::runtime_error("Unsupported element in Macros.");
        ImportedMacro item;
        item.name = QString::fromUtf8(node.attribute("name").value());
        if (item.name.isEmpty() || item.name.toUtf8().size() > 128 || names.contains(item.name) || result.size() >= 256)
            throw std::runtime_error("Macro names must be unique and bounded.");
        names.insert(item.name);
        int count = 0;
        qsizetype totalText = 0;
        for (const auto action : node.children()) {
            if (action.type() != pugi::node_element) continue;
            if (++count > 4096) { item.error = "Too many macro actions."; break; }
            if (QString::fromUtf8(action.name()) != "Action") { item.error = "Unsupported macro child."; continue; }
            for (const auto child : action.children()) if (child.type() == pugi::node_element) item.error = "Action children are unsupported.";
            const QSet<QString> attributes{"type", "message", "wParam", "lParam", "sParam"};
            for (const auto attribute : action.attributes())
                if (!attributes.contains(QString::fromUtf8(attribute.name()))) item.error = "Unknown action attribute.";
            bool typeOk = false;
            bool messageOk = false;
            const int type = QString::fromUtf8(action.attribute("type").value()).toInt(&typeOk);
            const auto message = QString::fromUtf8(action.attribute("message").value()).toUInt(&messageOk);
            const auto wParam = QString::fromUtf8(action.attribute("wParam").as_string("0"));
            const auto lParam = QString::fromUtf8(action.attribute("lParam").as_string("0"));
            const auto text = QString::fromUtf8(action.attribute("sParam").value());
            if (!typeOk || !messageOk || wParam != "0" || lParam != "0") {
                item.error = "Only supported zero-parameter basic edits can be imported."; continue;
            }
            if (type == 1 && message == SCI_REPLACESEL) {
                totalText += text.toUtf8().size();
                if (totalText > 1024 * 1024 || text.contains(QChar(0))) { item.error = "Macro text exceeds its limit."; continue; }
                item.steps.append(QJsonObject{{"kind", "Insert"}, {"text", text}});
            } else if (type == 0 && text.isEmpty() && allowedCommands.find(message) != allowedCommands.end()) {
                item.steps.append(QJsonObject{{"kind", "Command"}, {"command", allowedCommands.at(message)}});
            } else item.error = "This macro includes actions outside the safe basic-edit format.";
        }
        if (!item.error.isEmpty()) item.steps = QJsonArray();
        result.push_back(std::move(item));
    }
    if (result.isEmpty()) throw std::runtime_error("No macros were found.");
    return result;
}
}
