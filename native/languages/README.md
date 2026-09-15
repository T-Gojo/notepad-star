# Native language-support foundation

Qt **6.8.3**, C++17, Core/Gui only. This directory provides declarative metadata;
it does not create widgets, open editor windows, configure Scintilla directly,
run programs, download files, or modify the upstream assets. Parsing uses bounded
`QXmlStreamReader`, rejects DTDs (including UTF-16 DTDs), namespaces, malformed XML
and invalid field values, and has no external entity resolver.

## Parent integration

In the parent native CMake project:

```cmake
add_subdirectory(languages)
target_link_libraries(star-shell PUBLIC star-languages)
```

The subdirectory installs `star-languages` to `lib`. If Rust links installed
archives explicitly rather than using a CMake executable target, also add the
`star-languages` static archive to that link list. Static archives do not
automatically incorporate their dependent archives. Explicit `Q_INIT_RESOURCE`
in the catalog retains its embedded resource object even with manual linking.

```cpp
#include "language_catalog.h"
using namespace star::languages;

QString error;
auto catalog = LanguageCatalog::load(&error);
if (!catalog) {
    // Report error; do not silently replace missing/corrupt assets.
}
const Language* language = catalog->detectFileName(fileName);
if (!language) language = catalog->language(QStringLiteral("normal"));
// CreateLexer(language->engine.constData()), then apply declarations below.
```

Keep the catalog alive while retaining returned `const Language*` pointers.
Catalog construction is atomic; a failed read/parse produces `std::nullopt` and
an optional error message. Once loaded, the catalog has no mutating operations
and can be shared for concurrent const access. Completion methods parse a local
asset into an independent result, with no shared mutable cache.

Public methods:

```cpp
static std::optional<LanguageCatalog> load(QString* error = nullptr);
static std::optional<LanguageCatalog> load(const AssetPaths&, QString* error = nullptr);
const QList<Language>& languages() const;
const Language* language(const QString& identifier) const;
const Language* forExtension(const QString& extension) const;
const Language* detectFileName(const QString& fileName) const;
QList<Style> styles(const QString& identifier, Theme theme) const;
std::optional<Style> defaultStyle(Theme theme) const;
std::optional<CompletionData> completions(const QString& identifier, QString* error = nullptr) const;
static bool supportsEngine(const QByteArray& engine);
static bool supportsUserDefined();
static std::optional<ThemeData> parseThemeXml(const QByteArray&, QString* error = nullptr);
static std::optional<CompletionData> parseCompletionXml(const QByteArray&, QString* error = nullptr);
static std::optional<UdlConfiguration> parseUdlXml(
    const QByteArray&, int udlIdentity, int documentIdentity, QString* error = nullptr);
```

`Language` contains:

* `QString id`, `displayName`; `QStringList extensions`.
* `QByteArray engine`: the actual, case-sensitive Lexilla engine name.
* `QList<KeywordSet> keywords`: each has `int index`, `QByteArray text` (UTF-8).
* `QList<LexerProperty> properties`: each has `QByteArray name`, `value`.

Language IDs follow the checked-in `langs.model.xml`, not lexer names. Explicit
identifier aliases include `C++`/`cxx`, `C#`/`csharp`, `py`/`python3`, `rs`, `js`,
`shell`/`sh`, `objective-c`, and `text`. Extension lookup is case-insensitive and
accepts a leading dot. Filename detection recognizes checked-in suffixes and the
upstream special basenames (`Makefile`, `GNUmakefile`, `CMakeLists.txt`, SCons
names, `wscript`, `Rakefile`, `Vagrantfile`, `crontab`, `PKGBUILD`, `APKBUILD`).
Windows and Unix filename separators are accepted without filesystem access.
Unknown names/suffixes return `nullptr`. Upstream XML order resolves extension
collisions with the **last** language winning.

### Applying a built-in language

1. Create a fresh `ILexer5` from `engine` and transfer ownership using the
   existing editor convention. Recreating prevents stale properties/word lists.
2. Apply `properties` with `SCI_SETPROPERTY` / `ILexer5::PropertySet`.
3. Apply every keyword set using its **numeric `index`**, not vector position.
   `SCI_SETKEYWORDS` / `WordListSet` takes the UTF-8 `text`.
4. Set the theme's global `defaultStyle` (ID 32), then `SCI_STYLECLEARALL`, then
   apply the language's theme styles. Numeric style IDs are lexer-specific.
   Keep the editor's font preferences if language/theme fonts should not override
   them. This helper does not choose that UI policy.
5. Recolor after changes. Full substyle setup is explicitly outside this baseline.

