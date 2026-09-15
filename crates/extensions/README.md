# Notepad Star extension foundation

`star-extensions` is an import-free **Wasm interpreter**, not a native plugin
loader, webview, WASI host, or general-purpose scripting environment. API v1
transforms only text explicitly selected and approved by the editor user.
It returns an edit proposal; it never opens or edits editor documents.

`managed` adds local content-addressed installation, explicit enable/disable,
validated uninstall and bounded management-worker messages. See
[MANAGEMENT.md](MANAGEMENT.md) for the storage/API contract. Installation does
not execute code or grant permission; new versions are disabled. The existing
`worker()` additionally accepts `managed::ManagedRunRequest` and rejects legacy
path-only requests targeting managed packages, including incomplete versions.

## Parent integration

Public entry points:

```rust
pub fn inspect_package(directory: &std::path::Path)
    -> star_extensions::Result<star_extensions::PackageMetadata>;
pub fn create_example(directory: &std::path::Path)
    -> star_extensions::Result<star_extensions::PackageMetadata>;
pub fn worker() -> std::io::Result<()>;
pub fn worker_with_io(input: impl std::io::Read, output: impl std::io::Write)
    -> std::io::Result<()>;
```

1. Inspect a user-selected local directory. No guest functions run during
   inspection, including start functions, which are forbidden. Inspection does
   parse, validate, translate, and instantiate bounded guest memory. Avoid doing
   potentially slow filesystem access/validation on the UI thread.
2. Display the untrusted package name, canonical native directory, module SHA-256,
   API version, and sole requested capability (`selected_text`) in a permission
   dialog. The hash is an integrity identifier, **not an author signature**.
3. After explicit approval, snapshot selection, document identity, and revision.
   Start a fresh child of the editor with `--extension-worker`, dispatched to
   `star_extensions::worker()` **before Qt or document initialization**. Do not
   reuse an existing editor process/window.
4. Send one JSON-serialized `WorkerRequest`, then **close child stdin**. Read
   stdout concurrently with sending input to avoid pipe deadlocks, and limit it
   to `MAX_RESPONSE_BYTES`. Apply a wall-clock timeout to the entire child
   lifetime (including filesystem reads, compilation and pipe I/O); kill and
   reap the child on timeout, oversize response, cancellation, or pipe failure.
   Do not use a shell. Clear unneeded environment and inherited handles.
5. Require successful child exit, exactly one valid `WorkerResponse`, and the
   expected protocol version. Errors, abnormal exits, empty/truncated output,
   extra JSON, invalid UTF-8, and timeout **never** mean a successful edit.
6. Only apply `Success.proposal.replacement` if document identity, revision, and
   selection still match the original snapshot; apply as one undoable edit.
   Neither worker responses nor package metadata select documents or ranges.

The worker request fields are:

```rust
pub struct WorkerRequest {
    pub protocol_version: u32,          // PROTOCOL_VERSION = 1
    pub package: NativePath,
    pub expected_manifest_sha256: String,
    pub granted_capabilities: Vec<Capability>, // exactly [SelectedText]
    pub selected_text: String,
}
```

`package` should be `metadata.directory`, and `expected_manifest_sha256` must be
`metadata.manifest_sha256` from the permission review. The worker rereads bounded
package files, compares the exact manifest digest, verifies the module digest,
then executes those already-read bytes. Even a manifest-only rename requires
new review; changing the module without updating the manifest fails its hash.
No missing field, permission, or version has a permissive default.

`NativePath` is reexported from `star-io`: its JSON form is
`{"platform":"Windows","units":[67,58,92,...]}` (UTF-16 units), or
`{"platform":"Unix","units":[47,...]}` (bytes). Paths are never converted through
lossy strings; NUL units, another platform's units, and excessive lengths fail.

