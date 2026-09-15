use super::*;
use crate::{read_bounded, MAX_REQUEST_BYTES, MAX_RESPONSE_BYTES};
use std::io::{self, Read, Write};

pub const MANAGEMENT_PROTOCOL_VERSION: u32 = 1;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ManagementRequest {
    pub protocol_version: u32,
    pub action: ManagementAction,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "operation", rename_all = "snake_case", deny_unknown_fields)]
pub enum ManagementAction {
    Initialize {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        root: NativePath,
    },
    Inspect {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        source: NativePath,
    },
    Install {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        root: NativePath,
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        source: NativePath,
        expected_manifest_sha256: String,
        expected_snapshot_sha256: String,
    },
    Update {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        root: NativePath,
        previous: ManagedId,
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        source: NativePath,
        expected_manifest_sha256: String,
        expected_snapshot_sha256: String,
    },
    List {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        root: NativePath,
    },
    SetEnabled {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        root: NativePath,
        id: ManagedId,
        enabled: bool,
    },
    Uninstall {
        #[serde(deserialize_with = "crate::deserialize_native_path")]
        root: NativePath,
        id: ManagedId,
    },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ManagementResult {
    Root { root: NativePath },
    Review { review: PackageReview },
    Installation { result: InstallResult },
    Packages { entries: Vec<InstalledEntry> },
    Enabled { package: InstalledPackage },
    Uninstalled { id: ManagedId },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case", deny_unknown_fields)]
pub enum ManagementResponse {
    Success {
        protocol_version: u32,
        result: ManagementResult,
    },
    Error {
        protocol_version: u32,
        error: ExtensionError,
    },
}

/// Dispatch a management request in the parent's bounded worker process.
/// No action here grants execution permission or calls a guest function.
pub fn handle_management(request: ManagementRequest) -> Result<ManagementResult> {
    if request.protocol_version != MANAGEMENT_PROTOCOL_VERSION {
        return Err(error(
            ErrorCode::UnsupportedVersion,
            "Unsupported extension management protocol version.",
        ));
    }
    match request.action {
        ManagementAction::Initialize { root } => Ok(ManagementResult::Root {
            root: initialize_managed_root(&crate::path_from_native(&root)?)?,
        }),
        ManagementAction::Inspect { source } => Ok(ManagementResult::Review {
            review: inspect_install_source(&crate::path_from_native(&source)?)?,
        }),
        ManagementAction::Install {
            root,
            source,
            expected_manifest_sha256,
            expected_snapshot_sha256,
        } => Ok(ManagementResult::Installation {
            result: install_package(
                &crate::path_from_native(&root)?,
                &crate::path_from_native(&source)?,
                &expected_manifest_sha256,
                &expected_snapshot_sha256,
            )?,
        }),
        ManagementAction::Update {
            root,
            previous,
            source,
            expected_manifest_sha256,
            expected_snapshot_sha256,
        } => Ok(ManagementResult::Installation {
            result: update_package(
                &crate::path_from_native(&root)?,
                &previous,
                &crate::path_from_native(&source)?,
                &expected_manifest_sha256,
                &expected_snapshot_sha256,
            )?,
        }),
        ManagementAction::List { root } => Ok(ManagementResult::Packages {
            entries: list_installed(&crate::path_from_native(&root)?)?,
        }),
        ManagementAction::SetEnabled { root, id, enabled } => Ok(ManagementResult::Enabled {
            package: set_enabled(&crate::path_from_native(&root)?, &id, enabled)?,
        }),
        ManagementAction::Uninstall { root, id } => {
            uninstall_package(&crate::path_from_native(&root)?, &id)?;
            Ok(ManagementResult::Uninstalled { id })
        }
    }
}

/// Read one bounded EOF-delimited request and write one bounded JSON response.
pub fn management_worker() -> io::Result<()> {
    management_worker_with_io(io::stdin().lock(), io::stdout().lock())
}

pub fn management_worker_with_io(input: impl Read, mut output: impl Write) -> io::Result<()> {
    let result = read_bounded(input, MAX_REQUEST_BYTES).and_then(|bytes| {
        let request: ManagementRequest = serde_json::from_slice(&bytes).map_err(|_| {
            error(
                ErrorCode::InvalidRequest,
                "Invalid extension management JSON request.",
            )
        })?;
        handle_management(request)
    });
    let response = match result {
        Ok(result) => ManagementResponse::Success {
            protocol_version: MANAGEMENT_PROTOCOL_VERSION,
            result,
        },
        Err(error) => ManagementResponse::Error {
            protocol_version: MANAGEMENT_PROTOCOL_VERSION,
            error,
        },
    };
    let mut bytes = serde_json::to_vec(&response).map_err(io::Error::other)?;
    bytes.push(b'\n');
    if bytes.len() > MAX_RESPONSE_BYTES {
        return Err(io::Error::other(
            "Extension management response exceeds the byte limit.",
        ));
    }
    output.write_all(&bytes)?;
    output.flush()
}