`Style` has `id`, `name`, optional `QColor foreground/background`,
`colorFlags` (1=foreground, 2=background), `fontFlags`
(-1=inherit, otherwise 1=bold, 2=italic, 4=underline), `fontFamily`,
`fontSize` (0=inherit), and `quint32 nesting` (UDL). Missing colors are not black.
Honor `colorFlags` even when a disabled color has stored metadata. Convert QColor
RGB to Scintilla's color integer with `red | (green << 8) | (blue << 16)`; do not
send QColor's ARGB integer directly. Global override/editor chrome styles are
not returned as language defaults.

The default light theme is `stylers.model.xml`, the default dark theme is
`DarkModeDefault.xml`. JSON5 explicitly shares JSON styles. HTML/PHP/ASP/JSP
compose HTML plus their embedded JS/PHP/ASP style ranges. Embedded JavaScript's
standalone selection uses the modern `javascript.js` styling, as upstream does.
Custom `ThemeData` parsing does not mutate the catalog.

### Completion/calltip data

`CompletionData` contains `ignoreCase`, `startFunction`, `stopFunction`,
`parameterSeparator`, `additionalWordCharacters`, and `entries`.
Each `CompletionEntry` has `name`, `function`, and `QList<Calltip> overloads`.
Each `Calltip` carries `signature`, `returnType`, `parameters`, and `description`.
Overload signatures use the source asset's delimiters; descriptions remain plain
text. Repeated completion names are merged without dropping overloads.
The parent chooses prefix filtering, menu limits, trigger policy and calltip UI.
No semantic inference or execution is performed.

Completion lookup uses exact asset names, with explicit `javascript.js` →
`javascript.xml` and `coffeescript` → `coffee.xml` mappings. Filename case such as
`BaanC.xml` is handled independently of the host filesystem. Missing completion
assets return `nullopt` with an explanation, not invented completions.

## Verified upstream mapping, not XML-order guessing

`generate_assets.cmake` reads, but never edits:

* `PowerEditor/src/ScintillaComponent/ScintillaEditView.cpp`:
  `_langNameInfoArray` for identifiers, display names and lexer names.
* `ScintillaEditView.h`: actual `setLexer(L_..., LIST_...)` masks.
* `lexilla/lexers/Lex*.cxx` and `lexilla/src/Lexilla.cxx`:
  engine names from modules actually registered by the checked-in Lexilla.

It generates `upstream_mappings.h` in the **build directory** and tracks source
changes for CMake reconfiguration. Manual dispatcher-specific mappings were
verified against `defineDocType`, `setCppLexer`, `setJsLexer`, `setObjCLexer`,
`setTypeScriptLexer`, `setTclLexer`, `setXmlLexer`, and `setJsonLexer`.

`Parameters.cpp::getKwClassFromName` and `NppConstants.h` define semantic keyword
classes (`instre1=0`, `instre2=1`, `type1=2`, ..., `type7=8`). Their numeric aliases
are accepted, but **those class numbers are not universally lexer indices**:

* C/C++ and related languages: `instre1 → 0`, `type1 → 1`, C++'s Doxygen `type2 → 2`,
  and `instre2 → 3`; Objective-C's own `type2 → 4`.
* Python uses `instre1 → 0`, `instre2 → 1`.
* Rust uses its verified generic mask for indices 0–6.
* Tcl remaps `type1 → 1`, `instre2 → 2`.
* XML's DOCTYPE words go to **5**, not 0.
* HTML-family tags/JS/VB/PHP/DOCTYPE use 0/1/2/4/5. In particular, PHP's actual
  upstream dispatcher selects **`hypertext`**, overriding its `phpscript` table
  entry so mixed PHP/HTML receives the correct styling.
* JSON5 shares JSON keyword sets and enables comments.

Examples of nontrivial engine aliases include `ini → props`, `html → hypertext`,
`nim → nimrod`, `fortran77 → f77`, `autoit → au3`, `postscript → ps`,
`scheme → lisp`, and the case-sensitive **`COBOL`** engine.

`substyle1`–`substyle8` are recognized but intentionally **not** sent to
`SCI_SETKEYWORDS`: upstream uses `SCI_ALLOCATESUBSTYLES`/`SCI_SETIDENTIFIERS` for
them. Theme-supplied custom keyword additions, custom extension overrides,
content/shebang detection, auto-indent heuristics, function-list XML, every
upstream language-specific property, and semantic/LSP completion are not
implemented. Dispatcher changes upstream require reviewing the explicit
remappings; generation is not a source-code interpreter.

## Actual Notepad++ User Defined Language lexer

The checked-in Lexilla registers `lmUserDefine`, engine **`user`**.
`parseUdlXml` supports a single **UDL 2.1** profile in a `NotepadPlus` document.
It returns a `UdlConfiguration` containing `language`, `styles`, and the optional
profile's `darkTheme` hint. It does not install a language into the catalog or
choose how imported extensions override built-ins; the parent owns that policy.

