//! Import-free, bounded WebAssembly selected-text transforms.
//!
//! See the crate README for ABI v1 and the child-process integration contract.
//! Never run extension functions in the editor process. [`inspect_package`]
//! validates and instantiates, but never executes guest code.

pub mod managed;
mod package;
mod runtime;

use serde::{Deserialize, Serialize};
use std::io::{self, Read, Write};

pub use package::{create_example, inspect_package};
pub use star_io::NativePath;

pub const SCHEMA_VERSION: u32 = 1;
pub const API_VERSION: u32 = 1;
pub const PROTOCOL_VERSION: u32 = 1;
pub const MANIFEST_FILE: &str = "notepad-star-extension.json";
pub const MAX_MANIFEST_BYTES: usize = 16 * 1024;
pub const MAX_MODULE_BYTES: usize = 1024 * 1024;
pub const MAX_INPUT_BYTES: usize = 256 * 1024;
pub const MAX_OUTPUT_BYTES: usize = 256 * 1024;
pub const MAX_MEMORY_BYTES: usize = 8 * 1024 * 1024;
pub const MAX_REQUEST_BYTES: usize = 2 * 1024 * 1024;
pub const MAX_RESPONSE_BYTES: usize = 2 * 1024 * 1024;
pub const INSTRUCTION_FUEL: u64 = 10_000_000;

