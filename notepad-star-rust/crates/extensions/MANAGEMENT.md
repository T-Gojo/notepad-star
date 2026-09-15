# Managed local Wasm packages

This is a local-only R6 package-management foundation, not a registry, downloader,
publisher-signature system, or complete R6 implementation. No new dependency is
needed. No management operation calls a guest function or grants execution
permission. **Every newly installed version is disabled.**

All APIs below are in `star_extensions::managed`. `Result<T>` is the existing
`Result<T, ExtensionError>`. Paths use the existing lossless `NativePath` wire
representation; direct path arguments must be explicit absolute local paths.

## Public APIs

```rust
pub fn initialize_managed_root(directory: &Path) -> Result<NativePath>;
pub fn inspect_install_source(source: &Path) -> Result<PackageReview>;
pub fn install_package(
    root: &Path, source: &Path,
    expected_manifest_sha256: &str, expected_snapshot_sha256: &str,
) -> Result<InstallResult>;
pub fn update_package(
    root: &Path, previous: &ManagedId, source: &Path,
    expected_manifest_sha256: &str, expected_snapshot_sha256: &str,
) -> Result<InstallResult>;
pub fn list_installed(root: &Path) -> Result<Vec<InstalledEntry>>;
pub fn set_enabled(root: &Path, id: &ManagedId, enabled: bool)
    -> Result<InstalledPackage>;
pub fn uninstall_package(root: &Path, id: &ManagedId) -> Result<()>;
pub fn run_managed(request: ManagedRunRequest) -> Result<EditProposal>;

pub fn handle_management(request: ManagementRequest) -> Result<ManagementResult>;
pub fn management_worker() -> std::io::Result<()>;
pub fn management_worker_with_io(input: impl Read, output: impl Write)
    -> std::io::Result<()>;
```

`ManagedId` is a private-field string newtype with:

```rust
pub fn ManagedId::parse(value: &str) -> Result<ManagedId>;
pub fn ManagedId::as_str(&self) -> &str;
```

It implements `Display`, ordering, cloning and Serde. Parsing/deserialization
accepts **exactly 64 lowercase hexadecimal SHA-256 digits**. It never normalizes
or accepts arbitrary child paths. An ID is the hash of the exact manifest bytes,
not the module hash or display name.

`initialize_managed_root` creates a new leaf directory under an existing parent,
or verifies an already initialized managed root. It refuses filesystem roots,
relative paths and existing unrelated/empty directories. It never adopts a
user directory by dropping a marker into it. Unix directories are created with
mode 0700 and new package files with 0600 (subject to umask); Windows inherits the
parent ACL. The parent must choose a private, genuinely local storage location.

## Review and installation

```rust
pub struct PackageReview {
    pub package: PackageMetadata,
    pub snapshot_sha256: String,
    pub files: Vec<PackageFile>,
}
pub struct PackageFile {
    pub name: String,
    pub sha256: String,
    pub size: u64,
}
pub struct InstallResult {
    pub package: InstalledPackage,
    pub already_installed: bool,
    pub previous: Option<ManagedId>,
}
pub struct InstalledPackage {
    pub id: ManagedId,
    pub manifest: Manifest,
    pub snapshot_sha256: String,
    pub files: Vec<PackageFile>,
    pub enabled: bool,
}
```

Call `inspect_install_source` for the installation review. Display the original
manifest name, API/schema, declared capability, module hash, source directory,
and supplemental file inventory as **untrusted plain text**. After confirmation,
pass **both** `review.package.manifest_sha256` and `review.snapshot_sha256` to
installation/update.

The manifest digest is mandatory and pins the existing permission metadata and
module hash. The additional snapshot digest pins every filename, byte size and
file hash, including license/notice/source sidecars. It is SHA-256 over a
versioned domain prefix plus canonical Serde JSON of the sorted `PackageFile`
array. Callers should use the returned value rather than recreate it.

The loader retains raw manifest bytes alongside module bytes. Installation
rereads and validates a snapshot against the review, then copies **only those
already-read bytes**. It never inspects one module and later reopens the source
for copying. In-memory snapshots survive subsequent source edits unchanged.
The staged copy is validated again before publication.

Same-ID/same-snapshot installation is idempotent and preserves the existing
enabled state without rewriting payloads. If supplemental files differ but the
manifest ID does not, installation fails: a version's contents are immutable.
Any version change, including licensing changes, must change manifest bytes and
receive a new review.