Verified against `UserDefineDialog.h::GlobalMappers`,
`ScintillaEditView.cpp::setUserLexer`, `Parameters.cpp::feedUser*`,
`SciLexer.h` and `LexUser.cxx`:

* All 28 named keyword groups map to the actual `userDefine.*` properties or
  **15** lexer word lists. Operators2 is word list 0, folding groups are 1–6,
  Keywords1–8 are 7–14. The lexer's short word-list-description array is **not**
  a reliable list-count specification.
* Case, fold, pure-line-comment and decimal options; eight prefix switches;
  style nesting masks; comment/delimiter encodings; fonts/colors are validated.
* Style IDs derive from the actual UDL **style names**, because shipped UDLs
  often omit numeric `styleID` entirely. A supplied inconsistent ID fails.
* The upstream multiword-quote transformation is preserved: double-quoted
  internal spaces become VT, single-quoted internal spaces become BS.
  Unterminated quotes fail instead of producing partially interpreted keywords.
* Unknown groups/attributes, duplicate groups/styles, unsupported versions,
  invalid RGB/flags/nesting, numbered delimiter syntax, malformed XML/DTD,
  and keyword groups of 30,720 UTF-8 bytes or more fail. Missing groups are
  explicitly configured empty, matching the upstream container defaults.
* `udlIdentity` and `documentIdentity` must be positive `int` values. They become
  `userDefine.udlName` and `userDefine.currentBufferID`, respectively. Do **not**
  substitute truncated pointers or hashes that can collide.

**Important native-lexer limitation:** `LexUser.cxx` owns process-global caches
keyed by those IDs. The parent must allocate collision-free, monotonically
managed IDs, avoid cross-document/profile reuse, serialize UDL lexing onto its
editor thread, and recolor from byte zero after applying/replacing configuration.
No public upstream cache cleanup exists here, so unbounded import/document churn
may retain cache entries for the process lifetime. This helper does not change
the upstream lexer or claim that arbitrary native lexers are sandboxed.

Both checked-in light and dark Markdown examples are embedded unchanged and
tested against the actual `user` lexer. UDL 2.0/pre-2.0 migration, multi-profile
imports, wildcard/path extension rules, user-facing UDL editing, persistence,
cache lifecycle changes, and automatic light/dark profile pairing remain out of
scope; unsupported versions are rejected rather than emulated.

## Assets, bounds, licensing

Original language/theme/completion/UDL XML and their comments are embedded with
Qt resources under `:/notepad-star/languages/PowerEditor/...`. The original GPL
and Lexilla license texts are also embedded. Runtime lookup does **not** depend
on the current directory, repository checkout, or a source absolute path.
`AssetPaths::embedded()` provides exact paths; the overload accepts explicit
absolute filesystem or Qt resource paths only. No environment search or network
fallback is performed.

XML inputs are bounded to 8 MiB, 100,000 elements, 16 nested elements and 32
attributes per element. Only a global default style plus lexer style metadata
is returned. These limits bound parsing, not a downstream native lexer's runtime.

New helper/test code is GPL-3.0-or-later, consistent with the application.
See `NOTICE.txt` for exact asset/source provenance. Existing upstream files are
unchanged; generated resources preserve their content and license comments.

## Standalone build and tests

From an MSVC x64 developer shell at the repository root:

```powershell
cmake -S .\native\languages `
  -B .\native\languages\_build `
  -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$PWD\build\qt\6.8.3\msvc2022_64"
cmake --build .\native\languages\_build
ctest --test-dir .\native\languages\_build --output-on-failure
```

Tests default on for standalone use, off for `add_subdirectory`; override with
`STAR_LANGUAGES_BUILD_TESTS`. CTest prepends the selected Qt SDK's bin directory
to the test process PATH. No Qt platform plugin or display is required.

The test target compiles upstream C++/Python/Rust/User lexers plus Lexilla's
lexer-support sources independently of the parent's build. An in-memory ASCII
`IDocument` test double verifies real lexical styles, not merely metadata.
Additional tests cover aliases/extensions, keyword XML reordering, every
completion XML asset, light/dark colors and font flags, DTDs including UTF-16,
malformed/oversize XML, UDL validation, both Markdown profiles, keyword list 14,
quoted UDL phrases, explicit paths and working-directory independence.
A separate raw-archive link smoke test verifies that resources remain available
without CMake's transitive resource-initialization object, as required by an
explicit Rust static-library link.

The Windows MSVC/Qt 6.8.3 standalone target was built and tested. Non-Windows
compilation and the parent's widget/menu/completion integration require their
own validation. No live editor windows are used by these tests.