```rust
pub struct PackageMetadata {
    pub directory: NativePath,
    pub manifest: Manifest,
    pub manifest_sha256: String,
    pub module_size: u64,
}
pub enum Capability { SelectedText }
pub struct EditProposal { pub replacement: String }
pub enum WorkerResponse {
    Success { protocol_version: u32, proposal: EditProposal },
    Error { protocol_version: u32, error: ExtensionError },
}
pub struct ExtensionError { pub code: ErrorCode, pub message: String }
```

`WorkerResponse` is internally tagged by `"status"`, with snake-case variant
names. `Capability` and `ErrorCode` use snake-case strings. Responses are one JSON
value followed by a newline, for example:

```json
{"status":"success","protocol_version":1,"proposal":{"replacement":"HELLO"}}
```

```json
{"status":"error","protocol_version":1,"error":{"code":"fuel_exhausted","message":"Extension exhausted its instruction fuel budget."}}
```

`worker()` returns an I/O error only if writing the response fails; ordinary
package/guest/protocol errors are explicit `Error` responses. Never decide edit
success solely from the child's exit code. Error strings contain no guest text,
local file contents, paths, or interpreter diagnostics.

## Local package format

The directory contains `notepad-star-extension.json` and the named binary module:

```json
{
  "schema_version": 1,
  "api_version": 1,
  "name": "ASCII uppercase",
  "module": "uppercase.wasm",
  "sha256": "<64 lowercase hexadecimal digits of the binary module SHA-256>",
  "capabilities": ["selected_text"]
}
```

These are exactly the public `Manifest` fields (versions are `u32`, text fields
are `String`, capabilities are `Vec<Capability>`). Unknown or duplicate fields,
unknown capabilities, absent/duplicate grants, and unsupported versions fail
closed. The sole supported capability list is exactly `["selected_text"]`.

The module filename must be a sibling basename, at most 128 ASCII bytes, starting
with an alphanumeric, using only alphanumerics, `.`, `_`, and `-`, and ending in
`.wasm`. Windows device names are forbidden. Absolute paths, nested paths, parent
traversal, alternate data streams, symlinks and Windows reparse points are not
allowed. The manifest must also be a regular, non-symlink file. Selected directory
ancestors must not be symlinks/reparse points. Windows UNC/device paths are
rejected. Directories may otherwise contain native non-Unicode path units.

File opens use Unix `O_NOFOLLOW | O_NONBLOCK`, or Windows
`FILE_FLAG_OPEN_REPARSE_POINT`, and verify handle metadata. The module is hashed
and executed from one immutable in-memory snapshot; it is not reopened.
Directory ancestry checks are not an OS-wide filesystem sandbox or a guarantee
against a concurrent privileged local attacker replacing an ancestor. Mapped or
mounted network filesystems are not detected as such; choose genuinely local
packages. The guest itself has no filesystem/network capabilities.

## ABI version 1

Only validated binary WebAssembly is accepted as an installed module:

* Export `memory`: one **32-bit** linear memory with standard 64-KiB pages.
* Export `alloc`: `(i32 byte_length) -> i32 pointer`.
* Export `transform`: `(i32 pointer, i32 byte_length) -> i64 packed_result`.
* The packed result uses the **unsigned pointer in the high 32 bits** and the
  **unsigned byte length in the low 32 bits**:
  `(u64(pointer) << 32) | u64(length)`.
* The host calls `alloc`, checks its entire range, copies input UTF-8 bytes, then
  calls `transform`. Output may overlap input. Both ranges are checked using
  checked addition before any host memory access or output allocation.
* Returned output must be UTF-8; NUL characters are permitted. Empty output
  means an explicit replacement with an empty string, not a failed transform.
  Even an empty range must have a pointer at or before the memory end.
* All function, memory, table, and global imports are rejected. There is no
  linker namespace, WASI, host callback, clock, random source, environment,
  filesystem, network, subprocess, native library, or external-tool API.
* Start functions and **all `memory.grow` / `table.grow` instructions** are
  rejected, even if unreachable or their failure would be swallowed. Extensions
  must provision their memory initially. A resource limiter independently
  rejects runtime growth. Extra harmless exports are allowed.
