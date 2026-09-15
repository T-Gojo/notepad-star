#include "star-native/src/lib.rs.h"
#include "ScintillaEditBase.h"
#include "BoostRegexSearch.h"
#include "ILexer.h"
#include "Lexilla.h"
#include "language_catalog.h"
#include <QApplication>
#include <stdexcept>

namespace star {
bool valid_udl(rust::Str xml) {
    const QByteArray input(xml.data(), static_cast<qsizetype>(xml.size()));
    const auto decoded = QByteArray::fromBase64Encoding(input, QByteArray::AbortOnBase64DecodingErrors);
    return decoded && languages::LanguageCatalog::parseUdlXml(decoded.decoded, 1, 1).has_value();
}
bool lexer_exists(rust::Str name) {
    const std::string value(name);
    auto* lexer = CreateLexer(value.c_str());
    if (!lexer) return false;
    lexer->Release();
    return true;
}
SearchOutput search_snapshot(const SearchJob& job) {
    int argc = 1;
    char name[] = "notepad-star-search";
    char* argv[] = {name, nullptr};
    QApplication application(argc, argv);
    ScintillaEditBase editor;
    editor.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    editor.send(SCI_SETUNDOCOLLECTION, false);
    editor.sends(SCI_ADDTEXT, job.text.size(), job.text.data());
    int flags = (job.match_case ? SCFIND_MATCHCASE : 0) | (job.whole_word ? SCFIND_WHOLEWORD : 0);
    if (job.regex) flags |= SCFIND_REGEXP | SCFIND_CXX11REGEX | SCFIND_REGEXP_EMPTYMATCH_ALL |
        (job.dot_newline ? SCFIND_REGEXP_DOTMATCHESNL : 0);
    editor.send(SCI_SETSEARCHFLAGS, flags);
    // The Boost adapter consumes NUL-terminated patterns and format strings.
    const std::string pattern(job.query);
    const std::string substitution(job.replacement);
    SearchOutput output;
    output.replaced = job.replace;
    output.count = 0;
    output.next_position = 0;
    if (job.first_only) {
        editor.send(SCI_SETTARGETRANGE, static_cast<uptr_t>(job.start), static_cast<sptr_t>(job.end));
        auto found = editor.sends(SCI_SEARCHINTARGET, pattern.size(), pattern.c_str());
        if (found < -1) throw std::runtime_error("Regex search failed: " + g_exceptionMessage);
        if (found == -1 && job.wrap) {
            editor.send(SCI_SETTARGETRANGE, job.reverse ? editor.send(SCI_GETLENGTH) : 0, static_cast<sptr_t>(job.start));
            found = editor.sends(SCI_SEARCHINTARGET, pattern.size(), pattern.c_str());
            if (found < -1) throw std::runtime_error("Regex search failed: " + g_exceptionMessage);
        }
        if (found >= 0) {
            SearchHit hit;
            hit.start = static_cast<std::uint64_t>(found);
            hit.end = static_cast<std::uint64_t>(editor.send(SCI_GETTARGETEND));
            hit.line = static_cast<std::uint64_t>(editor.send(SCI_LINEFROMPOSITION, found) + 1);
            output.hits.push_back(std::move(hit));
            output.count = 1;
        }
        return output;
    }
    sptr_t position = static_cast<sptr_t>(job.start);
    sptr_t rangeEnd = static_cast<sptr_t>(job.end);
    while (position <= rangeEnd) {
        editor.send(SCI_SETTARGETRANGE, position, rangeEnd);
        const auto found = editor.sends(SCI_SEARCHINTARGET, pattern.size(), pattern.c_str());
        if (found < -1) throw std::runtime_error("Regex search failed: " + g_exceptionMessage);
        if (found < 0) break;
        const auto end = editor.send(SCI_GETTARGETEND);
        if (job.require_full_range && (found != static_cast<sptr_t>(job.start) || end != static_cast<sptr_t>(job.end))) break;
        if (++output.count > 10000) throw std::runtime_error("Search exceeded 10,000 matches. Narrow the search.");
        if (!job.replace) {
            SearchHit hit;
            hit.start = static_cast<std::uint64_t>(found);
            hit.end = static_cast<std::uint64_t>(end);
            const auto line = editor.send(SCI_LINEFROMPOSITION, found);
            hit.line = static_cast<std::uint64_t>(line + 1);
            const auto begin = editor.send(SCI_POSITIONFROMLINE, line);
            auto finish = std::min(editor.send(SCI_GETLINEENDPOSITION, line), begin + 200);
            while (finish > begin && static_cast<std::size_t>(finish) < job.text.size() &&
                (static_cast<unsigned char>(job.text.data()[static_cast<std::size_t>(finish)]) & 0xc0) == 0x80) --finish;
            const auto preview = QString::fromUtf8(job.text.data() + begin, finish - begin).toUtf8();
            hit.preview = rust::String(preview.constData(), static_cast<std::size_t>(preview.size()));
            output.hits.push_back(std::move(hit));
            position = end;
        } else {
            const auto before = editor.send(SCI_GETLENGTH);
            editor.sends(job.regex ? SCI_REPLACETARGETRE : SCI_REPLACETARGET, substitution.size(), substitution.c_str());
            if (editor.send(SCI_GETLENGTH) > 32 * 1024 * 1024)
                throw std::runtime_error("Replacement exceeds the 32 MiB limit. The source was not changed.");
            position = editor.send(SCI_GETTARGETEND);
            rangeEnd += editor.send(SCI_GETLENGTH) - before;
            if (job.replace_limit != 0 && output.count >= job.replace_limit) break;
        }
        if (found == end) {
            if (position >= editor.send(SCI_GETLENGTH)) break;
            position = editor.send(SCI_POSITIONAFTER, position);
        }
    }
    if (job.replace) {
        const auto length = editor.send(SCI_GETLENGTH);
        std::string result(static_cast<std::size_t>(length) + 1, '\0');
        editor.sends(SCI_GETTEXT, result.size(), result.data());
        result.resize(static_cast<std::size_t>(length));
        output.text = rust::String(result);
        output.next_position = static_cast<std::uint64_t>(std::min(position, length));
    }
    return output;
}
}
