# Compatibility and open gates

What Notepad Star implements, how it is measured against the retained Notepad++
reference in `reference/notepad-plus-plus`, and what is explicitly unfinished.

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


## Validation gates still open

- macOS CI has been configured, but cannot be claimed passed without a runner result.
- Physical IME/dead-key behavior, Narrator/VoiceOver, high-contrast/RTL and
  accessibility remediation remain manual/platform validation requirements.
- Pixel-level comparison with the reference UI has not been completed.
- Command-level semantic reconciliation is not complete.
- R1 is implemented locally, with initial R2/R3/R4 functionality. The full
  R1-R7 acceptance criteria and cross-platform qualification are not complete.

The continuous-integration workflow is `.github\workflows\ci.yml`. It also runs the
language catalog, real-lexer and function-list fixture tests. It targets
Windows x64 and Apple Silicon macOS, asserts the actual Rust host architecture,
builds/tests/packages each target and uploads clearly labeled foundation
artifacts. It never signs untrusted PR artifacts.
