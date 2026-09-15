# Bounded function-list definition and extraction foundation

Qt **6.8.3 Core only**, C++17. Production code does not link a regex engine or
Scintilla and does not open widgets, documents, child processes, or files selected
by parser definitions. It reads embedded XML metadata and returns names plus
explicit **UTF-8 byte locations** from an immutable snapshot supplied by the
parent.

**Extraction is synchronous and worker-only. Never call it with an editor
Scintilla instance on the GUI thread.** Every search is delegated to the parent
through a callback, which must execute inside the existing wall-clock-bounded
child-process worker. Call-count limits cannot interrupt one pathological regex.

## Integration

Inside the parent's native CMake project:

```cmake
add_subdirectory(function_list)
target_link_libraries(star-shell PUBLIC star-function-list)
```

The subdirectory installs `star-function-list` into `lib`. If Rust explicitly
links installed static archives, add **`star-function-list`** to that link list
as well. A static archive does not incorporate its dependent archive. Explicit
resource initialization retains the embedded XML even when linking the archive
directly; a standalone test verifies that case with Qt Core alone.

```cpp
#include "function_list.h"
using namespace star::function_list;

Error error;
auto definition = DefinitionCatalog::loadEmbedded(languageId, &error);
if (!definition) {
    // Report unsupported/malformed definition, not an invented empty result.
}

// CHILD PROCESS ONLY: backend owns an immutable Scintilla UTF-8 snapshot.
SearchCallback callback = [&](const SearchRequest& request) -> SearchReply {
    // Execute the contract below against the full snapshot, not a substring.
    return backendSearch(request);
};
auto result = extract(*definition, snapshotUtf8, callback, Limits{}, &error);
if (!result) {
    // Explicit failure; no partial result was published.
}
```

Public catalog and extraction signatures:

```cpp
static QStringList DefinitionCatalog::embeddedKeys();
static std::optional<Definition> DefinitionCatalog::loadEmbedded(
    const QString& key, Error* error = nullptr);
static std::optional<Definition> DefinitionCatalog::parseXml(
    const QByteArray& xml, Error* error = nullptr);

std::optional<Extraction> extract(
    const Definition& definition, const QByteArray& utf8,
    const SearchCallback& search, const Limits& limits = {},
    Error* error = nullptr);
```

Catalog keys are exact checked-in filename stems, e.g. `cpp`, `python`, `rust`,
`xml`, `javascript.js`, `cobol-free`, or `nppexec`. Use the language catalog's
**language identifier**, not its Lexilla engine name (`java` must not become
`cpp`). Unknown keys fail. There is no alias guessing, extension inference,
working-directory search, network download, or fallback to another language.
The original `overrideMap.xml` is embedded but is not a parser definition;
resolving per-user overrides, numeric langIDs, or UDL-name associations is the
parent's policy. The bundled definitions can be selected explicitly by key.

`Definition` contains `key`, parser `id`, `displayName`, `commentExpression`,
and optional top-level `FunctionRule functions` / `ClassRule classes`.
Rules expose their exact ordered expression arrays; see the header. Parsing
validates schema and bounds, **not regex syntax**. Regex compile/search errors
must be reported by the callback; the helper does not silently substitute Qt
PCRE, `std::regex`, or a different Boost configuration.

## Search callback contract

```cpp
struct ByteRange { qint64 begin; qint64 end; }; // Half-open, original UTF-8 bytes.
struct SearchRequest {
    QByteArray expression; // UTF-8, NUL-free; preserve all newlines/tabs.
    ByteRange range;
    quint32 flags;
    SearchPurpose purpose;
};
enum class SearchStatus { Found, NotFound, Failed };
struct SearchReply {
    SearchStatus status;
    ByteRange match;
    QString error;
};
using SearchCallback = std::function<SearchReply(const SearchRequest&)>;
```

* Return the **first nonempty forward match**, entirely within `range`.
* Use one full, immutable UTF-8 Scintilla document. Keep original CR/LF bytes.
  Do not search a sliced `QString`/byte buffer: that changes `^`, `$`, `\K`,
  lookbehind, document-relative anchors, and Unicode behavior.
* Set search flags to `FunctionSearchFlags = 0x10600000`, exactly upstream:
  `SCFIND_REGEXP | SCFIND_POSIX | SCFIND_REGEXP_DOTMATCHESNL`.
  There is **no `SCFIND_MATCHCASE`**, so the default is case-insensitive;
  inline `(?-i:...)` in definitions overrides it. Empty-match bits are zero.