`update_package` validates `previous` and installs the reviewed new ID. It does
**not** overwrite, disable, remove or grant permissions to the old version, and
does not enable the new version. `previous` is informational in the result;
there is no persisted "current version" pointer or inferred publisher identity.
Same-content updates simply return the existing version.

### License and supplemental files

Every regular, portable sibling file is included in the review, snapshot, copy
and receipt. This preserves LICENSE/COPYING/NOTICE/README files and bounded
source archives or WAT files without guessing which names carry legal duties.
Bytes are copied exactly; file modes, timestamps, ownership and alternate
filesystem streams are not preserved. No supplemental file is executable
through the guest API.

Unsupported files are **rejected**, never silently dropped: subdirectories,
symlinks/reparse points, nonregular entries, nonportable filenames, case-folding
name collisions and exceeded limits all prevent installation. A filename must
start with an ASCII alphanumeric, use only ASCII alphanumerics/dot/underscore/
hyphen, be at most 128 bytes, not end with a dot, and not name a Windows device.
The directory path itself may contain native non-Unicode units.

Bounds: 18 total package files (manifest + module + up to 16 supplemental files),
256 KiB per supplemental file and 1 MiB total supplemental data. Existing
manifest/module/runtime limits remain unchanged. This is not a license
compliance checker: authors/distributors must supply the required notices and
corresponding source. Packages that need nested trees or larger source archives
require a future package format rather than dropping those files.

## Storage, state and failure behavior

```text
managed-root\
  .notepad-star-extension-root.json
  .notepad-star-extension-lock
  <manifest-sha256>\
    notepad-star-extension.json
    <manifest.module>
    <supplemental leaf files>
    .notepad-star-package.json
    .notepad-star-enabled           (present only when explicitly enabled)
  .install-<manifest-sha256>\        (incomplete/staged, never runnable)
```

The immutable receipt records the ID, snapshot digest and complete hashed file
inventory. Enabling is represented separately by a small ID-bound marker.
The marker is **not a capability grant**. Corrupt markers/receipts are errors,
not default-enabled or success-shaped fallbacks.

Install writes into a fixed hash-specific staging directory using create-new
files, syncs each file, writes the receipt last, validates everything and renames
to an absent final hash directory. A normal I/O error or process termination can
leave staging content, but it is never considered installed/ready or executable.
Already-existing staging content is not overwritten or recursively cleaned.

```rust
pub enum InstalledEntry {
    Ready { package: InstalledPackage },
    Incomplete { id: ManagedId, staged: bool, error: ExtensionError },
}
```

Listing is deterministic by ID and staging flag, with at most 128 total version/
staging entries. It validates ready entries, including guest structure. Damaged
versions are returned as explicit `Incomplete` entries. Unknown root names,
non-directory hash entries and reparse/symlink directories fail listing outright.
List responses omit repeated root paths to keep wire responses bounded.

Uninstall validates the root marker, exact ID, complete receipt, hashes, guest
structure, enable marker and **entire leaf inventory before deleting anything**.
Unexpected files/directories or reparse/symlink entries cause refusal. It removes
the enable/commit markers first, then only named, rechecked receipt files, then
calls nonrecursive `remove_dir` on that exact hash child. It never removes the
managed root, any source package, another version, or an arbitrary child path.
A concurrent filesystem/I/O failure can leave an incomplete version; no success
response is returned. Corrupt/incomplete versions and staging directories require
explicit manual inspection/cleanup; this API intentionally refuses to guess which
unverified files may be removed.

### Concurrency and execution gating

All managed operations use a nonblocking, kernel-released root lock:
Windows opens the verified lock file with sharing denied; Unix uses `flock`
on the existing no-follow file handle. The Unix FFI call is isolated and holds
the descriptor alive for the guard lifetime. No new dependency was added.
Contention returns `ErrorCode::Busy`; it never waits indefinitely or breaks a
live/stale-looking lock. Closing or killing the process releases the OS lock.

A managed invocation retains the root lock through snapshot validation and guest
execution. Managed enable/disable/uninstall/update cannot race that invocation;
they return Busy and the parent can retry after it finishes. This serializes
all invocations for a root, a deliberate conservative limitation.

The existing `star_extensions::worker()` accepts either its original
`WorkerRequest` **or** this new, disjoint JSON request:

```rust
pub struct ManagedRunRequest {
    pub protocol_version: u32, // existing PROTOCOL_VERSION = 1
    pub managed_root: NativePath,
    pub managed_id: ManagedId,
    pub granted_capabilities: Vec<Capability>,
    pub selected_text: String,
}
```

