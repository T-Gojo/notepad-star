# Notepad Star Rust

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="resources/brand/notepad-star-wordmark-dark.svg">
  <img alt="Notepad Star" src="resources/brand/notepad-star-wordmark.svg" width="540">
</picture>

Original vector logos, transparent wordmarks and desktop icons are available in
the [brand kit](resources/brand/README.md).

For optimized deployment candidates, use the [desktop release procedure](packaging/RELEASE.md).
`package-release` / `installer-release` produce a versioned release candidate,
not the developer build. Publisher-signed release commands fail closed until
qualification evidence and credentials are supplied.

**Release candidate, not full Notepad++ parity.** This is a separate
Rust-led application using Qt Widgets and this checkout's Scintilla/Lexilla.
It does not replace `app\` or build the Notepad++ application in `PowerEditor`.

**Run the packaged Windows app:** double-click `Open Notepad Star.cmd` in the
repository root, or open `dist\windows-x64-0.1.0-rc.6\notepad-star.exe` in Windows
File Explorer. The optimized candidate does not show the developer-only banner.
An already running older window remains unchanged until you close it normally.
`dist` and executables are Git-ignored, so editor
views configured to hide ignored files may not show them.
The optional installer is `dist\notepad-star-windows-x64-0.1.0-rc.6-candidate-setup.exe`.
The package remains unsigned and does not change operating-system protections.

The current preview has Windows-like in-window menus, tabs, a dockable document
list, shared-document split views, selectable Lexilla lexers, literal find/replace,
undo/redo, clipboard operations, wrap, zoom and a shared light/dark theme.
Rust owns command definitions, document state, encoding, file I/O and recovery. C++ hosts
the Qt/Scintilla UI and native operations behind a CXX bridge.

Implemented additions:

- Open, Open with Encoding, Save, Save As, Save Copy, Save All, Reload, file
  drag/drop and command-line paths; save/discard/cancel when explicitly closing a tab.
- A shared 52-mode encoding catalog: UTF-8, UTF-16 with/without BOM and all 46
  named legacy code pages in the reference character-set menu. Encoding
  conversion refuses unrepresentable characters. Existing line endings remain.
- Same-directory staged and flushed saves, original-content SHA-256 conflict
  checks, read-only protection and duplicate-file detection.
- Checksummed session export/import and periodic, per-instance locked recovery
  snapshots. Normal app exit retains open tabs and unsaved text for automatic
  restoration. Recovery never writes original files.
- Match-case/whole-word literal replace and undoable replace-all, bookmarks,
  go-to-line, line duplicate/delete/movement, case conversion, sorting,
  duplicate removal, whitespace display, folding and EOL conversion.
- Read-only folder browser, language selection, rectangular/multiple selections,
  shared split buffers and independent view focus.
- A bounded recent-file menu, Open All Recent, Clear Recent File List and
  Restore Last Closed File. Successful explicit opens/saves retain saved encoding
  hints; reopening a closed file reads disk and never resurrects discarded text.
  The recent list and in-memory closed-file stack each retain at most 20 entries.

Recent-file updates merge into the latest profile with bounded conflict retries
without silently replacing another instance's preference edits. Legacy path-only
history profiles still load. History-write failures are reported separately from
successful document operations. Clearing history removes metadata, not files;
reopen attempts consume a closed-file entry so a missing file does not permanently
block access to older entries.

### Sidebar, startup and JSON tools

The app starts **maximized in a normal window**, not full-screen. Turn off
**Settings > Preferences > Start maximized (not full screen)** if you prefer the
smaller initial window. Preview and automated-test launches keep their explicit
window sizing. The editor gutter sizes itself to the line count and font, with
narrow bookmark/fold strips instead of the former broad gray band.

Reopen the left sidebar using the **Sidebar** toolbar button,
**View > Document Sidebar**, or **Ctrl+Alt+L**. Closing the dock updates the toggle.

A second **Tools** toolbar row holds the panel toggles: **Compare Tabs Panel**,
Next/Previous Difference, Comparison Options, **JSON Inspector Panel**
(`Ctrl+Alt+I`) and the **Tree / Graph / Pretty** selector. Each toggle turns its
panel on *and* off, so nothing has to be dismissed from inside a panel. The
JSON view selector lives on that toolbar rather than as tabs inside the panel, so
the inspector shows only content. View toggles are disabled while the inspector
is closed. The same toggles are in **Tools > JSON** and **Tools > Compare**.

The **Language** menu groups entries by initial letter: hover **A**, **B**, **J**
and so on to reveal the languages that start with that letter instead of one long
list. Non-alphabetic names are grouped under **#**.

**Tools > JSON** includes:

- **Pretty JSON** (`Ctrl+Alt+J`) and **Minify JSON**: format the selection, or the
  whole document when nothing is selected. One Undo restores the original text.
  Key order, duplicate keys, string escapes and exact number tokens are retained.
- **JSON Inspector Panel**: a dockable tool panel with three views. It can be
  docked left, right or below the editors, and remembers where you put it.
- **Tree**: a searchable key/type/value tree.
- **Graph**: each object or array is drawn as **one card listing its own
  key/value rows**, not one card per value. Nested keys stay in the parent card
  and link by a curved edge to their own card, so structure and data read
  together. Double-click a nested key to expand or collapse that branch; drag
  empty space to pan and use the wheel or the compact zoom/fit/collapse buttons.
- **Pretty**: read-only formatted JSON, **syntax-coloured by default** (keys,
  strings, numbers, `true`/`false`/`null` and punctuation), matching the light or
  dark editor theme.

Selecting a tree or graph node selects that exact source value in the editor.
Copy JSON pointers or original value JSON with the buttons below the views.
JSON Inspector, Pretty JSON, Preview and Graph View also appear in a tab's
right-click menu.

The JSON inspector follows the active tab and refreshes after a brief typing
pause. It is an independent tool panel: opening it no longer cancels a running
comparison, and closing it leaves the comparison and both tab groups untouched.
Invalid input clears the old preview and displays its parse error without
editing the document. All processing stays on the device: no website, upload,
plugin or service is required. Preview supports 2 MiB input, 3,000 tree nodes,
64 nesting levels, 4 KiB pointers and 400 simultaneously drawn graph cards.
Pretty output is capped at 8 MiB. Limits produce explicit messages, not truncated
documents. Duplicate keys are shown separately and flagged because their JSON
pointers can be ambiguous.

### Comparing two open tabs

There are now **two genuine editable tab groups**, not a read-only comparison
preview. Right-click a tab and choose **Move to Other View**, **Move to Left View**
or **Move to Right View**. Repeat for additional tabs, or use
**View > Document Views > Move All Tabs in This View to Other View**. Dragging a
tab out of its strip reveals both drop targets; dropping it moves that same
document, selection and undo history. Dragging within a strip still reorders tabs.
Empty groups hide automatically. New files open in the active group.

Select one tab in each group and choose **Tools > Compare > Compare These Two
Tabs** (`Ctrl+Alt+C`), also available from the tab context menu. If the other view
is still empty, comparison **adopts the adjacent tab** from the current view
automatically, so a single right-click can compare two neighbouring tabs. **Both
documents remain editable**, with their original syntax highlighting and read-only
flags. Changes on either side and changes to the selected pair automatically
recompare after a short typing pause. Full-document line differences and changed
words or characters are highlighted in the editors. No permanent comparison button
panel takes space above the documents; the main toolbar uses compact icons.

Turn comparison off from the **Compare Tabs Panel** toolbar toggle, from
**Tools > Compare > Close Comparison**, or from the same tab right-click menu that
started it — the menu now offers both starting and closing.

**Tools > Compare** contains Next/Previous Difference (`Alt+Down` / `Alt+Up`),
Swap Compared Tabs, Close Comparison and **Comparison Options**. Options select
line/word/character detail, case/whitespace/EOL/blank-line ignoring, automatic
recompare, alignment and synchronized scrolling. Ignored differences are reported
instead of being described as byte-identical files.

Matching lines align using non-editable annotations and leading viewport space,
never inserted padding in your files. Alignment temporarily turns off wrapping
and links zoom; closing comparison restores the ordinary layout. Expand any
folded/hidden lines to enable alignment. Closing comparison leaves both groups
and their documents open and removes only comparison decorations. Tab reordering,
sorting and pinning operate within their group; Close All covers both groups.
Both groups, their selected tabs, and unsaved text survive normal exit/restart.
The old same-document clone remains available as **Split Current Tab (Shared
Text)**; it is distinct from moving independent documents between groups.

Comparison supports 2 MiB and 20,000 lines per side. Patience line diff and
Myers intra-line diff share a 200 ms search deadline. Fine detail is bounded to
16 KiB per changed line, roughly 100,000 tokens and 20,000 highlight spans;
the status reports when complete line differences use coarser detail. This is
text comparison, not a structural JSON merge or full ComparePlus replacement:
Git/SVN, patch application, move detection and comparison against clipboard
content are not included in this increment.

The workflow follows the checked-in reference's
`PowerEditor\src\Notepad_plus.cpp` (`docGotoAnotherEditView`) and
`PowerEditor\src\ScintillaComponent\FindReplaceDlg.rc`. Built-in multi-view editing
and plugin comparison are distinct: see the
[ComparePlus project](https://github.com/pnedev/comparePlus) and its
[release notes](https://github.com/pnedev/comparePlus/blob/master/ReleaseNotes.txt)
for the researched auto-recompare, annotation alignment and detailed diff behavior.
This app implements its own portable workflow; it does not load that Windows DLL.

### Find and Replace popup

**Ctrl+F** and **Ctrl+H** open a reusable, modeless **Find / Replace** dialog, not
the previous bottom toolbar. It provides Find Next/Previous, Count, Replace,
Replace All in the active document, Find All in Current Document and Find All in
All Opened Documents. Match case, whole word, wrap-around, Normal/Extended/Boost
regex modes and `. matches newline` remain available. F3 / Shift+F3 navigate
matches; Escape closes the dialog without closing a document.

Find All searches unsaved buffers in **both** groups using bounded worker
snapshots. Results are grouped by document and show source-line previews;
double-click a match to activate its original tab in the correct group. Closed
or changed buffers cannot be navigated using stale offsets: rerun the search.
Count does not replace the existing result list. Errors and cancellation appear
in the dialog, and a failed search does not change documents. Limits are 10,000
total results, 32 MiB per editable source and 128 MiB of aggregate searched text.
Search previews truncate only at UTF-8 boundaries.

Find in Files remains a separate disk-search workflow. Replace All in this
dialog affects only the active document; it is not an all-open-document batch
replace. Search-result navigation and comparison never save files implicitly.

Run `cargo run --locked -p xtask -- ui-tools-test` for focused native checks at
100%, 125%, 150% and 200% display scaling.

### Remembering unsaved work

**Enabled by default:** Settings > Preferences > **Remember open tabs and unsaved
text**. Close the application normally, then launch the same profile: untitled
notes and modified files return automatically, without requiring Save As or a
routine recovery confirmation. Command-line files open after the restored tabs.
The app checkpoints the complete workspace before accepting an exit; if writing
fails, it reports the error and leaves the window and buffers open.

Text, pending encoding conversions, saved-file encoding, selection, bookmarks,
read-only state, pinned/color tabs and split-view state are retained. Undo history
is not retained across restarts. Dirty buffers keep their original save-conflict
checks; exiting or reopening never writes those edits into your original files.
Clean files refresh from disk. If a clean file is missing or cannot be decoded,
its snapshot becomes an unsaved buffer and a warning explains the problem.

Explicitly closing a dirty tab still offers **Save / Discard / Cancel**.
Discarded tabs do not return on restart. Disabling the preference, or using
`--no-session` or `--preview`, keeps the explicit exit prompts instead. Independent
windows use separate locked journals; live windows are never stolen. If closed
windows contained the same path, additional snapshots become untitled restored
copies rather than overwriting one another.

Snapshots live under the profile's `recovery\instance-*` folders, contain document
text, and are not encrypted backups. Keep the profile private and do not delete it
to clean build outputs. Legacy saved-file sessions migrate on first successful
resume; use the new app afterward rather than alternating with older candidates.

**Save important work explicitly and keep backups.** Crash checkpoints run about
every five seconds after edits, not on every keystroke, and cannot guarantee
recovery after every crash or power loss. If periodic recovery fails, the UI
reports it and pauses checkpointing; a normal exit retries persistence. Previous
snapshots remain until restored content has been checkpointed successfully.

Editable files are limited to 32 MiB, 64 documents, and 128 MiB serialized session
state. Larger files open in a separate **read-only paged preview** that reads
64 KiB at a time, with byte-offset navigation. UTF-8 pages are decoded without
splitting characters; binary/legacy/invalid pages are explicitly shown as hex.
This is bounded viewing, not full large-file editing or whole-file searching.
The sparse 1 GiB fixture qualifies page handling, not dense-file throughput.
The default code-font long-line path uses Scintilla's existing checked-monospace
optimization, including Document Map styles. Scintilla verifies actual glyph
widths before using it; proportional fonts and complex text keep their original
shaping path. Hidden maps detach their shared document and rebind when shown.
`text-test` compares public caret-position results with the optimization on/off
across fonts, styles, Unicode samples and scale factors 1/1.25/1.5/2.
`long-line-test` checks a real 32 MiB ASCII insert/select/shrink/undo/clear
workflow, with Document Map visible, against a 15-second Windows budget.
This specific path is qualified locally; it is not a general performance-parity
claim for proportional-font/complex-Unicode lines or other platforms.
Symbolic-link save destinations are refused; saving replaces the named
directory entry (other hard links retain their old content). Concurrent external
writers can still race the final replacement. Windows replacement preserves
target ACLs; macOS extended-attribute handling has not been runtime-qualified.

**Open with Encoding** and **Convert Encoding** share the Rust codec catalog.
Legacy Windows/OEM/ISO/Chinese/Japanese/Korean/Cyrillic mappings are frozen from
explicit Windows code-page APIs and used identically on both targets; ISO 8859-14
uses `encoding_rs` because Windows NLS does not supply that reference page.
ISO 8859-1 is not mislabeled Windows-1252. The reference's "GB2312" entry actually
selects Windows-936, so this app calls it GBK. No machine-dependent "ANSI" default
is guessed. UTF-16 BOM presence is selectable explicitly.
Opening/reloading observes actual BOM presence even with a Unicode codec
override; only **Convert Encoding** intentionally changes the output BOM mode.

Mappings must preserve both Unicode text and source bytes exactly. Invalid,
truncated, best-fit-only and non-reversible aliases are refused rather than
silently normalized during Save. This is deliberately stricter than permissive
Notepad++ conversions. **File > Open Read-only Byte Preview** also accepts small
files so unsupported/binary input remains inspectable without modification.
The 32 MiB limit applies to decoded UTF-8 text as well as encoded file bytes.
Mapping data and its digest/provenance live in `crates\io\src\codepages`.
On Windows, `python tools\generate_codepages.py --check` verifies the frozen
tables against the current OS; regeneration requires deliberate review.

Also implemented:

- Persistent font/tab/indent/theme options and a validated shortcut mapper.
  General, Editing and Display preference tabs cover completion sources/triggers,
  calltips, margins, guides, whitespace/EOL visibility, virtual space, multiple
  selections, backspace unindent, caret appearance/blinking and wrap indentation.
  Automatic completion uses a bounded nearby document window, counts unique
  candidates, and stays out of active IME preedit. Physical IME qualification
  on both operating systems remains required.
- Session cursor/scroll/zoom/split-view state and optional startup reopening of
  saved disk files. Explicitly discarded buffers are not reopened.
- Normal, extended and Boost-regex search modes; regex matching/replacement runs
  in a separate process with a deadline and revision checking. Zero-width searches
  advance, and invalid patterns never produce a success-shaped result.
- Find in Files with filename filters, bounded background traversal, per-file
  warnings, an explicit encoding choice and checked result navigation. Results
  and replacement previews retain their actual codec through navigation,
  preflight, save and byte-exact backup restore. This searches disk, not unsaved buffers.
- Basic edit macro recording/load/save/playback, native print/preview and PDF
  support, explicit program launching, hashes, Base64/URL conversions and JSON
  formatting that preserves large numeric values.
- Local Wasm selected-text extensions: bounded asynchronous inspection, explicit
  permission, hash pinning, isolated execution and revision/selection-checked
  undoable proposals. See `crates\extensions\README.md` for the ABI and threat model.
- **Plugins > Manage Local Extensions** installs, lists, updates, enables,
  disables and uninstalls reviewed local versions. Each version is content
  addressed, notices are copied byte-for-byte, updates retain the old version,
  and new versions start disabled. Enabling is not an execution grant: each run
  asks permission to read and replace the selected text.

Extension management runs in deadline-bounded child processes against the
private `managed-extensions` directory beside application settings. Corrupt or
interrupted installations are reported as incomplete, never runnable. Uninstall
removes only a validated version, not source packages, the root or documents.
Failed/interrupted operations require Refresh and sometimes manual inspection;
there is no speculative recursive cleanup. See
`crates\extensions\MANAGEMENT.md` for exact limits and storage/concurrency rules.

The language catalog now embeds 95 Notepad++ language profiles, actual Lexilla
engine mappings, indexed keyword lists, bundled light/dark colors, and API
completion/calltip data. UDL 2.1 profiles can be imported/exported and saved with
sessions. UDL caches are process-global upstream, so this preview bounds
configuration churn to 128 identities and keeps lexing on the editor thread.
Legacy UDL migration, substyles and a full visual UDL editor are still pending.

Three project panels load/save the Notepad++ `NotepadPlus/Project/Folder/File`
workspace structure. Project entries are links: removing one never deletes its
disk file. Relative paths are preserved; absolute paths from another OS must be
remapped by removing and re-adding the local file. Unsaved workspace edits prompt
on exit and saved-workspace conflicts are reported. Workspace edits are not
included in document recovery checkpoints.

Disk replacement now has a preview/confirmation workflow using existing Find in
Files results. It refuses unsaved open targets, preserves detected encodings,
backs up original bytes and records per-file results. A batch is not globally
atomic: a later failure may leave earlier files replaced. **Restore Replacement
Backup** verifies both backup hashes and current replacement hashes before
restoring; it refuses to overwrite subsequent external edits. Commit/restore
briefly disables editing, and worker deadlines still apply. Backups remain under
the app's `replace-backups` directory until the user removes them.

Document Map, linked split-view scrolling, file-change indicators and read-only
tail monitoring are available. Styled HTML/RTF export preserves syntax colors;
HTML text is escaped and RTF Unicode is encoded explicitly.

Function List now uses 47 embedded definitions and a separate deadline-bounded
worker with the existing Scintilla/Boost engine. Results are revision checked
before navigation. Parser definitions may be selected explicitly; unsupported
nested ranges or malformed definitions report errors instead of inventing
partial outlines. See `native\function_list\README.md` for exact limits and
documented upstream differences.

Tabs support persistent pins/colors, grouping pinned tabs first, movement,
sorting and close variants. Bulk close preflights every save/discard/cancel
prompt before removing any tab. File rename and OS Trash actions are explicit;
Trash never falls back to permanent deletion.

Full macro/configuration migration, an authenticated extension/update registry,
localization and exhaustive parity/scale qualification remain open.
The implemented subset is not a declaration of complete Notepad++ parity.

The Column Editor inserts text or checked integer sequences across multiple or
rectangular ranges, with virtual-column padding and a single undo group.
Advanced transformations include Unicode case variants, whitespace conversion,
line filtering/reversal and strict exact numeric sorting. These operations are
bounded (4 MiB input, 16 MiB output); scalar-column and strict numeric semantics
are not claims of visual-column, locale or natural-sort parity.

Language-aware line comment/toggle/uncomment and block wrapping/unwrapping use
the bundled language or simple UDL comment delimiters. They preserve mixed EOLs,
indentation and undo grouping; line toggles operate per line. Comment editing
currently accepts one stream selection and at most 4 MiB / 10,000 lines.
Grouped UDL comment markers and ambiguous nested block removals report explicit
errors; select an inner comment to remove it safely. Matching-brace navigation
and selection use Scintilla's lexer-aware matching and UTF-8 byte positions.

Bookmarked lines support copy/cut, paste-to-each-line, remove marked/unmarked
lines and inversion. Paste preserves each original line ending; deletion includes
the removed line's ending. Cut checks clipboard ownership and unchanged source
state before removing text. Batch text edits share one undo group; removed
bookmark markers themselves are not undo records, matching the reference.
Up to 10,000 affected lines and 16 MiB of inserted/copied text are supported.
Bookmarks are included in checksummed sessions/recovery, with explicit bounds.

**Settings > Import Preferences** accepts Notepad++ `config.xml` / `stylers.xml`
or a Notepad Star JSON profile. XML import previews supported changes and lists
unmapped settings; it never imports or executes program/plugin definitions.
It maps supported tab/caret/view/completion/session/dark-mode values and the
default editor font. Advanced indentation, custom colors/themes, per-language
settings and other unmapped fields remain explicitly unimported.
The source file is never modified. Before application, the exact existing
profile is saved beside settings as `settings-before-import-*.json`; those JSON
backups can be imported to restore the previous profile. Conflicts and invalid
values cancel the import. **Restore Default Preferences** can repair a malformed
profile after confirmation, retaining its original bytes and leaving documents,
sessions and installed extensions untouched.

Line operations include joining selected lines, splitting at the visible view
width, and inserting blank lines using the document EOL. **Duplicate Line**
retains the reference's whole-line behavior; **Duplicate Selection** is a
separate action and a typed macro operation. Edits are undoable and size bounded.
Large line transformations reject excessive input/line counts before allocation.

**Import Notepad++ Basic Macro** reads `shortcuts.xml` without executing it or
changing that file. Choose one macro, then explicitly Play or Save it. Supported
zero-parameter basic Scintilla edits and `SCI_REPLACESEL` text are translated to
the typed Rust macro format; literal tabs/newlines and XML escapes are preserved.
Unsupported actions reject that whole macro, while other supported macros remain
available. DTDs, external entities, duplicate names and oversized input are
rejected. Imported shortcuts, search actions, Run commands, pointer payloads and
DLL-plugin commands are not migrated or executed.
Basic recording/playback requires one stream selection and records automatic
indentation explicitly. Non-recordable menu actions are refused while recording,
not silently omitted. Playback checks document/indentation/edit-volume budgets
and a five-second deadline; an interrupted run remains one undo group.
**Repeat Loaded Macro** accepts 1-1,000 repeats, capped at 100,000 total steps.
Longer playback runs in small GUI-thread batches with a Cancel dialog; short
macros finish without opening it. Cancellation stops after the current command
and retains an undoable applied prefix. Concurrent editing, forwarding and
file-monitor reloads are excluded while the group is open. Active IME composition
must finish before recording or playback.

**Document Summary** reports UTF-8 bytes, Unicode scalars, UTF-16 units,
whitespace-delimited words, mixed-EOL counts and line lengths without allocating
a line list. The word definition is explicit, not a claim of locale-aware token
parity. Date/time insertion supports locale short/long forms and bounded custom
**Qt** patterns, not imported Notepad++ `strftime` patterns. Generated text is
undoable and never bypasses read-only or document-size checks.

## Toolchain

- Rust **1.97.1**, pinned in `rust-toolchain.toml`.
- Qt **6.8.3**: Core, Gui, Widgets, Core5Compat, Test, PrintSupport and Network.
- CMake 3.24 or newer.
- Windows: x64 MSVC, Visual Studio 2022 C++ tools and Windows SDK.
- macOS: native Apple Silicon Rust toolchain, Xcode command line tools;
  deployment target macOS 13.0. An actual Mac/runner is required for validation.

`QT_ROOT` must identify the matching Qt SDK. `xtask` also recognizes the
project-local paths below and discovers Visual Studio's CMake on Windows.
Override `CMAKE` if necessary. No SDK installation occurs implicitly during builds.

If Qt is not installed, these explicit provisioning commands use the official
Qt package repositories and a pinned provisioning tool:

```powershell
# From the repository root, on Windows:
python -m pip install aqtinstall==3.3.0
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qt5compat --outputdir .\notepad-star-rust\build\qt
Set-Location .\notepad-star-rust
cargo run --locked -p xtask -- doctor
cargo run --locked -p xtask -- run --preview
```

On an Apple Silicon Mac, run the equivalent commands from this directory:

```sh
python3 -m pip install aqtinstall==3.3.0
python3 -m aqt install-qt mac desktop 6.8.3 clang_64 -m qt5compat --outputdir build/qt
cargo run --locked -p xtask -- doctor
cargo run --locked -p xtask -- run --preview
```

Qt's `clang_64` distribution is used for the macOS SDK; CMake explicitly builds
our native code for `arm64`, and `doctor` rejects an Intel Rust host for this
initial target. Do not infer Apple Silicon correctness from the SDK label.
The portable Rust crates also type-check for `aarch64-apple-darwin` on the Windows
development host. This checks target-specific Rust code only: it does not build
or execute the Qt `.app`, run Unix tests, or qualify macOS behavior.

## Development commands

From `notepad-star-rust`:

```text
cargo run --locked -p xtask -- doctor
cargo run --locked -p xtask -- build
cargo run --locked -p xtask -- run --preview
cargo run --locked -p xtask -- test
cargo run --locked -p xtask -- lint
cargo run --locked -p xtask -- parity
cargo run --locked -p xtask -- language-test
cargo run --locked -p xtask -- function-test
cargo run --locked -p xtask -- text-test
cargo run --locked -p xtask -- long-line-test
```

The native build is currently Release-mode Qt/C++ linked into a Rust development
binary. `xtask` configures library paths for development launches. To launch the
executable independently, use the packaged directory rather than relying on
the developer's Qt installation.
C++ exception unwinding is explicitly enabled in both the CXX bridge and native
CMake targets: Cargo's native-build flags must not disable RAII cleanup on errors.
Release native targets also set optimization and `NDEBUG` explicitly, since the
Cargo/CMake adapter can otherwise replace CMake's normal Release compiler flags.

The smoke test exercises a real Qt window: file/session safety, split views,
synthetic IME, isolated regex and disk search, zero-width navigation, cancellation,
macro replay/undo, conversions, PDF output, persisted settings and a real Wasm
extension invocation. It is not a
substitute for physical IME, accessibility or full compatibility testing.

`test` uses disposable documents and packages; it does not manipulate unrelated
application windows. Search workers have a 5-second deadline, 32 MiB input limit
and 10,000-match cap. Directory search examines at most 10,000 entries and 1,000
matching files, without following symlinks. Regex parity fixtures remain incomplete.
Extension workers use tighter 2 MiB protocol limits, 256 KiB selected-text/output
limits, instruction fuel, fixed linear memory and no guest imports/WASI.

## Inventory and implementation boundaries

`compat\features.json` is a deterministic **seed inventory**, generated from
the pinned command definitions, menu resource occurrences, preference controls
and additional non-menu families. It includes aliases and range markers.
**Its entry count is not an implemented feature count.**

```text
cargo run --locked -p xtask -- inventory
cargo run --locked -p xtask -- parity
```

Review upstream changes before regenerating. Semantic reconciliation, detailed
acceptance fixtures and visual parity remain unfinished. The baseline and
platform qualification states are recorded in `compat\upstream.json` and
`compat\gates.json`. The inventory command rejects changes to the pinned
reference files rather than labeling new source as the old baseline.

| Location | Responsibility |
|---|---|
| `crates\core` | Typed commands, document identity/state and line transformations, no unsafe code |
| `crates\io` | Strict codecs, guarded replacement saves, checksummed sessions and recovery locks |
| `crates\search` | Bounded worker protocol, extended escapes and directory filters |
| `crates\extensions` | Import-free Wasm interpreter, package validation and selected-text ABI |
| `crates\native` | CXX facade and Rust callbacks with fallible state access |
| `crates\desktop` | Rust entry point and explicit prototype arguments |
| `native\qt-shell` | Qt widgets, editor views, native smoke checks |
| `native\CMakeLists.txt` | Out-of-tree Qt, Scintilla, Lexilla and Boost integration |
| `native\languages` | Embedded language/style/completion data, UDL parser and real-lexer fixtures |
| `native\function_list` | Bounded extraction rules and upstream function-list fixtures |
| `native\editor\project_panel.*` | Workspace tree and XML compatibility |
| `native\editor\rich_export.*` | Escaped styled HTML and Unicode RTF generation |
| `resources\commands` | Single command metadata source for menus and shortcuts |
| `xtask` | Environment checks, build/test, inventory and developer packaging |
| `compat` | Frozen reference metadata and incomplete parity inventory |

Scintilla owns live text and undo. Rust does not keep a second editable copy.
All widget access stays on the Qt thread; Rust state rejects stale document IDs.
Content-change notifications advance text revisions; savepoint, fold and marker
metadata do not spuriously invalidate worker snapshots or macro playback.
The UI catches callback errors and reports them rather than crossing Qt event
boundaries. Native destruction stops editor callbacks before releasing Rust state.

The application accepts `--line N`/`-nN`, `--column N`/`-cN`,
`--language NAME`/`-lNAME`, `--read-only`/`-ro`, and
`--no-session`/`-nosession`. Use `--` before filenames beginning with `-`.
`--preview` opens the welcome document without restoring or replacing the
remembered workspace. Periodic crash recovery for newly edited documents still
runs, but normal exit asks to save/discard instead of retaining those tabs.
Not every legacy Notepad++ flag is supported.

Instance reuse is **opt-in** with `--reuse-instance` on both launches. The first
launch becomes the shared window; later launches forward files, line/column,
language and read-only options, then exit after acknowledgement. Relative file
paths are resolved in the launching process. Existing unsaved buffers are
activated, not overwritten. A no-file request only activates the window.
`--new-instance` / `-multiInst` retains independent-window behavior, which is
still the default. Reuse cannot be combined with preview, screenshot, smoke-test
or no-session options.

`--profile-dir PATH` selects a dedicated application-data directory for settings,
sessions, recovery and managed extensions; use a separate directory, not a folder
containing unrelated application data. Shared instances are scoped to that profile.

The reuse channel uses local named pipes/sockets, not TCP. Windows access is
restricted to the same user. Unix sockets live inside an owned mode-0700 directory
because macOS ignores socket-file permission flags. Startup locks serialize owners;
unrecognized lock files and non-socket entries are retained and reported. Requests
contain typed open/navigation data only, never commands or document text.

Limits are 64 regular, non-symlink editable files, 1 MiB requests, 256 KiB replies
and eight clients. Windows forwarding rejects UNC/device namespaces. Large files
and byte previews use independent windows. Incomplete connections expire after
five seconds; client acknowledgements are deadline bounded. Filesystem stalls can
still outlive a client deadline, so a timeout reports **uncertain delivery**, not
success or automatic rollback. Check the existing window before retrying.
Partially successful requests report every failure. Mapped network drives and
hostile same-user filesystem races are not a sandbox guarantee.

Qt operating-system file-open events are routed through the document lifecycle.
The macOS bundle advertises text/source editing as an alternate handler, not a
default-association takeover. Real Finder/LaunchServices behavior remains unqualified.

## Developer packaging

Packaging needs the official Qt source license notices in addition to the SDK.
For a project-local Windows installation, from this directory:

```powershell
python -m aqt install-src windows 6.8.3 --archives qtbase qt5compat qtsvg --outputdir build\qt
cargo run --locked -p xtask -- package
```

Use `cargo run --locked -p xtask -- package r1` to build a separate labeled
package without changing an older package that may still be running.

On Windows, after creating a package, compile a per-user development installer:

```powershell
cargo run --locked -p xtask -- installer r1
```

This requires NSIS 3 (`MAKENSIS` may override its path). It does not install the
application on the build machine. The installer uses a separate application
directory and registry identity, and uninstall preserves user documents/settings.
Installer lifecycle testing on a clean Windows machine remains a release gate.

`packaging\windows\sign.ps1` provides a fail-closed signing hook for a publisher
certificate and timestamp service. It requires `NOTEPAD_STAR_CERTIFICATE_SHA1`,
`NOTEPAD_STAR_SIGNTOOL` and `NOTEPAD_STAR_TIMESTAMP_URL`; the standard developer
installer explicitly remains unsigned. No signing credentials are included.

Use `mac` in place of `windows` on macOS. Set `QT_SOURCE_ROOT` if the source
tree is not beside the SDK as `6.8.3\Src`.

`package` stages an unsigned developer artifact, runs Qt's deployment tool,
includes source/license provenance and Rust/Qt license notices, then launches
the packaged smoke test without SDK library search paths. It refuses to
overwrite an existing destination; move a previous package aside explicitly.

Windows output: `dist\windows-x64\notepad-star.exe`.
macOS output: the application bundle inside `dist\macos-arm64`.
macOS bundles are **not notarized or signed for public distribution**.
No Gatekeeper or Defender settings are changed.

## Validation gates still open

- macOS CI has been configured, but cannot be claimed passed without a runner result.
- Physical IME/dead-key behavior, Narrator/VoiceOver, high-contrast/RTL and
  accessibility remediation remain manual/platform validation requirements.
- Pixel-level comparison with the reference UI has not been completed.
- Command-level semantic reconciliation is not complete.
- R1 is implemented locally, with initial R2/R3/R4 functionality. The full
  R1-R7 acceptance criteria and cross-platform qualification are not complete.

The standalone workflow is `.github\workflows\rust-editor.yml`. It also runs the
language catalog, real-lexer and function-list fixture tests. It targets
Windows x64 and Apple Silicon macOS, asserts the actual Rust host architecture,
builds/tests/packages each target and uploads clearly labeled foundation
artifacts. It never signs untrusted PR artifacts.