* With `SCI_SEARCHINTARGET`, set/clear status appropriately, set target start
  and end, pass `expression.size()` and its bytes, then read both target
  endpoints on success. Treat Boost `-2` (invalid regex), `-3` (runtime failure),
  and non-OK Scintilla regex/error status as `Failed`, never `NotFound`.
  Do not turn an exception/cancellation/timeout into an empty successful list.
* With Scintilla's internal `Document::FindText`, initialize its length argument
  to the expression's byte count; it returns match length through that pointer.
  `tests/scintilla_search.cpp` is a working adapter using the exact checked-in
  document/Boost sources and UTF-8 case-folder setup.
* `Found` must provide `begin < end`, within the requested range and on UTF-8
  character boundaries. `NotFound` is reserved for an ordinary absence of a
  match. The helper rejects malformed callback output and catches exceptions.
* `SearchPurpose` is diagnostic (`Comment`, `ClassRange`, `Delimiter`,
  `OpenDelimiter`, `Function`, `Name`, `GroupName`), not permission to change
  regex semantics. The callback may not retain references to request objects.

The checked-in Boost adapter internally uses its ECMAScript/Perl-compatible
syntax selection, Unicode document iterator, inline regex modes and the
document's full-prefix context. Merely passing "POSIX" to an unrelated regex
engine is **not equivalent**. Production chooses no engine here. Standalone
tests compile actual `scintilla/src/Document.cxx`, `BoostRegExSearch.cxx`, and
`UTF8DocumentIterator.cxx` with `SCI_OWNREGEX` and `BOOST_REGEX_STANDALONE`.

## Results and deterministic bounds

```cpp
struct FunctionEntry {
    QString name;
    ByteRange nameRange;
    ByteRange matchRange;
    QString groupName;
    std::optional<ByteRange> groupNameRange;
    qint64 line;        // Zero-based.
    qint64 columnBytes; // Zero-based byte column, NOT UTF-16 or visual column.
};
struct Extraction {
    QList<FunctionEntry> entries;
    int searchCalls;
};
struct Error { ErrorCode code; QString message; };
```

Names are copied from their final matching byte ranges without replacement or
1024-byte truncation. Empty/NUL-containing names are not manufactured.
Group ranges may precede the function's match range, for example a containing
class declaration. Repeated names at different source locations remain separate.
Output is stably sorted by name byte position, then main match position,
group label and name; it does not depend on hash iteration or UI sorting.
CRLF counts as one line ending, and lone CR/LF are supported.

`Limits` defaults are also hard ceilings; callers can lower but not raise them:

| Resource | Maximum |
| --- | --- |
| Source snapshot | 4 MiB, valid UTF-8 |
| One backend match or complete class range | 1 MiB |
| One function/group name | 8 KiB UTF-8 |
| Total emitted name/group bytes | 2 MiB |
| Results | 10,000 |
| Callback calls | 100,000 |
| Comment ranges | 20,000 |
| Class ranges | 2,000 |
| Balanced delimiter depth | 256 |

Definitions additionally allow at most 512 KiB UTF-8 XML, 512 elements, 16 levels,
16 attributes per element, 64 KiB per regex and 16 successive name selectors.
Combined opening/closing delimiter regexes also obey the expression cap.
Errors are limited to 512 QString units. Size, match, count, callback and
incomplete-class failures return **no partial extraction**.

These are deterministic work/data bounds, not an instruction or wall-clock
bound for a regex call. The parent must enforce child lifetime/RSS/output caps,
cancel by killing/reaping the child when necessary, reject abnormal exits, and
revision-check the snapshot before displaying navigable positions. The helper
does not mutate files/documents or perform GUI navigation.

## Upstream behavior preserved and explicit differences

Inspected controlling sources:

* `PowerEditor/src/WinControls/FunctionList/functionParser.cpp`:
  `getUnitPaserParameters`, `getZonePaserParameters`, `funcParse`,
  `parseSubLevel`, `getCommentZones`, `getBodyClosePos`, `classParse`,
  and the unit/zone/mixed parser paths.
* `PowerEditor/src/NppXml.h::loadFileFunctionParser`.
* `functionListPanel.cpp::serialize` and
  `PowerEditor/Test/FunctionList/{rust,python,cpp,xml}` expected outputs.

