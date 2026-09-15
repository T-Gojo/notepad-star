# Contributing to Notepad Star

Thanks for your interest. This document covers how to work in this repository.
It is about **Notepad Star**, the application at the repository root. For the
retained Notepad++ checkout under `reference/notepad-plus-plus`, take issues and
patches to the [upstream project](https://github.com/notepad-plus-plus/notepad-plus-plus)
instead.

## Before you start

Read [docs/REPOSITORY-STRUCTURE.md](docs/REPOSITORY-STRUCTURE.md) to see where
things live, then [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) to install the
pinned Rust toolchain, Qt SDK and C++ build tools.

All commands run from the repository root:

```powershell
cargo run --locked -p xtask -- doctor
```

## Making a change

1. **Work in the right layer.** Editor logic, document state, encodings, search
   and extension hosting belong in `crates/`. Widgets, editor views and native
   integration belong in `native/`. Rust does not keep a second editable copy of
   document text; Scintilla owns live text and undo.
2. **Add or change commands in one place.** `resources/commands/commands.json`
   and `resources/commands/menu-layout.json` are the single source of truth for
   command IDs, labels, shortcuts, checkability and menu placement. The command
   enum in `crates/core/src/lib.rs` and the Qt menu construction in
   `native/qt-shell/shell.cpp` must stay in sync with them.
3. **Never modify `reference/notepad-plus-plus`.** The parity inventory verifies
   that the pinned reference files still match the baseline commit and will fail
   the build if they change.
4. **Cover the change with the existing harnesses.** Rust changes get unit tests
   in the owning crate; UI and interaction changes get assertions in the native
   smoke and UI-tools checks, which run at multiple display scales.

## Before you open a pull request

Run the same checks CI runs:

```powershell
cargo run --locked -p xtask -- test
cargo run --locked -p xtask -- lint
cargo run --locked -p xtask -- language-test
cargo run --locked -p xtask -- function-test
cargo run --locked -p xtask -- text-test
cargo run --locked -p xtask -- ui-tools-test
```

`lint` enforces `cargo fmt` and Clippy for the workspace, and type-checks the
portable crates for `aarch64-apple-darwin`. Native C++ targets are built with
warnings as errors.

If you changed the upstream reference baseline deliberately, regenerate the
inventory with `cargo run --locked -p xtask -- inventory` and explain why in the
pull request.

## Reporting issues

Please include your operating system and version, the Notepad Star version from
**Help → About** or the package manifest, exact steps to reproduce, and what you
expected instead. For crashes or data loss, say whether the document was
unsaved, which encoding it used and roughly how large it was.

## Scope and honesty

This project does not claim Notepad++ parity. `compat/features.json` is a seed
inventory, not a feature checklist, and
[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) lists the gates that are still
open. Please describe what a change actually validates rather than what it is
expected to imply, and do not mark a gate closed without evidence.

Notepad Star does not reuse Notepad++ binaries, its installer, its updater or
its DLL plugins, and it does not adopt its publisher identity. Changes must not
disable or work around operating-system security protections.

## License

By contributing you agree that your work is licensed under the
[GNU GPL v3](LICENSE), the same terms as the rest of this repository.
