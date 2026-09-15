# Retained reference material

This directory holds code that Notepad Star does **not** ship as its own
application, kept in the repository for provenance, compatibility reference and,
in one specific case, as a build-time source of compatibility data.

## `notepad-plus-plus/`

The unchanged [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus)
checkout this repository was forked from, pinned at commit
`572650c1894501ace6050ae5c90a9c70cb4691dc` (version 8.9.8). It contains the
upstream `PowerEditor` application together with the `scintilla`, `lexilla` and
`boostregex` trees that the upstream project vendors, plus the upstream build
instructions, contribution rules, CI workflows, issue templates and GPG key.

Nothing here is modified. `cargo run -p xtask -- inventory` verifies that the
pinned reference files still match the baseline commit byte-for-byte before it
will regenerate `compat/features.json`, and it refuses to relabel new source as
the old baseline. Because the trees were moved wholesale rather than edited, the
check compares tracked blob identities instead of file paths.

The Notepad++ application, installer, updater and DLL plugins are **not** built,
shipped or reused. Notepad Star does not inherit the Notepad++ publisher
identity or its code-signing material.

### What the build actually consumes

Two categories of upstream material are compiled or embedded into Notepad Star.
Everything else in this directory is reference only.

| Upstream path | Used for |
|---|---|
| `scintilla/` | The editing component, built out of tree as `star-scintilla` |
| `lexilla/` | Syntax-highlighting lexers, built as `star-lexilla` |
| `boostregex/` | This checkout's Boost.Regex integration for Scintilla searching |
| `PowerEditor/src/pugixml/` | XML parsing, built as `star-pugixml` |
| `PowerEditor/src/langs.model.xml`, `stylers.model.xml` | Language and style definitions |
| `PowerEditor/src/ScintillaComponent/` | Parsed at configure time for verified lexer mappings |
| `PowerEditor/installer/APIs/` | Auto-completion and call-tip data |
| `PowerEditor/installer/functionList/` | Function-list extraction rules |
| `PowerEditor/installer/themes/DarkModeDefault.xml` | Dark theme colours |
| `PowerEditor/bin/userDefineLangs/` | Preinstalled user-defined language definitions |
| `PowerEditor/Test/FunctionList/` | Fixtures for the function-list compatibility tests |
| `PowerEditor/src/menuCmdID.h` and the menu/preference resources | The pinned inputs to the parity inventory |

These paths are referenced from `native/CMakeLists.txt`,
`native/languages/`, `native/function_list/` and `crates/native/build.rs` through
a single `UPSTREAM` root, so relocating this directory only requires changing
that root. Licence terms for the reused sources are reproduced in `NOTICE.txt`,
`native/languages/NOTICE.txt` and `native/function_list/NOTICE.txt`, and are
copied into every package.

## `windows-cpp-preview/`

The first Notepad Star prototype: a standalone Windows C++ editor that reused
this checkout's Scintilla and Lexilla directly. It has been **superseded by the
Rust application at the repository root** and is retained only as a historical
reference.

It is not part of the Rust workspace, is not built by `xtask`, and is not
covered by CI. Its project files still build on Windows with Visual Studio 2022
if you want to run it; see `windows-cpp-preview/README.md`.
