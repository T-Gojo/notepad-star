# Security

Notepad Star opens files that the user did not write. This document records the
controls the application relies on, the result of the pre-release security
review of `0.1.0-rc.7`, and how to report a problem.

## Reporting a vulnerability

Report suspected vulnerabilities privately through GitHub Security Advisories on
this repository rather than in a public issue. Include the affected version
(`Help > About` or `notepad-star --version`), the platform, and a minimal file or
sequence that reproduces the behavior. There is no update channel in the
application, so a fix ships as a new signed package.

## Security model

- **No network, no telemetry, no auto-update.** Nothing in the product performs
  outbound HTTP. `Qt6::Network` is linked only for the local `QLocalServer` /
  `QLocalSocket` used by opt-in single-instance reuse. Document content is never
  transmitted.
- **No URL surface.** The only `QDesktopServices::openUrl` call uses
  `QUrl::fromLocalFile` for reveal-in-folder. Drag-drop and OS file-open events
  reject anything that is not a local file.
- **Inline image references are confined.** Image references parsed out of
  untrusted document text are rejected when they are UNC (`\\host\share`),
  Win32 device (`\\?\`, `\\.\`) or scheme-like (`smb://`, `http://`) paths, so
  opening a file cannot make the operating system contact a remote host.
- **Extensions are sandboxed.** The Wasm interpreter rejects every guest import
  including WASI, meters fuel, fixes linear memory at 8 MiB, traps on
  `memory.grow`/`table.grow`, bounds-checks every guest pointer, pins the module
  against a SHA-256 in the manifest, runs in a separate worker process, and
  requires an explicit user confirmation that names the digest.
- **No shell execution from content.** The only user-visible run action requires
  the user to pick the executable in a file dialog and confirm; the command is
  split with `QProcess::splitCommand` and never passed to a shell. Search workers
  are spawned from `applicationFilePath()` with a scrubbed allowlist environment,
  a 5 second deadline and an output budget.
- **XML parsing is hardened.** DTDs, external entities and entity expansion are
  disabled in every parser (language catalog, function list, config import, macro
  import), with a regression test in `native/languages/tests/catalog_tests.cpp`.
- **Everything is bounded.** File 32 MiB, settings 1 MiB, JSON 2 MiB in / 8 MiB
  out with depth 64, XML 8 MiB with depth 16, IPC request 1 MiB / response
  256 KiB, extension request 2 MiB. See `README.md` for the full budgets.
- **Saves are atomic and preserve ACLs.** Replacement saves write a sibling
  temporary file, `sync_all()`, then `ReplaceFileW` (Windows) or `persist`, and
  copy the destination's prior permissions and macOS extended attributes.
  Recovery state lives in 0700 directories with 0600 files.
- **Memory safety.** `crates/core` is `#![forbid(unsafe_code)]`,
  `unsafe_op_in_unsafe_fn` is denied workspace-wide, FFI goes through `cxx`, and
  production Rust contains exactly one `unsafe` block (the `ReplaceFileW` call).
- **Exploit mitigations.** C++ translation units are compiled with `/guard:cf`
  (MSVC) or `-fstack-protector-strong` plus `_FORTIFY_SOURCE=2` (clang), and the
  Cargo link step asserts `/guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`
  through `.cargo/config.toml`.
- **Least privilege at install time.** The Windows installer is per-user,
  `RequestExecutionLevel user`, installs under `%LOCALAPPDATA%\Programs`, writes
  only to HKCU, and preserves profile and unknown files on uninstall. The app
  manifest requests `asInvoker`.
- **Signing is fail-closed.** `packaging/windows/sign.ps1` enforces a code-signing
  EKU, validity window, SHA-256 digest, RFC 3161 timestamping and post-sign
  verification. `tools/sign_macos.py` requires Developer ID signing with the
  hardened runtime, successful notarization, stapling and a Gatekeeper
  assessment. No macOS entitlements are requested, so there is no
  `disable-library-validation` or unsigned-executable-memory escape hatch.

## Pre-release review of 0.1.0-rc.7

| # | Severity | Area | Status |
|---|----------|------|--------|
| 1 | High | Inline image references dereferenced UNC paths from untrusted document text, causing an automatic SMB/WebDAV connection that leaks Windows credentials | Fixed — `inlineImageReferenceIsRemote` in `native/editor/inline_images.h`, covered by a smoke-test assertion |
| 2 | Medium | Windows single-instance named pipe is predictable and squattable: no `FILE_FLAG_FIRST_PIPE_INSTANCE`, no user SID in the name, no `SECURITY_IDENTIFICATION` SQOS and no server-identity check | Open — requires replacing `QLocalServer`/`QLocalSocket` with direct Win32 calls. Only reachable when the user passes `--reuse-instance`; Unix transports already verify ownership, mode and socket type |
| 3 | Low | No exploit mitigations were configured for the statically linked untrusted-input parsers | Fixed — see *Exploit mitigations* above |

Known gaps deliberately left open for a later release, tracked by the
`dependency_vulnerability_audit` and `exploit_mitigations_verified` release
gates in `packaging/release-gates.json`:

- `overflow-checks` is not enabled in `[profile.release]`, so integer overflow
  wraps silently in release builds. Enabling it needs a throughput measurement
  against the long-line and large-file budgets first, because `panic = "abort"`
  turns an overflow into a process abort.
- No `cargo-audit`/`cargo-deny` step or SBOM in CI. Dependabot now watches the
  `cargo` ecosystem, but version bumps are not a release gate.
- GitHub Actions are referenced by mutable major tags, including on the
  self-hosted runner that holds the signing certificate; SHA pinning is the
  correct posture for that machine.
- `aqtinstall`, the Qt 6.8.3 SDK and NSIS 3.12 are downloaded during CI without
  hash pinning, and those tools produce the signed artifacts.
- No application sandbox: there is no macOS App Sandbox entitlement, no Windows
  AppContainer, and no `SetProcessMitigationPolicy` call at startup. Worker
  subprocesses run at full user integrity.
- Boost.Regex evaluation of user-supplied function-list patterns has call-count
  and result limits but no per-evaluation timeout, and decoded inline-image
  dimensions are unbounded even though the on-disk file is capped at 64 MiB.
  Both are denial-of-service classes only.