The XML schema is not inferred from generic regex conventions:

* A top-level function uses `functionName/nameExpr` and optional
  `className/nameExpr`.
* A function within `classRange` uses **`functionName/funcNameExpr`**.
* Successive name expressions **narrow the preceding match range**. They are
  not alternatives and are not regex capture-group indices.
* Class names use their own ordered `nameExpr` chain. Opening/closing
  attributes really are spelled **`openSymbole` / `closeSymbole`**.
* Comments are discovered as ranges; delimiter balancing ignores matches
  starting in those ranges. Mixed parsers label methods inside class ranges
  and scan remaining ranges with the top-level function rule.
* A function with no name selectors uses its whole main match as the label.
  This may intentionally include a signature, as the upstream schemas allow.
* `displayMode` is accepted but does not format results: the inspected upstream
  parser loader also does not consume it. Other unknown elements/attributes,
  multiple rule nodes, mismatched selector tags, and incomplete delimiter pairs
  are rejected rather than silently ignored.

**Critical XML detail:** upstream pugixml disables attribute-whitespace
conversion while retaining entity and EOL processing. Standard Qt XML parsing
would turn the newlines inside `(?x)` regex attributes into spaces, causing
`#` comments to consume the remaining regex. This helper first validates the
original XML (rejecting DTDs/entities), then encodes literal attribute tabs/LFs
as character references before parsing. CRLF/lone CR normalize to LF, comments
and processing instructions remain untouched, and ordinary XML entities are
decoded once. Original source XML files are never rewritten.

Intentional bounded-foundation differences:

* Complement ranges are genuinely half-open. Upstream's `getInvertZones`
  drops an extra byte beside comments/classes; this helper does not.
* Matches ending exactly at EOF/range end are retained. Upstream sometimes
  exits before emitting them. Adjacent-comment and EOF cases are tested.
* Unmatched class delimiters fail explicitly instead of falling back to the
  class header end. Oversize labels fail instead of truncating into 1024 bytes.
* **Only flat grouping is supported.** Nested class ranges recognized by the
  definition are rejected. No second-level re-scan that repeats the outer range,
  recursive hierarchy, or invented class placeholder is produced.
* This remains regex-based, not an AST. A definition that cannot recognize
  nested/indented types cannot establish their semantic ownership. Braces in
  strings, macros, unusual language syntax, and missing constructs remain
  limitations of the supplied upstream expressions. The original Rust generic
  expression's spelling is preserved, not silently repaired.
* Output ordering is explicitly source-byte order rather than class-pass-first
  order. The representative upstream expected names and flat groups all match.

## Embedded assets, provenance and standalone tests

All 47 checked-in parser definitions and `overrideMap.xml` are embedded,
unchanged, at `:/notepad-star/function-list/PowerEditor/installer/functionList/`.
The repository GPL license is embedded too. Runtime operation needs neither the
working directory nor the upstream checkout. See `NOTICE.txt` for provenance.
No network or package install is needed.

From an MSVC x64 developer shell at the repository root:

```powershell
cmake -S .\native\function_list `
  -B .\native\function_list\_build `
  -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$PWD\build\qt\6.8.3\msvc2022_64"
cmake --build .\native\function_list\_build
ctest --test-dir .\native\function_list\_build --output-on-failure
```

`STAR_FUNCTION_LIST_BUILD_TESTS` defaults on standalone, off under
`add_subdirectory`. Tests require no GUI or live editor window. CTest injects
the selected Qt SDK's bin directory only into test processes.

Tests compare complete name/group outputs against seven original fixtures:
Rust; Python main, bad-definition and whitespace cases; both C++ fixtures;
and XML. They also verify actual byte slices/positions, UTF-8 names and emoji
prefixes, CRLF, EOF/adjacent-comment handling, deterministic repeated extraction,
all embedded schemas, preserved attribute whitespace, DTD/malformed XML,
bad regex propagation, forged callback output, flat groups, unclosed/nested
classes, and configurable work/data limits. A separate raw-archive smoke test
verifies embedding without transitive resource-link helpers or regex libraries.

Validated on Windows with MSVC and Qt 6.8.3. Non-Windows builds, all other
languages' extraction fixtures, and the parent's child-process/UI integration
need their own validation. This does not claim full R4 parity or completion of
the overall application.
