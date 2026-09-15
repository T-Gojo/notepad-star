# Summary

<!-- What does this change and why? -->

## Area

<!-- Which part of the tree does this touch? See docs/REPOSITORY-STRUCTURE.md -->

- [ ] `crates/` (Rust application logic)
- [ ] `native/` (Qt/Scintilla shell)
- [ ] `resources/` (commands, menus, icons, brand)
- [ ] `packaging/`, `xtask/` or `tools/` (build and release)
- [ ] `docs/` only

## Checks

<!-- Run from the repository root. Tick what you actually ran. -->

- [ ] `cargo run --locked -p xtask -- test`
- [ ] `cargo run --locked -p xtask -- lint`
- [ ] `cargo run --locked -p xtask -- ui-tools-test`
- [ ] Other fixtures (`language-test`, `function-test`, `text-test`)

## Confirmations

- [ ] `reference/notepad-plus-plus` is unchanged
- [ ] New or changed commands are reflected in `resources/commands/` **and** the
      Rust command enum **and** the Qt menu construction
- [ ] No operating-system security protection is disabled or worked around
- [ ] Claims in the description match what the tests actually validate
