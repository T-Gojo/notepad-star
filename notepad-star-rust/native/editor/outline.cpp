#include "star-native/src/lib.rs.h"
#include "function_list.h"
#include "ScintillaEditBase.h"
#include "BoostRegexSearch.h"
#include <QApplication>
#include <stdexcept>

namespace star {
rust::Vec<rust::String> outline_keys() {
    rust::Vec<rust::String> result;
    for (const auto& key : function_list::DefinitionCatalog::embeddedKeys()) result.push_back(key.toStdString());
    return result;
}
rust::Vec<OutlineSymbol> outline_snapshot(rust::Str text, rust::Str parser) {
    int argc = 1;
    char name[] = "notepad-star-outline";
    char* argv[] = {name, nullptr};
    QApplication application(argc, argv);
    const QByteArray source(text.data(), static_cast<qsizetype>(text.size()));
    function_list::Error error;
    const auto definition = function_list::DefinitionCatalog::loadEmbedded(
        QString::fromUtf8(parser.data(), static_cast<qsizetype>(parser.size())), &error);
    if (!definition) throw std::runtime_error(error.message.toStdString());
    ScintillaEditBase editor;
    editor.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    editor.send(SCI_SETUNDOCOLLECTION, false);
    editor.sends(SCI_ADDTEXT, source.size(), source.constData());
    auto search = [&editor](const function_list::SearchRequest& request) -> function_list::SearchReply {
        editor.send(SCI_SETSTATUS, SC_STATUS_OK);
        editor.send(SCI_SETSEARCHFLAGS, request.flags);
        editor.send(SCI_SETTARGETRANGE, static_cast<uptr_t>(request.range.begin), static_cast<sptr_t>(request.range.end));
        const auto position = editor.sends(SCI_SEARCHINTARGET, request.expression.size(), request.expression.constData());
        if (position < -1 || editor.send(SCI_GETSTATUS) != SC_STATUS_OK)
            return {function_list::SearchStatus::Failed, {}, QString::fromStdString(g_exceptionMessage)};
        if (position == -1) return {function_list::SearchStatus::NotFound, {}, {}};
        return {function_list::SearchStatus::Found,
            {editor.send(SCI_GETTARGETSTART), editor.send(SCI_GETTARGETEND)}, {}};
    };
    const auto extracted = function_list::extract(*definition, source, search, {}, &error);
    if (!extracted) throw std::runtime_error(error.message.toStdString());
    rust::Vec<OutlineSymbol> result;
    for (const auto& entry : extracted->entries) {
        OutlineSymbol item;
        item.name = entry.name.toStdString();
        item.group = entry.groupName.toStdString();
        item.start = static_cast<std::uint64_t>(entry.nameRange.begin);
        item.end = static_cast<std::uint64_t>(entry.nameRange.end);
        item.line = static_cast<std::uint64_t>(entry.line);
        item.group_start = entry.groupNameRange ? entry.groupNameRange->begin : -1;
        result.push_back(std::move(item));
    }
    return result;
}
}
