//! Content-addressed local extension packages. No installation grants permission
//! or runs guest code. See MANAGEMENT.md for storage and worker contracts.

mod protocol;
pub use protocol::*;

use crate::{
    package::{self, LoadedPackage},
    runtime, Capability, EditProposal, ErrorCode, ExtensionError, Manifest, NativePath,
    PackageMetadata, Result, MANIFEST_FILE, MAX_MANIFEST_BYTES, MAX_MODULE_BYTES,
};
use serde::{Deserialize, Deserializer, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::{self, File, OpenOptions},
    io::{self, Read},
    path::{Path, PathBuf},
};

pub const MAX_MANAGED_ENTRIES: usize = 128;
pub const MAX_PACKAGE_FILES: usize = 18;
pub const MAX_EXTRA_FILE_BYTES: usize = 256 * 1024;
pub const MAX_TOTAL_EXTRA_BYTES: usize = 1024 * 1024;
const MAX_RECEIPT_BYTES: usize = 16 * 1024;
const ROOT_MARKER: &str = ".notepad-star-extension-root.json";
const LOCK_FILE: &str = ".notepad-star-extension-lock";
const RECEIPT_FILE: &str = ".notepad-star-package.json";
const ENABLED_FILE: &str = ".notepad-star-enabled";
const STAGING_PREFIX: &str = ".install-";
const ROOT_BYTES: &[u8] = b"{\"schema_version\":1,\"kind\":\"notepad-star-extension-root\"}\n";
const LOCK_BYTES: &[u8] = b"Notepad Star extension root lock v1\n";

/// An exact lowercase manifest SHA-256, never an arbitrary filesystem component.
#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize)]
#[serde(transparent)]
pub struct ManagedId(String);

