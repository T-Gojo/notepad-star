# Notepad Star

A new native Windows text editor with a familiar Notepad++-style layout. This is
an independent **development preview**, not a renamed Notepad++ executable.
The application shell and file handling are new; only Scintilla, Lexilla and
the existing Scintilla build's Boost.Regex integration are reused.

## Build and preview

Requires 64-bit Windows 10/11, Visual Studio 2022 **Desktop development with C++**
(MSVC v143), and a Windows SDK. No npm packages, installer, administrator rights,
or downloaded application binaries are required.

From the repository root in PowerShell:

```powershell
.\app\build.ps1 -Run
```

This builds the Debug configuration and opens an editable welcome document.
For a Release build and the file I/O and desktop UI checks:

```powershell
.\app\build.ps1 -Configuration Release -Test
```

The UI checks briefly open a separate editor with disposable test documents;
they do not send global keystrokes or touch your open documents. Run them in
an interactive Windows desktop session.

Launch the application directly, optionally with file paths:

```powershell
.\app\build\Release\notepad-star.exe
.\app\build\Release\notepad-star.exe "C:\Work\example.cpp" "C:\Work\notes.txt"
```

Open `app\NotepadStar.vcxproj` in Visual Studio to develop and debug the new app.
Its project references build the existing Scintilla and Lexilla projects.
The original `PowerEditor` application is not compiled or modified.

## Working features

- Independent tabs with separate undo history, selection and scroll position.
- New, open, drag-and-drop, save, save as, save all, and save/discard/cancel on close.
- UTF-8 with/without BOM and BOM-marked UTF-16 LE/BE. Encoding and existing line
  endings are preserved. Invalid encodings and binary/NUL-containing input are
  rejected instead of silently converted.
- C/C++, JavaScript/TypeScript, Python, JSON, HTML, XML, CSS, SQL and Markdown
  lexers, selected by extension or the Language menu.
- Line numbers, indentation guides, automatic indentation, brace highlighting,
  word wrap, zoom, and light/dark editor colors. Window chrome uses Windows' theme.
- Literal find/replace with case and whole-word options, next/previous with
  wraparound, and replace-all grouped into a single undo action.
- Status bar showing language, line/column, encoding and the newline mode for
  newly entered lines. Existing mixed line endings are not normalized.

Common shortcuts: `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+W`,
`Ctrl+F`/`Ctrl+H`, `F3`/`Shift+F3`, `Ctrl+Tab`/`Ctrl+Shift+Tab`, and `Alt+Z`.
Undo/redo and clipboard shortcuts work directly in the editor.

## Structure

| Location | Responsibility |
|---|---|
| `src\main.cpp` | Window, tabs, document lifecycle, menus, search, themes, lexer selection |
| `src\file_io.*` | Strict Unicode decoding/encoding, file reads, guarded replacement saves |
| `src\resource.h`, `src\app.rc` | Commands, menus, keyboard accelerators and app version |
| `app.manifest` | Independent application identity, per-monitor DPI, standard-user execution |
| `tests\file_io_tests.cpp` | Byte-exact round trips, size boundaries and save protection |
| `tests\smoke.ps1` | Real-window editing and navigation regression checks |
| `..\scintilla`, `..\lexilla`, `..\boostregex` | Reused editor libraries, unchanged |

## Preview boundaries and data safety

This is a foundation to develop, not a feature-complete Notepad++ replacement.
It currently supports x64, 64 open tabs and input files up to 32 MiB. It does not
include plugins, an updater, shell integration, an installer, regex search,
printing, a session restore system, autosave/crash recovery or persistent
preferences. Save work before exiting. No Notepad++ settings or registry
identities are shared, and no app-initiated network requests are made.

Saving writes and flushes a uniquely named temporary file in the destination
directory before replacing the target. Existing files are compared with their
original bytes before replacement; detected external edits or deletion stop
the save. Use Save As to resolve a conflict. This is not a filesystem-wide lock:
another process can still race the final replacement. Read-only and I/O failures
are reported and do not mark the document saved.

## Distribution and Windows warnings

The build script copies the GPL and dependency license notices beside the
executable. Preserve them and provide corresponding source when distributing.
The Boost license copy comes from <https://www.boost.org/LICENSE_1_0.txt>.

Local builds are unsigned. A successful build or a new product name does not
prove safety or guarantee Defender/SmartScreen acceptance. Release publishing
and signing are separate future work; use your own publisher identity, not
Notepad++'s. The application and build scripts never disable Windows security,
add exclusions, or bypass installation warnings.