pub type Result<T> = std::result::Result<T, ExtensionError>;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Capability {
    SelectedText,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub schema_version: u32,
    pub api_version: u32,
    pub name: String,
    pub module: String,
    pub sha256: String,
    pub capabilities: Vec<Capability>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PackageMetadata {
    #[serde(deserialize_with = "deserialize_native_path")]
    pub directory: NativePath,
    pub manifest: Manifest,
    /// Hash of the exact manifest bytes; pins the entire permission review.
    pub manifest_sha256: String,
    pub module_size: u64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WorkerRequest {
    pub protocol_version: u32,
    #[serde(deserialize_with = "deserialize_native_path")]
    pub package: NativePath,
    pub expected_manifest_sha256: String,
    pub granted_capabilities: Vec<Capability>,
    pub selected_text: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct EditProposal {
    pub replacement: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case", deny_unknown_fields)]
pub enum WorkerResponse {
    Success {
        protocol_version: u32,
        proposal: EditProposal,
    },
    Error {
        protocol_version: u32,
        error: ExtensionError,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorCode {
    InvalidRequest,
    UnsupportedVersion,
    InvalidManifest,
    UnsupportedCapability,
    PermissionDenied,
    UnsafePath,
    Io,
    TooLarge,
    HashMismatch,
    PackageChanged,
    InvalidModule,
    ImportsForbidden,
    InvalidExports,
    StartForbidden,
    MemoryLimit,
    FuelExhausted,
    Trap,
    InvalidMemoryRange,
    InvalidUtf8,
    OutputTooLarge,
    InvalidManagedId,
    NotManagedRoot,
    NotInstalled,
    Disabled,
    Busy,
    UnexpectedEntry,
    IncompleteInstall,
    InvalidManagedState,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ExtensionError {
    pub code: ErrorCode,
    pub message: String,
}

impl ExtensionError {
    fn new(code: ErrorCode, message: &'static str) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}

impl std::fmt::Display for ExtensionError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for ExtensionError {}

/// Handle exactly one EOF-delimited JSON request on stdin, then return.
///
/// Accepts the original [`WorkerRequest`] or [`managed::ManagedRunRequest`].
/// Managed requests enforce enabled status and retain the root lock for the run.
///
/// Call only from a fresh editor child process before Qt initialization. The
/// parent must close stdin, drain stdout concurrently, bound response bytes,
/// enforce a wall-clock timeout, kill/reap on failure, and revision-check any
/// proposal before applying it as an undoable edit. Protocol errors are written
/// as `WorkerResponse::Error`; only transport failures return `io::Error`.
pub fn worker() -> io::Result<()> {
    worker_with_io(io::stdin().lock(), io::stdout().lock())
}

/// The same single-request protocol over supplied streams; never logs to stdout.
pub fn worker_with_io(input: impl Read, mut output: impl Write) -> io::Result<()> {
    #[derive(Deserialize)]
    #[serde(untagged)]
    enum ExecutionRequest {
        Direct(WorkerRequest),
        Managed(managed::ManagedRunRequest),
    }
    let result = read_bounded(input, MAX_REQUEST_BYTES).and_then(|bytes| {
        let request = serde_json::from_slice::<ExecutionRequest>(&bytes).map_err(|_| {
            ExtensionError::new(ErrorCode::InvalidRequest, "Invalid worker JSON request.")
        })?;
        match request {
            ExecutionRequest::Direct(request) => execute(request),
            ExecutionRequest::Managed(request) => managed::run_managed(request),
        }
    });
    let response = match result {
        Ok(proposal) => WorkerResponse::Success {
            protocol_version: PROTOCOL_VERSION,
            proposal,
        },
        Err(error) => WorkerResponse::Error {
            protocol_version: PROTOCOL_VERSION,
            error,
        },
    };
    let mut bytes = serde_json::to_vec(&response).map_err(io::Error::other)?;
    bytes.push(b'\n');
    if bytes.len() > MAX_RESPONSE_BYTES {
        return Err(io::Error::other(
            "Extension response exceeded protocol limit.",
        ));
    }
    output.write_all(&bytes)?;
    output.flush()
}

fn execute(request: WorkerRequest) -> Result<EditProposal> {
    validate_execution(
        request.protocol_version,
        &request.selected_text,
        &request.granted_capabilities,
    )?;
    if !package::valid_hash(&request.expected_manifest_sha256) {
        return Err(ExtensionError::new(
            ErrorCode::InvalidRequest,
            "An inspected manifest SHA-256 is required.",
        ));
    }
    let directory = path_from_native(&request.package)?;
    let loaded = package::load_package(&directory, Some(&request.expected_manifest_sha256))?;
    managed::reject_direct_managed_path(&loaded.metadata.directory.to_path().map_err(|_| {
        ExtensionError::new(ErrorCode::UnsafePath, "Invalid native package path.")
    })?)?;
    let mut guest = runtime::Guest::new(&loaded.module)?;
    guest.transform(&request.selected_text)
}

fn validate_execution(
    protocol_version: u32,
    selected_text: &str,
    granted: &[Capability],
) -> Result<()> {
    if protocol_version != PROTOCOL_VERSION {
        return Err(ExtensionError::new(
            ErrorCode::UnsupportedVersion,
            "Unsupported worker protocol version.",
        ));
    }
    if selected_text.len() > MAX_INPUT_BYTES {
        return Err(ExtensionError::new(
            ErrorCode::TooLarge,
            "Selected text exceeds the input byte limit.",
        ));
    }
    if granted != [Capability::SelectedText] {
        return Err(ExtensionError::new(
            ErrorCode::PermissionDenied,
            "Explicit selected_text permission is required.",
        ));
    }
    Ok(())
}

fn path_from_native(path: &NativePath) -> Result<std::path::PathBuf> {
    let path_len = match path {
        NativePath::Windows(units) => units.len(),
        NativePath::Unix(units) => units.len(),
    };
    if path_len == 0 || path_len > 32_768 {
        return Err(ExtensionError::new(
            ErrorCode::UnsafePath,
            "Invalid native package path length.",
        ));
    }
    path.to_path()
        .map_err(|_| ExtensionError::new(ErrorCode::UnsafePath, "Invalid native package path."))
}

fn read_bounded(input: impl Read, limit: usize) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    input
        .take(limit as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| ExtensionError::new(ErrorCode::Io, "Could not read bounded input."))?;
    if bytes.len() > limit {
        return Err(ExtensionError::new(
            ErrorCode::TooLarge,
            "Input exceeds the byte limit.",
        ));
    }
    Ok(bytes)
}

fn deserialize_native_path<'de, D>(deserializer: D) -> std::result::Result<NativePath, D::Error>
where
    D: serde::Deserializer<'de>,
{
    #[derive(Deserialize)]
    #[serde(tag = "platform", content = "units", deny_unknown_fields)]
    enum StrictPath {
        Windows(Vec<u16>),
        Unix(Vec<u8>),
    }
    Ok(match StrictPath::deserialize(deserializer)? {
        StrictPath::Windows(units) => NativePath::Windows(units),
        StrictPath::Unix(units) => NativePath::Unix(units),
    })
}

#[cfg(test)]
mod tests;
