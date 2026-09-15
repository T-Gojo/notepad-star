use crate::{
    read_bounded, runtime, Capability, ErrorCode, ExtensionError, Manifest, NativePath,
    PackageMetadata, Result, API_VERSION, MANIFEST_FILE, MAX_MANIFEST_BYTES, MAX_MODULE_BYTES,
    SCHEMA_VERSION,
};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File, Metadata, OpenOptions},
    io::Write,
    path::{Component, Path, PathBuf},
};

pub(crate) struct LoadedPackage {
    pub metadata: PackageMetadata,
    pub manifest_bytes: Vec<u8>,
    pub module: Vec<u8>,
}

/// Inspect a local directory without executing guest functions.
///
/// Returns lossless native paths and a digest to bind the subsequent permission
/// grant to these exact manifest bytes. Re-inspection is required after changes.
pub fn inspect_package(directory: &Path) -> Result<PackageMetadata> {
    let loaded = load_package(directory, None)?;
    runtime::Guest::new(&loaded.module)?;
    Ok(loaded.metadata)
}

pub(crate) fn load_package(directory: &Path, expected: Option<&str>) -> Result<LoadedPackage> {
    let directory = checked_directory(directory)?;
    let manifest_bytes = read_regular(&directory.join(MANIFEST_FILE), MAX_MANIFEST_BYTES)?;
    let manifest_sha256 = digest(&manifest_bytes);
    if expected.is_some_and(|hash| hash != manifest_sha256) {
        return Err(ExtensionError::new(
            ErrorCode::PackageChanged,
            "The manifest changed after permission confirmation.",
        ));
    }
    let manifest: Manifest = serde_json::from_slice(&manifest_bytes).map_err(|_| {
        ExtensionError::new(
            ErrorCode::InvalidManifest,
            "Invalid manifest fields or unsupported capability.",
        )
    })?;
    validate_manifest(&manifest)?;
    let module = read_regular(&directory.join(&manifest.module), MAX_MODULE_BYTES)?;
    if digest(&module) != manifest.sha256 {
        return Err(ExtensionError::new(
            ErrorCode::HashMismatch,
            "Module SHA-256 does not match the manifest.",
        ));
    }
    // Recheck the directory after reading; the worker still executes only the
    // already-hashed bytes, never reopens the module during execution.
    checked_directory(&directory)?;
    Ok(LoadedPackage {
        metadata: PackageMetadata {
            directory: NativePath::from_path(&directory),
            manifest,
            manifest_sha256,
            module_size: module.len() as u64,
        },
        manifest_bytes,
        module,
    })
}

fn validate_manifest(manifest: &Manifest) -> Result<()> {
    if manifest.schema_version != SCHEMA_VERSION || manifest.api_version != API_VERSION {
        return Err(ExtensionError::new(
            ErrorCode::UnsupportedVersion,
            "Unsupported manifest schema or extension API version.",
        ));
    }
    if manifest.name.trim().is_empty()
        || manifest.name.len() > 128
        || manifest.name.chars().any(char::is_control)
        || !valid_hash(&manifest.sha256)
    {
        return Err(ExtensionError::new(
            ErrorCode::InvalidManifest,
            "Invalid extension name or SHA-256.",
        ));
    }
    if manifest.capabilities != [Capability::SelectedText] {
        return Err(ExtensionError::new(
            ErrorCode::UnsupportedCapability,
            "The only supported capability list is [selected_text].",
        ));
    }
    validate_module_filename(&manifest.module)
}

fn validate_module_filename(name: &str) -> Result<()> {
    validate_portable_filename(name)?;
    if !name.ends_with(".wasm") {
        return Err(ExtensionError::new(
            ErrorCode::UnsafePath,
            "Module must be a portable sibling .wasm filename, not a path.",
        ));
    }
    Ok(())
}

pub(crate) fn validate_portable_filename(name: &str) -> Result<()> {
    let stem = name
        .split('.')
        .next()
        .unwrap_or_default()
        .to_ascii_uppercase();
    let reserved = matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || (stem.len() == 4
            && (stem.starts_with("COM") || stem.starts_with("LPT"))
            && matches!(stem.as_bytes()[3], b'1'..=b'9'));
    if name.len() > 128
        || name.ends_with('.')
        || !name
            .as_bytes()
            .first()
            .is_some_and(u8::is_ascii_alphanumeric)
        || !name
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, b'.' | b'_' | b'-'))
        || reserved
    {
        return Err(ExtensionError::new(
            ErrorCode::UnsafePath,
            "Package files must use portable sibling filenames, not paths or device names.",
        ));
    }
    Ok(())
}

pub(crate) fn valid_hash(hash: &str) -> bool {
    hash.len() == 64
        && hash
            .bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
}

