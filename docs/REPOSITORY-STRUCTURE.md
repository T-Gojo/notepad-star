# Repository structure

This repository contains one product, **Notepad Star**, plus the upstream
material it was forked from. The application therefore lives at the repository
root and the retained upstream code is isolated under `reference/`.

## Layout

```text
notepad-star/
├── Cargo.toml              Rust workspace manifest (the workspace root is the repo root)
├── Cargo.lock              Committed: release builds must be reproducible
├── rust-toolchain.toml     Pinned Rust 1.97.1
├── README.md               Product overview
├── LICENSE                 GNU GPL v3, governing this application
├── NOTICE.txt              Third-party attribution
├── Open Notepad Star.cmd   Launches the packaged Windows build
│
├── crates/                 Rust workspace members
│   ├── core/               Typed commands, document identity/state, text transformations
│   ├── io/                 Codecs, guarded saves, sessions, recovery locks, code pages
│   ├── search/             Bounded search workers, escapes, directory filters
│   ├── extensions/         Wasm extension host, package validation, ABI
│   ├── native/             CXX bridge facade and Rust callbacks
│   └── desktop/            Binary entry point and command-line handling
│
├── native/                 C++ that Rust links against
│   ├── CMakeLists.txt      Out-of-tree Qt, Scintilla, Lexilla and Boost integration
│   ├── qt-shell/           Main window, tab groups, JSON panel, search dialog
│   ├── editor/             Search tasks, project panel, export, macros, IPC channel
│   ├── languages/          Embedded language/style/completion data and the UDL parser
│   └── function_list/      Bounded function extraction and upstream fixtures
│
├── resources/              Command metadata, menu layout, icons, brand assets
├── packaging/              Release procedure, installer, signing hooks, licences
├── xtask/                  Build/test/lint/inventory/package automation
├── compat/                 Frozen upstream metadata, parity inventory, gates
├── tools/                  Python helpers (code pages, icons, source archive, signing)
├── docs/                   This documentation set
│
├── reference/              Retained, not shipped as this product
│   ├── notepad-plus-plus/  Unchanged upstream checkout (see reference/README.md)
│   └── windows-cpp-preview/  Superseded standalone C++ prototype
│
├── build/                  Local SDKs and standalone test builds (ignored)
├── target/                 Cargo output (ignored)
└── dist/                   Packages and installers (ignored)
```

## Why this shape

**The application is at the root.** This repository exists to build Notepad
Star, so its workspace manifest, crates and native sources are top level. There
is no nested product directory to `cd` into: every `cargo run -p xtask -- …`
command runs from the repository root.

**Upstream code is quarantined under `reference/`.** The Notepad++ checkout is
kept unmodified for provenance and compatibility work, and it is the only place
upstream sources may live. A single `UPSTREAM` root in the build files points at
it, so it can be relocated or updated without touching the rest of the tree. See
[`reference/README.md`](../reference/README.md) for exactly which retained files
the build consumes.

**Generated output is never committed.** `build/`, `target/` and `dist/` are
ignored, as are the upstream trees' own build products. `Cargo.lock` is the
deliberate exception, because release candidates must be reproducible.

**Command metadata has one source.** `resources/commands/commands.json` and
`resources/commands/menu-layout.json` define command IDs, labels, shortcuts,
checkability and menu placement. The Rust command enum in `crates/core` and the
Qt menu construction in `native/qt-shell` both derive from them, so all three
must stay in sync.

## Where things run

| Task | Command (from the repository root) |
|---|---|
| Check prerequisites | `cargo run --locked -p xtask -- doctor` |
| Build and launch | `cargo run --locked -p xtask -- run --preview` |
| Rust and native tests | `cargo run --locked -p xtask -- test` |
| Formatting and lint | `cargo run --locked -p xtask -- lint` |
| Parity inventory | `cargo run --locked -p xtask -- parity` |
| Developer package | `cargo run --locked -p xtask -- package` |

The full command list is in the [development guide](DEVELOPMENT.md).
