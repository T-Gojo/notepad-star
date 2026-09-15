#include "config_import.h"
#include "safe_xml.h"
#include <QMap>
#include <QSet>

namespace star {
namespace {
bool boolean(const QString& value, bool showHide = false) {
    if (value == (showHide ? "show" : "yes")) return true;
    if (value == (showHide ? "hide" : "no")) return false;
    throw std::runtime_error("Invalid boolean preference; import was not applied.");
}
int integer(const QString& value) {
    bool valid = false;
    const auto parsed = value.toInt(&valid);
    if (!valid) throw std::runtime_error("Invalid numeric preference; import was not applied.");
    return parsed;
}
}
QJsonObject mergeConfigPatch(QJsonObject settings, const QJsonObject& patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().isObject()) {
            settings[it.key()] = mergeConfigPatch(settings[it.key()].toObject(), it.value().toObject());
        } else settings[it.key()] = it.value();
    }
    return settings;
}
ConfigImport importNotepadConfig(const QByteArray& xml) {
    pugi::xml_document document;
    loadSafeXml(document, xml, pugi::parse_default);
    const auto root = document.child("NotepadPlus");
    if (!root) throw std::runtime_error("Expected a NotepadPlus configuration root.");
    ConfigImport result;
    QSet<QString> destinations;
    auto set = [&](const QString& section, const QString& key, const QJsonValue& value) {
        const auto destination = section + "/" + key;
        if (destinations.contains(destination)) throw std::runtime_error("Two imported preferences target the same setting.");
        destinations.insert(destination);
        if (section.isEmpty()) result.patch[key] = value;
        else {
            auto nested = result.patch[section].toObject();
            nested[key] = value;
            result.patch[section] = nested;
        }
    };
    auto unsupported = [&](const QString& key) {
        if (result.unsupported.size() >= 4096) throw std::runtime_error("Configuration import report exceeds its limit.");
        result.unsupported.append(key);
    };
    QSet<QString> sections;
    for (const auto section : root.children()) {
        if (section.type() != pugi::node_element) continue;
        const auto sectionName = QString::fromUtf8(section.name());
        if (sections.contains(sectionName)) throw std::runtime_error("Duplicate configuration sections are unsupported.");
        sections.insert(sectionName);
        if (sectionName != "GUIConfigs" && sectionName != "GlobalStyles") {
            unsupported(sectionName + " (entire section not imported)");
            continue;
        }
        QSet<QString> names;
        for (const auto node : section.children()) {
            if (node.type() != pugi::node_element) continue;
            const auto name = QString::fromUtf8(node.attribute("name").value());
            const auto path = sectionName + "/" + QString::fromUtf8(node.name()) + "[" + name + "]";
            if (name.isEmpty() || name.size() > 256 || names.contains(name))
                throw std::runtime_error("Configuration entries must have unique bounded names.");
            names.insert(name);
            const bool gui = sectionName == "GUIConfigs" && QString::fromUtf8(node.name()) == "GUIConfig";
            const bool font = sectionName == "GlobalStyles" && QString::fromUtf8(node.name()) == "WidgetStyle" && name == "Default Style";
            if (!gui && !font) { unsupported(path + " (not imported)"); continue; }
            const auto body = QString::fromUtf8(node.text().as_string()).trimmed();
            bool bodyHandled = false;
            if (gui && name == "RememberLastSession") {
                set({}, "restore_session", boolean(body)); bodyHandled = true;
            } else if (gui && (name == "MaintainIndent" || name == "MaitainIndent")) {
                if (body == "0" || body == "no") set({}, "auto_indent", false);
                else if (body == "2") set({}, "auto_indent", true);
                else if (body == "1" || body == "yes") unsupported(path + " (advanced indentation is not imported)");
                else throw std::runtime_error("Invalid indentation preference.");
                bodyHandled = true;
            }
            bool handled = bodyHandled;
            for (const auto attribute : node.attributes()) {
                const auto key = QString::fromUtf8(attribute.name());
                const auto value = QString::fromUtf8(attribute.value());
                if (key == "name" || (font && key == "styleID")) continue;
                bool imported = true;
                if (font && key == "fontName") set({}, "font_family", value);
                else if (font && key == "fontSize" && !value.isEmpty() && value != "-1" && value != "0")
                    set({}, "font_size", integer(value));
                else if (gui && name == "TabSetting" && key == "size") set("editor", "tab_width", integer(value));
                else if (gui && name == "TabSetting" && key == "replaceBySpace") set({}, "use_tabs", !boolean(value));
                else if (gui && name == "TabSetting" && key == "backspaceUnindent") set("view", "backspace_unindent", boolean(value));
                else if (gui && name == "Caret" && key == "width") set("view", "caret_width", integer(value));
                else if (gui && name == "Caret" && key == "blinkRate") set("view", "caret_period", integer(value));
                else if (gui && name == "DarkMode" && key == "enable") set("editor", "dark", boolean(value));
                else if (gui && name == "auto-completion" && key == "autoCAction") {
                    const auto mode = integer(value);
                    if (mode < 0 || mode > 3) throw std::runtime_error("Unsupported completion mode.");
                    set({}, "completion", mode != 0); set({}, "completion_mode", mode);
                } else if (gui && name == "auto-completion" && key == "triggerFromNbChar") set({}, "completion_min_chars", integer(value));
                else if (gui && name == "auto-completion" && key == "autoCIgnoreNumbers") set({}, "completion_ignore_numbers", boolean(value));
                else if (gui && name == "auto-completion" && key == "funcParams") set({}, "calltips", boolean(value));
                else if (gui && name == "ScintillaPrimaryView") {
                    static const QMap<QString, QString> shown{
                        {"lineNumberMargin", "line_numbers"}, {"bookMarkMargin", "bookmark_margin"},
                        {"indentGuideLine", "indent_guides"}, {"whiteSpaceShow", "show_whitespace"}, {"eolShow", "show_eol"}};
                    static const QMap<QString, QString> enabled{
                        {"scrollBeyondLastLine", "scroll_past_end"}, {"virtualSpace", "virtual_space"},
                        {"multiSelection", "multi_selection"}, {"columnSel2MultiEdit", "additional_typing"}};
                    if (shown.contains(key)) set("view", shown[key], boolean(value, true));
                    else if (enabled.contains(key)) set("view", enabled[key], boolean(value));
                    else if (key == "Wrap") set("editor", "wrap", boolean(value));
                    else if (key == "lineWrapMethod") {
                        static const QMap<QString, QString> modes{{"default", "fixed"}, {"aligned", "same"}, {"indent", "indent"}};
                        if (!modes.contains(value)) throw std::runtime_error("Unsupported wrap indentation.");
                        set("view", "wrap_indent", modes[value]);
                    } else imported = false;
                } else imported = false;
                if (!imported) unsupported(path + "@" + key + " (not imported)");
                handled = handled || imported;
            }
            for (const auto child : node.children())
                if (child.type() == pugi::node_element) unsupported(path + "/" + QString::fromUtf8(child.name()) + " (subtree not imported)");
            if (!body.isEmpty() && !bodyHandled) unsupported(path + " text (not imported)");
            if (!handled) unsupported(path + " (not imported)");
        }
    }
    if (result.patch.isEmpty()) throw std::runtime_error("This XML contains no supported preferences.");
    result.unsupported.removeDuplicates();
    result.unsupported.sort();
    return result;
}
}