* The instance is discarded after one invocation; no `free` or persistent state
  is needed. Traps and budget failures produce errors, never a fallback.

Fixed host budgets (not guest- or request-configurable):

| Resource | Limit |
| --- | --- |
| Manifest | 16 KiB |
| Binary module | 1 MiB |
| Selected text / output | 256 KiB each, UTF-8 bytes |
| Request / response JSON | 2 MiB each, including response newline |
| Linear memory | 8 MiB initially; no growth |
| Instances / memories / tables | 1 each |
| Table elements | 1,024 initially; no growth |
| Interpreter value stack | 65,536 entries |
| Guest recursion | 128 calls |
| Instruction fuel | 10,000,000 shared by alloc and transform |

Wasmi's strict translation limits and validation are enabled. Fuel bounds guest
execution, **not parsing, compilation, host filesystem operations, or process
RSS**. Parent wall-clock enforcement remains mandatory. OS job objects, restricted
tokens, seccomp, process memory caps, and signed package distribution are outside
this crate. This is a constrained v1 foundation, not full R6 extension parity.

## Runnable example and validation

`src\uppercase.wat` is a complete ASCII-uppercase implementation. It preserves
non-ASCII UTF-8 bytes rather than attempting Unicode case folding. The WAT parser
is used only by `create_example`; installed modules must already be binary.
Creating the example invokes no WASI SDK, compiler subprocess, or network.

`create_example(directory)` requires a **nonexistent leaf directory** with an
existing parent. It uses create-new writes and never overwrites or deletes
existing files. An I/O failure may leave a partial new directory for the user to
inspect; the manifest is written last.

From the workspace on Windows:

```powershell
cargo run -p star-extensions --example create_example -- .\my-uppercase-package
cargo build -p star-extensions --example worker
cargo test -p star-extensions
cargo clippy -p star-extensions --all-targets --no-deps -- -D warnings
```

`target\debug\examples\worker.exe` implements the identical single-request protocol
for integration testing; production should dispatch the editor child instead.
Use `NativePath::from_path`, serialize the typed request, send bytes, and close
stdin rather than relying on shell text encodings.

Tests cover successful/empty/max-size UTF-8 transforms, fixed JSON bounds,
malformed/extra JSON, missing permissions, manifest/module tampering, invalid
exports and imports, start functions, bad pointers, excess output, invalid UTF-8,
fuel exhaustion, recursion, initial memory/table caps, and growth instructions.
Native path and link tests are platform-specific. Windows symlink testing reports
when the required privilege is unavailable; Unix cases require a Unix test run.

## Interpreter and licensing

Pinned Wasmi 0.46.0 (Rust 1.83 minimum) and WAT/wasmparser 1.228.0/0.228.0
(Rust 1.76 minimum) fit the workspace's declared Rust 1.85 minimum. Wasmi 2.0
was considered but requires Rust 1.86. The workspace toolchain, not the declared
minimum compiler, is used for validation; transitive dependency MSRVs are still
subject to the workspace lockfile.

API behavior was checked against:

* <https://docs.rs/wasmi/0.46.0/wasmi/>
* <https://docs.rs/wasmi/0.46.0/src/wasmi/limits.rs.html>
* <https://docs.rs/wasmi/0.46.0/src/wasmi/engine/config.rs.html>
* <https://docs.rs/wasmi/0.46.0/wasmi/struct.InstancePre.html>

The crate and example are GPL-3.0-or-later. `LICENSE` carries the project license.
`licenses` preserves the licensing documents for this crate's registry dependency
trees (excluding the pre-existing `star-io` subtree), with source/version details
in `THIRD_PARTY_NOTICES.txt`. Permissive dependencies are GPLv3-compatible.
Distributors must additionally retain the application's existing dependency
notices, including those for `star-io`, and refresh notices when dependencies
change. Runtime behavior needs no third-party downloads.
