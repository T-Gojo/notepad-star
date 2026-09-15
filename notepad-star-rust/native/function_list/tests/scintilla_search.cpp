// SPDX-License-Identifier: GPL-3.0-or-later
#include "scintilla_search.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <algorithm>
#include <memory>
#include "Scintilla.h"
#include "ScintillaTypes.h"
#include "ILoader.h"
#include "ILexer.h"
#include "Debugging.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "BoostRegexSearch.h"

using namespace Scintilla;
using namespace Scintilla::Internal;
using namespace star::function_list;

static_assert(FunctionSearchFlags == (SCFIND_REGEXP | SCFIND_POSIX | SCFIND_REGEXP_DOTMATCHESNL));

// Standalone test diagnostics, following scintilla/test/unit/unitTest.cxx.
void Platform::Assert(const char* condition, const char* file, int line) noexcept {
    std::fprintf(stderr, "Scintilla assertion %s at %s:%d\n", condition, file, line);
    std::abort();
}
void Platform::DebugPrintf(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

struct ScintillaSearch::Impl {
    Document document{DocumentOption::Default};
    explicit Impl(const QByteArray& source) {
        document.SetDBCSCodePage(SC_CP_UTF8);
        document.SetCaseFolder(std::make_unique<CaseFolderUnicode>());
        document.InsertString(0, source.constData(), source.size());
    }
};

ScintillaSearch::ScintillaSearch(const QByteArray& source) : impl_(std::make_unique<Impl>(source)) {}
ScintillaSearch::~ScintillaSearch() = default;

SearchReply ScintillaSearch::operator()(const SearchRequest& request) {
    Sci::Position length = request.expression.size();
    const auto position = impl_->document.FindText(
        request.range.begin, request.range.end, request.expression.constData(),
        static_cast<FindOption>(request.flags), &length);
    if (position == -1) return {SearchStatus::NotFound, {}, {}};
    if (position < -1) return {SearchStatus::Failed, {}, QString::fromStdString(g_exceptionMessage)};
    return {SearchStatus::Found, {position, position + length}, {}};
}