impl ManagedId {
    pub fn parse(value: &str) -> Result<Self> {
        if !package::valid_hash(value) {
            return Err(error(
                ErrorCode::InvalidManagedId,
                "Managed IDs must be exactly 64 lowercase SHA-256 hexadecimal digits.",
            ));
        }
        Ok(Self(value.into()))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for ManagedId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl<'de> Deserialize<'de> for ManagedId {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> std::result::Result<Self, D::Error> {
        let value = String::deserialize(deserializer)?;
        Self::parse(&value).map_err(serde::de::Error::custom)
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PackageFile {
    pub name: String,
    pub sha256: String,
    pub size: u64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PackageReview {
    pub package: PackageMetadata,
    /// Binds all file names, byte sizes and hashes, including license/notice files.
    pub snapshot_sha256: String,
    pub files: Vec<PackageFile>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InstalledPackage {
    pub id: ManagedId,
    pub manifest: Manifest,
    pub snapshot_sha256: String,
    pub files: Vec<PackageFile>,
    pub enabled: bool,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case", deny_unknown_fields)]
pub enum InstalledEntry {
    Ready {
        package: InstalledPackage,
    },
    Incomplete {
        id: ManagedId,
        /// Staged directories are never executable, even with a complete receipt.
        staged: bool,
        error: ExtensionError,
    },
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InstallResult {
    pub package: InstalledPackage,
    pub already_installed: bool,
    /// Informational only: updates do not disable, replace or remove this version.
    pub previous: Option<ManagedId>,
}

/// Send this JSON shape to the existing `worker()` entry point for managed runs.
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ManagedRunRequest {
    pub protocol_version: u32,
    #[serde(deserialize_with = "crate::deserialize_native_path")]
    pub managed_root: NativePath,
    pub managed_id: ManagedId,
    pub granted_capabilities: Vec<Capability>,
    pub selected_text: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Receipt {
    schema_version: u32,
    id: ManagedId,
    snapshot_sha256: String,
    files: Vec<PackageFile>,
}

struct Snapshot {
    review: PackageReview,
    files: BTreeMap<String, Vec<u8>>,
}

struct RootGuard {
    directory: PathBuf,
    _lock: File,
}

struct Validated {
    package: InstalledPackage,
    loaded: LoadedPackage,
}

fn error(code: ErrorCode, message: &'static str) -> ExtensionError {
    ExtensionError::new(code, message)
}

fn absolute_directory(path: &Path) -> Result<PathBuf> {
    if !path.is_absolute() {
        return Err(error(
            ErrorCode::UnsafePath,
            "Managed package operations require explicit absolute directory paths.",
        ));
    }
    package::checked_directory(path)
}

fn create_private_directory(path: &Path) -> io::Result<()> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::DirBuilderExt;
        fs::DirBuilder::new().mode(0o700).create(path)
    }
    #[cfg(not(unix))]
    {
        fs::create_dir(path)
    }
}

/// Create a new managed root, or verify an already initialized one.
///
/// Never adopts an existing unrelated/empty directory. The parent must exist.
pub fn initialize_managed_root(directory: &Path) -> Result<NativePath> {
    if !directory.is_absolute() {
        return Err(error(
            ErrorCode::UnsafePath,
            "A managed root must be absolute.",
        ));
    }
    let parent = directory.parent().ok_or_else(|| {
        error(
            ErrorCode::UnsafePath,
            "A filesystem root cannot be a managed root.",
        )
    })?;
    let leaf = directory.file_name().ok_or_else(|| {
        error(
            ErrorCode::UnsafePath,
            "A managed root needs a leaf directory name.",
        )
    })?;
    let destination = absolute_directory(parent)?.join(leaf);
    match create_private_directory(&destination) {
        Ok(()) => {
            package::write_new(&destination.join(LOCK_FILE), LOCK_BYTES)?;
            package::write_new(&destination.join(ROOT_MARKER), ROOT_BYTES)?;
        }
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {}
        Err(e) => return Err(package::io_error(e)),
    }
    let root = acquire_root(&destination)?;
    Ok(NativePath::from_path(&root.directory))
}

fn acquire_root(directory: &Path) -> Result<RootGuard> {
    let directory = absolute_directory(directory)?;
    let marker_path = directory.join(ROOT_MARKER);
    if !exists(&marker_path)? {
        return Err(error(
            ErrorCode::NotManagedRoot,
            "This directory is not an initialized managed root.",
        ));
    }
    if package::read_regular(&marker_path, 256)? != ROOT_BYTES {
        return Err(error(
            ErrorCode::NotManagedRoot,
            "Invalid managed-root marker.",
        ));
    }
    let lock_path = directory.join(LOCK_FILE);
    package::check_regular(&fs::symlink_metadata(&lock_path).map_err(lock_error)?, 256)?;
    let mut options = OpenOptions::new();
    options.read(true).write(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.share_mode(0).custom_flags(0x0020_0000);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK);
    }
    let mut lock = options.open(&lock_path).map_err(lock_error)?;
    package::check_regular(&lock.metadata().map_err(package::io_error)?, 256)?;
    #[cfg(unix)]
    {
        use std::os::fd::AsRawFd;
        // SAFETY: the descriptor belongs to the live File retained in RootGuard.
        // flock changes only kernel lock state and does not access Rust memory.
        let result = unsafe { libc::flock(lock.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) };
        if result != 0 {
            return Err(lock_error(io::Error::last_os_error()));
        }
    }
    let mut lock_bytes = Vec::new();
    (&mut lock)
        .take(257)
        .read_to_end(&mut lock_bytes)
        .map_err(package::io_error)?;
    if lock_bytes != LOCK_BYTES {
        return Err(error(
            ErrorCode::NotManagedRoot,
            "Invalid managed-root lock marker.",
        ));
    }
    if package::read_regular(&marker_path, 256)? != ROOT_BYTES {
        return Err(error(
            ErrorCode::NotManagedRoot,
            "Managed-root marker changed.",
        ));
    }
    Ok(RootGuard {
        directory,
        _lock: lock,
    })
}

fn lock_error(error_value: io::Error) -> ExtensionError {
    if error_value.kind() == io::ErrorKind::WouldBlock
        || cfg!(windows) && matches!(error_value.raw_os_error(), Some(32 | 33))
    {
        error(
            ErrorCode::Busy,
            "Another operation or managed invocation holds this root lock.",
        )
    } else {
        package::io_error(error_value)
    }
}

fn exists(path: &Path) -> Result<bool> {
    match fs::symlink_metadata(path) {
        Ok(_) => Ok(true),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(false),
        Err(e) => Err(package::io_error(e)),
    }
}

fn file_names(directory: &Path, managed: bool) -> Result<BTreeSet<String>> {
    let mut names = BTreeSet::new();
    let mut folded = BTreeSet::new();
    for entry in fs::read_dir(directory).map_err(package::io_error)? {
        let entry = entry.map_err(package::io_error)?;
        if names.len() >= MAX_PACKAGE_FILES + if managed { 2 } else { 0 } {
            return Err(error(
                ErrorCode::TooLarge,
                "Too many files in an extension package.",
            ));
        }
        let os_name = entry.file_name();
        let name = os_name.to_str().ok_or_else(|| {
            error(
                ErrorCode::UnsafePath,
                "Package leaf filenames must be portable ASCII.",
            )
        })?;
        if !(managed && matches!(name, RECEIPT_FILE | ENABLED_FILE)) {
            package::validate_portable_filename(name)?;
        }
        if !folded.insert(name.to_ascii_lowercase()) {
            return Err(error(
                ErrorCode::UnsafePath,
                "Package filenames collide under case-insensitive lookup.",
            ));
        }
        let metadata = fs::symlink_metadata(entry.path()).map_err(package::io_error)?;
        if package::is_link(&metadata) || !metadata.is_file() {
            return Err(error(
                ErrorCode::UnsafePath,
                "Package entries must be regular non-symlink leaf files.",
            ));
        }
        names.insert(name.to_owned());
    }
    Ok(names)
}

fn snapshot_hash(files: &[PackageFile]) -> Result<String> {
    let mut bytes = b"notepad-star-package-snapshot-v1\0".to_vec();
    bytes.extend(serde_json::to_vec(files).map_err(|_| {
        error(
            ErrorCode::InvalidManagedState,
            "Could not encode the package snapshot.",
        )
    })?);
    Ok(package::digest(&bytes))
}

fn descriptors(files: &BTreeMap<String, Vec<u8>>) -> Vec<PackageFile> {
    files
        .iter()
        .map(|(name, bytes)| PackageFile {
            name: name.clone(),
            sha256: package::digest(bytes),
            size: bytes.len() as u64,
        })
        .collect()
}

fn source_snapshot(
    source: &Path,
    expected_manifest: Option<&str>,
    expected_snapshot: Option<&str>,
) -> Result<Snapshot> {
    let source = absolute_directory(source)?;
    let loaded = package::load_package(&source, expected_manifest)?;
    runtime::Guest::new(&loaded.module)?;
    let inventory = file_names(&source, false)?;
    let mut files = BTreeMap::new();
    files.insert(MANIFEST_FILE.to_owned(), loaded.manifest_bytes);
    files.insert(loaded.metadata.manifest.module.clone(), loaded.module);
    if !files.keys().all(|name| inventory.contains(name)) {
        return Err(error(
            ErrorCode::UnexpectedEntry,
            "Package filenames do not match their exact manifest names.",
        ));
    }
    let mut extras = 0usize;
    for name in &inventory {
        if files.contains_key(name) {
            continue;
        }
        let bytes = package::read_regular(&source.join(name), MAX_EXTRA_FILE_BYTES)?;
        extras += bytes.len();
        if extras > MAX_TOTAL_EXTRA_BYTES {
            return Err(error(
                ErrorCode::TooLarge,
                "Supplemental package files exceed the total byte limit.",
            ));
        }
        files.insert(name.clone(), bytes);
    }
    if file_names(&source, false)? != inventory {
        return Err(error(
            ErrorCode::PackageChanged,
            "Package file inventory changed while reading.",
        ));
    }
    let entries = descriptors(&files);
    let hash = snapshot_hash(&entries)?;
    if expected_snapshot.is_some_and(|expected| expected != hash) {
        return Err(error(
            ErrorCode::PackageChanged,
            "Package files changed after the installation review.",
        ));
    }
    Ok(Snapshot {
        review: PackageReview {
            package: loaded.metadata,
            snapshot_sha256: hash,
            files: entries,
        },
        files,
    })
}

/// Inspect the complete bounded source snapshot, including license/notice files.
/// Use both returned hashes when installing. No guest function is executed.
pub fn inspect_install_source(source: &Path) -> Result<PackageReview> {
    Ok(source_snapshot(source, None, None)?.review)
}

fn check_review_hashes(manifest: &str, snapshot: &str) -> Result<()> {
    if !package::valid_hash(manifest) || !package::valid_hash(snapshot) {
        return Err(error(
            ErrorCode::InvalidRequest,
            "Exact reviewed manifest and snapshot SHA-256 values are required.",
        ));
    }
    Ok(())
}

/// Install an immutable content-addressed version, initially disabled.
pub fn install_package(
    root: &Path,
    source: &Path,
    expected_manifest_sha256: &str,
    expected_snapshot_sha256: &str,
) -> Result<InstallResult> {
    check_review_hashes(expected_manifest_sha256, expected_snapshot_sha256)?;
    let snapshot = source_snapshot(
        source,
        Some(expected_manifest_sha256),
        Some(expected_snapshot_sha256),
    )?;
    let root = acquire_root(root)?;
    install_snapshot(&root, snapshot, None)
}

/// Install a new version without modifying, disabling or removing `previous`.
pub fn update_package(
    root: &Path,
    previous: &ManagedId,
    source: &Path,
    expected_manifest_sha256: &str,
    expected_snapshot_sha256: &str,
) -> Result<InstallResult> {
    check_review_hashes(expected_manifest_sha256, expected_snapshot_sha256)?;
    let snapshot = source_snapshot(
        source,
        Some(expected_manifest_sha256),
        Some(expected_snapshot_sha256),
    )?;
    let root = acquire_root(root)?;
    validate_installed(&root, previous, false)?;
    install_snapshot(&root, snapshot, Some(previous.clone()))
}

fn install_snapshot(
    root: &RootGuard,
    snapshot: Snapshot,
    previous: Option<ManagedId>,
) -> Result<InstallResult> {
    let id = ManagedId::parse(&snapshot.review.package.manifest_sha256)?;
    let destination = root.directory.join(id.as_str());
    if exists(&destination)? {
        let installed = validate_installed(root, &id, false)?;
        if installed.package.snapshot_sha256 != snapshot.review.snapshot_sha256 {
            return Err(error(
                ErrorCode::PackageChanged,
                "This manifest ID already has different immutable supplemental files.",
            ));
        }
        return Ok(InstallResult {
            package: installed.package,
            already_installed: true,
            previous,
        });
    }
    let entries = root_entries(root)?;
    if entries.len() >= MAX_MANAGED_ENTRIES {
        return Err(error(
            ErrorCode::TooLarge,
            "Managed root entry limit reached.",
        ));
    }
    let staging = root.directory.join(format!("{STAGING_PREFIX}{id}"));
    if exists(&staging)? {
        return Err(error(
            ErrorCode::IncompleteInstall,
            "A staged install already exists; it requires explicit inspection/cleanup.",
        ));
    }
    create_private_directory(&staging).map_err(package::io_error)?;
    // From here on, failure intentionally leaves a non-executable staging entry.
    // Copy only the already reviewed bytes, never reopen the selected source.
    for (name, bytes) in &snapshot.files {
        package::write_new(&staging.join(name), bytes)?;
    }
    let receipt = Receipt {
        schema_version: 1,
        id: id.clone(),
        snapshot_sha256: snapshot.review.snapshot_sha256,
        files: snapshot.review.files,
    };
    let bytes = serde_json::to_vec(&receipt).map_err(|_| {
        error(
            ErrorCode::InvalidManagedState,
            "Could not encode the installation receipt.",
        )
    })?;
    package::write_new(&staging.join(RECEIPT_FILE), &bytes)?;
    validate_installed(root, &id, true)?;
    if exists(&destination)? {
        return Err(error(
            ErrorCode::PackageChanged,
            "Destination appeared while installing; no version was overwritten.",
        ));
    }
    fs::rename(&staging, &destination).map_err(package::io_error)?;
    let installed = validate_installed(root, &id, false)?;
    Ok(InstallResult {
        package: installed.package,
        already_installed: false,
        previous,
    })
}

fn enabled_bytes(id: &ManagedId) -> Vec<u8> {
    format!("notepad-star-enabled-v1:{id}\n").into_bytes()
}

fn validate_installed(root: &RootGuard, id: &ManagedId, staged: bool) -> Result<Validated> {
    let leaf = if staged {
        format!("{STAGING_PREFIX}{id}")
    } else {
        id.to_string()
    };
    let path = root.directory.join(leaf);
    if !exists(&path)? {
        return Err(error(
            ErrorCode::NotInstalled,
            "No such managed package version.",
        ));
    }
    let path = absolute_directory(&path)?;
    if path.parent() != Some(root.directory.as_path()) {
        return Err(error(
            ErrorCode::UnsafePath,
            "Managed package is not a direct child of its root.",
        ));
    }
    let names = file_names(&path, true)?;
    if !names.contains(RECEIPT_FILE) {
        return Err(error(
            ErrorCode::IncompleteInstall,
            "Package has no completed installation receipt.",
        ));
    }
    let receipt: Receipt = serde_json::from_slice(&package::read_regular(
        &path.join(RECEIPT_FILE),
        MAX_RECEIPT_BYTES,
    )?)
    .map_err(|_| {
        error(
            ErrorCode::InvalidManagedState,
            "Invalid installation receipt.",
        )
    })?;
    if receipt.schema_version != 1
        || receipt.id != *id
        || receipt.files.len() > MAX_PACKAGE_FILES
        || receipt.files.len() < 2
        || !package::valid_hash(&receipt.snapshot_sha256)
    {
        return Err(error(
            ErrorCode::InvalidManagedState,
            "Installation receipt identity or schema is invalid.",
        ));
    }
    let mut expected_names = BTreeSet::new();
    let mut folded = BTreeSet::new();
    for file in &receipt.files {
        package::validate_portable_filename(&file.name)?;
        if !package::valid_hash(&file.sha256)
            || !expected_names.insert(file.name.clone())
            || !folded.insert(file.name.to_ascii_lowercase())
        {
            return Err(error(
                ErrorCode::InvalidManagedState,
                "Invalid or duplicate receipt file entry.",
            ));
        }
    }
    if !receipt
        .files
        .windows(2)
        .all(|pair| pair[0].name < pair[1].name)
        || snapshot_hash(&receipt.files)? != receipt.snapshot_sha256
    {
        return Err(error(
            ErrorCode::InvalidManagedState,
            "Receipt snapshot digest or ordering is invalid.",
        ));
    }
    expected_names.insert(RECEIPT_FILE.into());
    let enabled = names.contains(ENABLED_FILE);
    if enabled {
        expected_names.insert(ENABLED_FILE.into());
        if package::read_regular(&path.join(ENABLED_FILE), 256)? != enabled_bytes(id) {
            return Err(error(
                ErrorCode::InvalidManagedState,
                "Invalid package enable marker.",
            ));
        }
    }
    if expected_names != names {
        return Err(error(
            ErrorCode::UnexpectedEntry,
            "Managed package has unexpected or missing files; nothing was removed.",
        ));
    }
    let loaded = package::load_package(&path, Some(id.as_str()))?;
    runtime::Guest::new(&loaded.module)?;
    let mut extras = 0usize;
    for file in &receipt.files {
        let bytes = if file.name == MANIFEST_FILE {
            loaded.manifest_bytes.clone()
        } else if file.name == loaded.metadata.manifest.module {
            loaded.module.clone()
        } else {
            let bytes = package::read_regular(&path.join(&file.name), MAX_EXTRA_FILE_BYTES)?;
            extras += bytes.len();
            if extras > MAX_TOTAL_EXTRA_BYTES {
                return Err(error(
                    ErrorCode::TooLarge,
                    "Managed supplemental files exceed their byte limit.",
                ));
            }
            bytes
        };
        if file.size != bytes.len() as u64 || file.sha256 != package::digest(&bytes) {
            return Err(error(
                ErrorCode::HashMismatch,
                "Managed package file does not match its immutable receipt.",
            ));
        }
    }
    if !receipt.files.iter().any(|file| file.name == MANIFEST_FILE)
        || !receipt
            .files
            .iter()
            .any(|file| file.name == loaded.metadata.manifest.module)
    {
        return Err(error(
            ErrorCode::InvalidManagedState,
            "Receipt omits the manifest or Wasm module.",
        ));
    }
    Ok(Validated {
        package: InstalledPackage {
            id: id.clone(),
            manifest: loaded.metadata.manifest.clone(),
            snapshot_sha256: receipt.snapshot_sha256,
            files: receipt.files,
            enabled,
        },
        loaded,
    })
}

fn root_entries(root: &RootGuard) -> Result<Vec<(ManagedId, bool)>> {
    let mut result = Vec::new();
    for entry in fs::read_dir(&root.directory).map_err(package::io_error)? {
        let entry = entry.map_err(package::io_error)?;
        let os_name = entry.file_name();
        let name = os_name.to_str().ok_or_else(|| {
            error(
                ErrorCode::UnexpectedEntry,
                "Managed root contains an unknown native filename.",
            )
        })?;
        if matches!(name, ROOT_MARKER | LOCK_FILE) {
            continue;
        }
        if result.len() >= MAX_MANAGED_ENTRIES {
            return Err(error(
                ErrorCode::TooLarge,
                "Managed root contains too many entries.",
            ));
        }
        let (hash, staged) = name
            .strip_prefix(STAGING_PREFIX)
            .map_or((name, false), |id| (id, true));
        let id = ManagedId::parse(hash).map_err(|_| {
            error(
                ErrorCode::UnexpectedEntry,
                "Managed root contains an unexpected entry.",
            )
        })?;
        let metadata = fs::symlink_metadata(entry.path()).map_err(package::io_error)?;
        if package::is_link(&metadata) || !metadata.is_dir() {
            return Err(error(
                ErrorCode::UnsafePath,
                "Managed root entries must be non-symlink directories.",
            ));
        }
        result.push((id, staged));
    }
    result.sort();
    Ok(result)
}

/// Enumerate deterministically. Corrupt/incomplete versions are never "ready".
pub fn list_installed(root: &Path) -> Result<Vec<InstalledEntry>> {
    let root = acquire_root(root)?;
    root_entries(&root)?
        .into_iter()
        .map(|(id, staged)| {
            if staged {
                return Ok(InstalledEntry::Incomplete {
                    id,
                    staged: true,
                    error: error(
                        ErrorCode::IncompleteInstall,
                        "Staged installation is not committed or executable.",
                    ),
                });
            }
            Ok(match validate_installed(&root, &id, false) {
                Ok(validated) => InstalledEntry::Ready {
                    package: validated.package,
                },
                Err(error) => InstalledEntry::Incomplete {
                    id,
                    staged: false,
                    error,
                },
            })
        })
        .collect()
}

/// Enabling is not an execution permission grant. Every run still needs a grant.
pub fn set_enabled(root: &Path, id: &ManagedId, enabled: bool) -> Result<InstalledPackage> {
    let root = acquire_root(root)?;
    let validated = validate_installed(&root, id, false)?;
    let marker = root.directory.join(id.as_str()).join(ENABLED_FILE);
    if enabled != validated.package.enabled {
        if enabled {
            package::write_new(&marker, &enabled_bytes(id))?;
        } else {
            fs::remove_file(&marker).map_err(package::io_error)?;
        }
    }
    Ok(validate_installed(&root, id, false)?.package)
}

/// Delete only receipt-validated leaf files, then the now-empty hash directory.
/// Refuses extra files, subdirectories, symlinks/reparse points and corruption.
pub fn uninstall_package(root: &Path, id: &ManagedId) -> Result<()> {
    let root = acquire_root(root)?;
    let validated = validate_installed(&root, id, false)?;
    let path = root.directory.join(id.as_str());
    if validated.package.enabled {
        fs::remove_file(path.join(ENABLED_FILE)).map_err(package::io_error)?;
    }
    // Remove the commit marker before payloads: a crash cannot leave a ready entry.
    fs::remove_file(path.join(RECEIPT_FILE)).map_err(package::io_error)?;
    for file in &validated.package.files {
        let limit = if file.name == MANIFEST_FILE {
            MAX_MANIFEST_BYTES
        } else if file.name == validated.package.manifest.module {
            MAX_MODULE_BYTES
        } else {
            MAX_EXTRA_FILE_BYTES
        };
        let bytes = package::read_regular(&path.join(&file.name), limit)?;
        if package::digest(&bytes) != file.sha256 || bytes.len() as u64 != file.size {
            return Err(error(
                ErrorCode::PackageChanged,
                "Package changed during uninstall; remaining files were retained.",
            ));
        }
        fs::remove_file(path.join(&file.name)).map_err(package::io_error)?;
    }
    fs::remove_dir(&path).map_err(package::io_error)
}

/// Worker-only synchronous execution. Holds the crash-released root lock until
/// the guest finishes, so managed disable/uninstall cannot race a managed run.
pub fn run_managed(request: ManagedRunRequest) -> Result<EditProposal> {
    crate::validate_execution(
        request.protocol_version,
        &request.selected_text,
        &request.granted_capabilities,
    )?;
    let root = acquire_root(&crate::path_from_native(&request.managed_root)?)?;
    let installed = validate_installed(&root, &request.managed_id, false)?;
    if !installed.package.enabled {
        return Err(error(
            ErrorCode::Disabled,
            "This managed extension version is disabled.",
        ));
    }
    runtime::Guest::new(&installed.loaded.module)?.transform(&request.selected_text)
}

pub(crate) fn reject_direct_managed_path(directory: &Path) -> Result<()> {
    let receipt = directory.join(RECEIPT_FILE);
    let in_root = if let Some(parent) = directory.parent() {
        exists(&parent.join(ROOT_MARKER))?
    } else {
        false
    };
    if in_root || exists(&receipt)? {
        return Err(error(
            ErrorCode::PermissionDenied,
            "Managed package paths require a managed root/ID execution request.",
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests;