Use this shape for installed packages, not a prepared legacy path request.
The worker checks the enable marker under the root lock, revalidates the exact
manifest-ID snapshot and still requires exactly `[Capability::SelectedText]`.
Disabled versions return `Disabled`; management does not cache or transfer
execution grants. The response remains the existing `WorkerResponse` containing
an edit proposal or an error.

Legacy path-only requests targeting a managed root child or a package with a
managed receipt are rejected, including staged/incomplete versions. This closes
the accidental path-only bypass of enabled status. It is not DRM: a user who
copies code outside the managed root can still select it as an ordinary local
package and separately approve execution.

## Management worker protocol

```rust
pub struct ManagementRequest {
    pub protocol_version: u32, // MANAGEMENT_PROTOCOL_VERSION = 1
    pub action: ManagementAction,
}
```

`ManagementAction` is internally tagged by `"operation"`:

| Variant / JSON tag | Fields |
| --- | --- |
| `Initialize` / `initialize` | `root: NativePath` |
| `Inspect` / `inspect` | `source: NativePath` |
| `Install` / `install` | `root`, `source`, `expected_manifest_sha256: String`, `expected_snapshot_sha256: String` |
| `Update` / `update` | install fields plus `previous: ManagedId` |
| `List` / `list` | `root` |
| `SetEnabled` / `set_enabled` | `root`, `id: ManagedId`, `enabled: bool` |
| `Uninstall` / `uninstall` | `root`, `id` |

```rust
pub enum ManagementResponse {
    Success { protocol_version: u32, result: ManagementResult },
    Error { protocol_version: u32, error: ExtensionError },
}
```

Response `"status"` is `"success"` or `"error"`. `ManagementResult` is tagged by
`"kind"`: `root {root}`, `review {review}`, `installation {result}`,
`packages {entries}`, `enabled {package}`, or `uninstalled {id}`.
Every request/result struct denies unknown fields. NativePath decoding also
rejects unknown fields, other-platform units, NUL and excessive path length.
Managed IDs serialize as plain lowercase hash strings.

Example listing request:

```json
{"protocol_version":1,"action":{"operation":"list","root":{"platform":"Windows","units":[67,58,92,120]}}}
```

`management_worker()` reads one EOF-delimited JSON request, then writes one JSON
response followed by a newline. The existing 2 MiB request/response caps apply.
Protocol and operation errors are explicit JSON errors; output transport failure
returns `io::Error`. There are no management-side guest execution operations.

The parent should dispatch a management child before Qt/window initialization,
close stdin after sending, drain/cap stdout concurrently, enforce wall-clock
and process/output limits, and require successful process exit plus the expected
typed response. UI must confirm install/update, enabling and uninstall actions.
Execution uses the existing extension worker with `ManagedRunRequest`, per-run
selected-text confirmation, revision/selection checks and undoable application.

## Verification and remaining limitations

```powershell
cargo test -p star-extensions --locked
cargo clippy -p star-extensions --locked --all-targets --no-deps -- -D warnings
cargo build -p star-extensions --locked --examples
```

`management_worker` is also an example executable for protocol integration tests.
Tests cover review/copy races, exact manifest/module/notice bytes, idempotence,
new-version updates, enabled gating through both worker shapes, lock contention,
uninstall/root safety, invalid IDs, unexpected files, source bounds, malformed
receipts, explicit staging failures, JSON strictness and platform link checks.
All original malicious Wasm tests remain in place.

This implementation does not supply an online registry, publisher signatures,
trusted licensing assertions, automatic rollback/recovery, garbage collection,
recursive package trees, migration/adoption of an existing directory, or a
logical extension/current-version registry. File syncing and final rename do
not claim transactional power-loss durability across every filesystem; receipts
and hashes fail closed after partial state.

Use a private root inaccessible to other writers. Locks coordinate cooperating
processes, not hostile local actors replacing/unlinking lock or ancestor paths.
The existing strict directory/no-follow/reparse checks are reused, but this is
not a race-free directory-handle-relative OS filesystem sandbox. Windows UNC and
device namespace paths are rejected; mapped/mounted network filesystems are not
detected as such. Parent must choose genuinely local storage and enforce timeouts
for filesystem access and Wasm compilation, not only guest fuel.

Windows behavior is exercised locally. Unix-only `flock`, permissions and symlink
tests require a Unix test run. This is a bounded package-management slice, not
overall R6/application completion.