pub(crate) fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

pub(crate) fn checked_directory(directory: &Path) -> Result<PathBuf> {
    let absolute = std::path::absolute(directory)
        .map_err(|_| ExtensionError::new(ErrorCode::UnsafePath, "Invalid package directory."))?;
    #[cfg(windows)]
    {
        use std::path::Prefix;
        if !matches!(
            absolute.components().next(),
            Some(Component::Prefix(prefix))
                if matches!(prefix.kind(), Prefix::Disk(_) | Prefix::VerbatimDisk(_))
        ) {
            return Err(ExtensionError::new(
                ErrorCode::UnsafePath,
                "UNC shares and device namespace package paths are forbidden.",
            ));
        }
    }
    if absolute
        .components()
        .any(|c| matches!(c, Component::ParentDir))
    {
        return Err(ExtensionError::new(
            ErrorCode::UnsafePath,
            "Package directory must not contain parent traversal.",
        ));
    }
    for path in absolute.ancestors() {
        let metadata = fs::symlink_metadata(path).map_err(io_error)?;
        if is_link(&metadata) || !metadata.is_dir() {
            return Err(ExtensionError::new(
                ErrorCode::UnsafePath,
                "Package directories must not contain symlinks or reparse points.",
            ));
        }
    }
    fs::canonicalize(absolute).map_err(io_error)
}

pub(crate) fn is_link(metadata: &Metadata) -> bool {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        metadata.file_attributes() & 0x400 != 0
    }
    #[cfg(not(windows))]
    {
        metadata.file_type().is_symlink()
    }
}

pub(crate) fn read_regular(path: &Path, limit: usize) -> Result<Vec<u8>> {
    check_regular(&fs::symlink_metadata(path).map_err(io_error)?, limit)?;
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        // Open the reparse point itself, never its target.
        options.custom_flags(0x0020_0000);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK);
    }
    let file = options.open(path).map_err(io_error)?;
    check_regular(&file.metadata().map_err(io_error)?, limit)?;
    read_bounded(file, limit)
}

pub(crate) fn check_regular(metadata: &Metadata, limit: usize) -> Result<()> {
    if is_link(metadata) || !metadata.is_file() {
        return Err(ExtensionError::new(
            ErrorCode::UnsafePath,
            "Package files must be regular, non-symlink files.",
        ));
    }
    if metadata.len() > limit as u64 {
        return Err(ExtensionError::new(
            ErrorCode::TooLarge,
            "Package file exceeds the byte limit.",
        ));
    }
    Ok(())
}

pub(crate) fn io_error(_: std::io::Error) -> ExtensionError {
    ExtensionError::new(
        ErrorCode::Io,
        "Could not access the local extension package.",
    )
}

/// Build the included ASCII-uppercase example into a newly created directory.
///
/// The parent must exist; `directory` must not exist. Never overwrites files or
/// deletes directories. On an I/O failure a partial package may remain.
/// Inspection and guest execution are read-only; explicit managed-package
/// operations are provided separately by [`crate::managed`].
pub fn create_example(directory: &Path) -> Result<PackageMetadata> {
    let parent = directory
        .parent()
        .filter(|path| !path.as_os_str().is_empty());
    let parent = checked_directory(parent.unwrap_or_else(|| Path::new(".")))?;
    let name = directory.file_name().ok_or_else(|| {
        ExtensionError::new(
            ErrorCode::UnsafePath,
            "An example directory name is required.",
        )
    })?;
    let destination = parent.join(name);
    let module = wat::parse_str(include_str!("uppercase.wat")).map_err(|_| {
        ExtensionError::new(
            ErrorCode::InvalidModule,
            "The bundled example did not compile.",
        )
    })?;
    runtime::Guest::new(&module)?;
    fs::create_dir(&destination).map_err(io_error)?;
    let manifest = Manifest {
        schema_version: SCHEMA_VERSION,
        api_version: API_VERSION,
        name: "ASCII uppercase".into(),
        module: "uppercase.wasm".into(),
        sha256: digest(&module),
        capabilities: vec![Capability::SelectedText],
    };
    write_new(&destination.join(&manifest.module), &module)?;
    let bytes = serde_json::to_vec_pretty(&manifest).map_err(|_| {
        ExtensionError::new(
            ErrorCode::InvalidManifest,
            "Could not encode example manifest.",
        )
    })?;
    write_new(&destination.join(MANIFEST_FILE), &bytes)?;
    inspect_package(&destination)
}

pub(crate) fn write_new(path: &Path, bytes: &[u8]) -> Result<()> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file: File = options.open(path).map_err(io_error)?;
    file.write_all(bytes).map_err(io_error)?;
    file.sync_all().map_err(io_error)
}
