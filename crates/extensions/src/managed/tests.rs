use super::*;
use crate::{
    create_example, tests::TestDirectory, WorkerRequest, WorkerResponse, PROTOCOL_VERSION,
};

struct Fixture {
    _owned: TestDirectory,
    root: PathBuf,
    source: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let owned = TestDirectory::new();
        let root = owned.0.join("managed");
        let source = owned.0.join("source");
        create_example(&source).unwrap();
        initialize_managed_root(&root).unwrap();
        Self {
            _owned: owned,
            root,
            source,
        }
    }

    fn review(&self) -> PackageReview {
        inspect_install_source(&self.source).unwrap()
    }

    fn install(&self) -> InstallResult {
        let review = self.review();
        install_package(
            &self.root,
            &self.source,
            &review.package.manifest_sha256,
            &review.snapshot_sha256,
        )
        .unwrap()
    }

    fn request(&self, id: &ManagedId) -> ManagedRunRequest {
        ManagedRunRequest {
            protocol_version: PROTOCOL_VERSION,
            managed_root: NativePath::from_path(&self.root),
            managed_id: id.clone(),
            granted_capabilities: vec![Capability::SelectedText],
            selected_text: "Hello, é🙂".into(),
        }
    }
}

fn assert_code<T: std::fmt::Debug>(result: Result<T>, expected: ErrorCode) {
    assert_eq!(result.unwrap_err().code, expected);
}

#[test]
fn installation_is_disabled_and_cannot_grant_execution_permission() {
    let fixture = Fixture::new();
    assert!(list_installed(&fixture.root).unwrap().is_empty());
    let installed = fixture.install();
    assert!(!installed.package.enabled);
    assert!(!installed.already_installed);
    assert!(installed.previous.is_none());
    assert_eq!(
        installed.package.id.as_str(),
        fixture.review().package.manifest_sha256
    );
    assert_eq!(
        list_installed(&fixture.root).unwrap(),
        vec![InstalledEntry::Ready {
            package: installed.package.clone()
        }]
    );
    assert_code(
        run_managed(fixture.request(&installed.package.id)),
        ErrorCode::Disabled,
    );
    let mut no_grant = fixture.request(&installed.package.id);
    no_grant.granted_capabilities.clear();
    set_enabled(&fixture.root, &installed.package.id, true).unwrap();
    assert_code(run_managed(no_grant), ErrorCode::PermissionDenied);
}

#[test]
fn enable_disable_and_existing_worker_protocol_enforce_managed_state() {
    let fixture = Fixture::new();
    let package = fixture.install().package;
    assert!(
        set_enabled(&fixture.root, &package.id, true)
            .unwrap()
            .enabled
    );
    let request = fixture.request(&package.id);
    assert_eq!(
        run_managed(request.clone()).unwrap().replacement,
        "HELLO, é🙂"
    );
    let mut output = Vec::new();
    crate::worker_with_io(
        serde_json::to_vec(&request).unwrap().as_slice(),
        &mut output,
    )
    .unwrap();
    assert!(matches!(
        serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
        WorkerResponse::Success { proposal, .. } if proposal.replacement == "HELLO, é🙂"
    ));
    assert!(
        !set_enabled(&fixture.root, &package.id, false)
            .unwrap()
            .enabled
    );
    assert_code(run_managed(request), ErrorCode::Disabled);
    let legacy = WorkerRequest {
        protocol_version: PROTOCOL_VERSION,
        package: NativePath::from_path(&fixture.root.join(package.id.as_str())),
        expected_manifest_sha256: package.id.to_string(),
        granted_capabilities: vec![Capability::SelectedText],
        selected_text: "bypass".into(),
    };
    output.clear();
    crate::worker_with_io(serde_json::to_vec(&legacy).unwrap().as_slice(), &mut output).unwrap();
    assert!(matches!(
        serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
        WorkerResponse::Error {
            error: ExtensionError {
                code: ErrorCode::PermissionDenied,
                ..
            },
            ..
        }
    ));
}

#[test]
fn same_snapshot_install_is_idempotent_and_does_not_change_enabled_state() {
    let fixture = Fixture::new();
    let first = fixture.install();
    set_enabled(&fixture.root, &first.package.id, true).unwrap();
    let receipt_path = fixture
        .root
        .join(first.package.id.as_str())
        .join(RECEIPT_FILE);
    let before = fs::read(&receipt_path).unwrap();
    let again = fixture.install();
    assert!(again.already_installed);
    assert!(again.package.enabled);
    assert_eq!(again.package.id, first.package.id);
    assert_eq!(fs::read(receipt_path).unwrap(), before);
    assert_eq!(list_installed(&fixture.root).unwrap().len(), 1);
}

#[test]
fn manifest_module_and_license_changes_after_review_fail_without_commit() {
    let fixture = Fixture::new();
    fs::write(
        fixture.source.join("LICENSE.txt"),
        b"Original license notice\n",
    )
    .unwrap();
    let review = fixture.review();
    let manifest = fs::read(fixture.source.join(MANIFEST_FILE)).unwrap();
    let mut changed = manifest.clone();
    changed.push(b'\n');
    fs::write(fixture.source.join(MANIFEST_FILE), changed).unwrap();
    assert_code(
        install_package(
            &fixture.root,
            &fixture.source,
            &review.package.manifest_sha256,
            &review.snapshot_sha256,
        ),
        ErrorCode::PackageChanged,
    );
    fs::write(fixture.source.join(MANIFEST_FILE), manifest).unwrap();
    fs::write(
        fixture.source.join("LICENSE.txt"),
        b"Changed licensing terms\n",
    )
    .unwrap();
    assert_code(
        install_package(
            &fixture.root,
            &fixture.source,
            &review.package.manifest_sha256,
            &review.snapshot_sha256,
        ),
        ErrorCode::PackageChanged,
    );
    fs::write(fixture.source.join("uppercase.wasm"), b"tampered").unwrap();
    assert_code(
        install_package(
            &fixture.root,
            &fixture.source,
            &review.package.manifest_sha256,
            &review.snapshot_sha256,
        ),
        ErrorCode::HashMismatch,
    );
    assert!(list_installed(&fixture.root).unwrap().is_empty());
}

#[test]
fn copying_uses_the_exact_loaded_snapshot_not_reopened_source_files() {
    let fixture = Fixture::new();
    fs::write(
        fixture.source.join("COPYING"),
        b"License bytes, preserved exactly.\r\n",
    )
    .unwrap();
    let snapshot = source_snapshot(&fixture.source, None, None).unwrap();
    let originals = snapshot.files.clone();
    let id = ManagedId::parse(&snapshot.review.package.manifest_sha256).unwrap();
    fs::write(fixture.source.join(MANIFEST_FILE), b"changed after reading").unwrap();
    fs::write(
        fixture.source.join("uppercase.wasm"),
        b"changed after reading",
    )
    .unwrap();
    fs::write(fixture.source.join("COPYING"), b"changed after reading").unwrap();
    let root = acquire_root(&fixture.root).unwrap();
    let result = install_snapshot(&root, snapshot, None).unwrap();
    assert_eq!(result.package.id, id);
    for (name, bytes) in originals {
        assert_eq!(
            fs::read(fixture.root.join(id.as_str()).join(name)).unwrap(),
            bytes
        );
    }
}

#[test]
fn supplemental_files_are_preserved_and_cannot_mutate_an_existing_version() {
    let fixture = Fixture::new();
    fs::write(fixture.source.join("LICENSE-MIT"), b"Permission notice\n").unwrap();
    fs::write(fixture.source.join("README.txt"), b"Required attribution\n").unwrap();
    fs::write(fixture.source.join("source.wat"), b"(module)\n").unwrap();
    let installed = fixture.install();
    assert_eq!(installed.package.files.len(), 5);
    for name in ["LICENSE-MIT", "README.txt", "source.wat"] {
        assert_eq!(
            fs::read(fixture.source.join(name)).unwrap(),
            fs::read(fixture.root.join(installed.package.id.as_str()).join(name)).unwrap(),
        );
    }
    fs::write(fixture.source.join("LICENSE-MIT"), b"Different license\n").unwrap();
    let reviewed = fixture.review();
    assert_code(
        install_package(
            &fixture.root,
            &fixture.source,
            &reviewed.package.manifest_sha256,
            &reviewed.snapshot_sha256,
        ),
        ErrorCode::PackageChanged,
    );
    assert_eq!(
        fs::read(
            fixture
                .root
                .join(installed.package.id.as_str())
                .join("LICENSE-MIT")
        )
        .unwrap(),
        b"Permission notice\n",
    );
}

#[test]
fn updates_create_a_new_disabled_version_and_preserve_the_old_one() {
    let fixture = Fixture::new();
    let first = fixture.install().package;
    set_enabled(&fixture.root, &first.id, true).unwrap();
    let mut manifest = fs::read(fixture.source.join(MANIFEST_FILE)).unwrap();
    manifest.push(b'\n');
    fs::write(fixture.source.join(MANIFEST_FILE), manifest).unwrap();
    let reviewed = fixture.review();
    let update = update_package(
        &fixture.root,
        &first.id,
        &fixture.source,
        &reviewed.package.manifest_sha256,
        &reviewed.snapshot_sha256,
    )
    .unwrap();
    assert_ne!(update.package.id, first.id);
    assert_eq!(update.previous, Some(first.id.clone()));
    assert!(!update.package.enabled);
    assert_eq!(list_installed(&fixture.root).unwrap().len(), 2);
    assert!(fixture
        .root
        .join(first.id.as_str())
        .join(ENABLED_FILE)
        .is_file());
    assert_eq!(
        run_managed(fixture.request(&first.id)).unwrap().replacement,
        "HELLO, é🙂"
    );
    assert_code(
        run_managed(fixture.request(&update.package.id)),
        ErrorCode::Disabled,
    );
}

#[test]
fn invalid_managed_ids_and_root_deletion_are_impossible_through_the_api() {
    let fixture = Fixture::new();
    for value in [
        "",
        ".",
        "..",
        "../",
        "C:\\",
        "\\\\server\\share",
        &"A".repeat(64),
        &"0".repeat(63),
        &format!("../{}", "a".repeat(64)),
    ] {
        assert_code(ManagedId::parse(value), ErrorCode::InvalidManagedId);
        assert!(serde_json::from_str::<ManagedId>(&serde_json::to_string(value).unwrap()).is_err());
    }
    let id = ManagedId::parse(&"a".repeat(64)).unwrap();
    let hash_named_root = fixture._owned.0.join(id.as_str());
    initialize_managed_root(&hash_named_root).unwrap();
    assert_code(
        uninstall_package(&hash_named_root, &id),
        ErrorCode::NotInstalled,
    );
    assert!(hash_named_root.join(ROOT_MARKER).is_file());
    let filesystem_root = fixture.root.ancestors().last().unwrap();
    assert_code(
        initialize_managed_root(filesystem_root),
        ErrorCode::UnsafePath,
    );
    assert!(fixture.root.join(ROOT_MARKER).is_file());
}

#[test]
fn root_initialization_never_adopts_an_unrelated_existing_directory() {
    let owned = TestDirectory::new();
    fs::write(owned.0.join("personal.txt"), b"keep me").unwrap();
    assert_code(initialize_managed_root(&owned.0), ErrorCode::NotManagedRoot);
    assert_eq!(fs::read(owned.0.join("personal.txt")).unwrap(), b"keep me");
    assert!(!owned.0.join(ROOT_MARKER).exists());
    assert_code(
        initialize_managed_root(Path::new("relative")),
        ErrorCode::UnsafePath,
    );
}

#[test]
fn unexpected_installed_entries_prevent_enable_run_and_uninstall() {
    let fixture = Fixture::new();
    let installed = fixture.install().package;
    let path = fixture.root.join(installed.id.as_str());
    fs::write(path.join("unexpected.txt"), b"do not delete").unwrap();
    assert_code(
        set_enabled(&fixture.root, &installed.id, true),
        ErrorCode::UnexpectedEntry,
    );
    assert_code(
        run_managed(fixture.request(&installed.id)),
        ErrorCode::UnexpectedEntry,
    );
    assert_code(
        uninstall_package(&fixture.root, &installed.id),
        ErrorCode::UnexpectedEntry,
    );
    assert!(path.join(MANIFEST_FILE).is_file());
    assert!(path.join(RECEIPT_FILE).is_file());
    assert_eq!(
        fs::read(path.join("unexpected.txt")).unwrap(),
        b"do not delete"
    );
    assert!(matches!(
        list_installed(&fixture.root).unwrap().as_slice(),
        [InstalledEntry::Incomplete {
            staged: false,
            error: ExtensionError {
                code: ErrorCode::UnexpectedEntry,
                ..
            },
            ..
        }]
    ));
    fs::remove_file(path.join("unexpected.txt")).unwrap();
    fs::create_dir(path.join("unexpected")).unwrap();
    assert_code(
        uninstall_package(&fixture.root, &installed.id),
        ErrorCode::UnsafePath,
    );
    assert!(path.join(RECEIPT_FILE).is_file());
}

#[test]
fn malformed_receipts_and_tampering_are_not_success_shaped_entries() {
    let fixture = Fixture::new();
    let installed = fixture.install().package;
    let path = fixture.root.join(installed.id.as_str());
    fs::write(path.join(RECEIPT_FILE), b"{}").unwrap();
    assert!(matches!(
        list_installed(&fixture.root).unwrap().as_slice(),
        [InstalledEntry::Incomplete { staged: false, .. }]
    ));
    assert_code(
        uninstall_package(&fixture.root, &installed.id),
        ErrorCode::InvalidManagedState,
    );
    assert!(path.join("uppercase.wasm").is_file());
}

#[test]
fn a_failed_copy_remains_explicitly_staged_and_never_executable() {
    let fixture = Fixture::new();
    let mut snapshot = source_snapshot(&fixture.source, None, None).unwrap();
    let id = ManagedId::parse(&snapshot.review.package.manifest_sha256).unwrap();
    // Simulate data corruption between snapshot assembly and staged validation.
    snapshot
        .files
        .insert("uppercase.wasm".into(), b"corrupt staged bytes".to_vec());
    {
        let root = acquire_root(&fixture.root).unwrap();
        assert_code(
            install_snapshot(&root, snapshot, None),
            ErrorCode::HashMismatch,
        );
    }
    assert!(!fixture.root.join(id.as_str()).exists());
    assert!(matches!(
        list_installed(&fixture.root).unwrap().as_slice(),
        [InstalledEntry::Incomplete { staged: true, .. }]
    ));
    assert_code(run_managed(fixture.request(&id)), ErrorCode::NotInstalled);
    assert_code(
        uninstall_package(&fixture.root, &id),
        ErrorCode::NotInstalled,
    );
    let review = fixture.review();
    assert_code(
        install_package(
            &fixture.root,
            &fixture.source,
            &review.package.manifest_sha256,
            &review.snapshot_sha256,
        ),
        ErrorCode::IncompleteInstall,
    );
}

#[test]
fn uninstall_removes_only_the_validated_version_and_keeps_the_root() {
    let fixture = Fixture::new();
    fs::write(
        fixture.source.join("LICENSE"),
        b"keep the notice until uninstall",
    )
    .unwrap();
    let package = fixture.install().package;
    set_enabled(&fixture.root, &package.id, true).unwrap();
    uninstall_package(&fixture.root, &package.id).unwrap();
    assert!(!fixture.root.join(package.id.as_str()).exists());
    assert!(fixture.root.join(ROOT_MARKER).is_file());
    assert!(fixture.root.join(LOCK_FILE).is_file());
    assert!(list_installed(&fixture.root).unwrap().is_empty());
    assert!(fixture.source.join("LICENSE").is_file());
    assert_code(
        uninstall_package(&fixture.root, &package.id),
        ErrorCode::NotInstalled,
    );
}

#[test]
fn exclusive_lock_serializes_mutations_and_is_released_by_handle_drop() {
    let fixture = Fixture::new();
    let package = fixture.install().package;
    let held = acquire_root(&fixture.root).unwrap();
    assert_code(
        set_enabled(&fixture.root, &package.id, true),
        ErrorCode::Busy,
    );
    assert_code(
        uninstall_package(&fixture.root, &package.id),
        ErrorCode::Busy,
    );
    assert_code(list_installed(&fixture.root), ErrorCode::Busy);
    drop(held);
    assert!(
        set_enabled(&fixture.root, &package.id, true)
            .unwrap()
            .enabled
    );
}

#[test]
fn source_sidecars_are_bounded_and_never_silently_omitted() {
    let fixture = Fixture::new();
    fs::create_dir(fixture.source.join("nested-notices")).unwrap();
    assert_code(
        inspect_install_source(&fixture.source),
        ErrorCode::UnsafePath,
    );
    fs::remove_dir(fixture.source.join("nested-notices")).unwrap();
    fs::write(
        fixture.source.join("LICENSE"),
        vec![b'a'; MAX_EXTRA_FILE_BYTES + 1],
    )
    .unwrap();
    assert_code(inspect_install_source(&fixture.source), ErrorCode::TooLarge);
    assert!(list_installed(&fixture.root).unwrap().is_empty());
}

#[test]
fn management_protocol_is_strict_bounded_and_does_not_accept_grants() {
    let fixture = Fixture::new();
    let request = ManagementRequest {
        protocol_version: MANAGEMENT_PROTOCOL_VERSION,
        action: ManagementAction::Inspect {
            source: NativePath::from_path(&fixture.source),
        },
    };
    let mut output = Vec::new();
    management_worker_with_io(
        serde_json::to_vec(&request).unwrap().as_slice(),
        &mut output,
    )
    .unwrap();
    assert!(matches!(
        serde_json::from_slice::<ManagementResponse>(&output).unwrap(),
        ManagementResponse::Success {
            result: ManagementResult::Review { .. },
            ..
        }
    ));
    assert_eq!(output.iter().filter(|&&byte| byte == b'\n').count(), 1);
    for changed in [
        serde_json::json!({"protocol_version":1,"action":{"operation":"inspect","source":NativePath::from_path(&fixture.source),"granted_capabilities":["selected_text"]}}),
        serde_json::json!({"protocol_version":1,"action":{"operation":"uninstall","root":NativePath::from_path(&fixture.root),"id":".."}}),
        serde_json::json!({"protocol_version":1,"extra":true,"action":{"operation":"list","root":NativePath::from_path(&fixture.root)}}),
    ] {
        output.clear();
        management_worker_with_io(
            serde_json::to_vec(&changed).unwrap().as_slice(),
            &mut output,
        )
        .unwrap();
        assert!(matches!(
            serde_json::from_slice::<ManagementResponse>(&output).unwrap(),
            ManagementResponse::Error {
                error: ExtensionError {
                    code: ErrorCode::InvalidRequest,
                    ..
                },
                ..
            }
        ));
    }
    output.clear();
    management_worker_with_io(
        vec![b' '; crate::MAX_REQUEST_BYTES + 1].as_slice(),
        &mut output,
    )
    .unwrap();
    assert!(matches!(
        serde_json::from_slice::<ManagementResponse>(&output).unwrap(),
        ManagementResponse::Error {
            error: ExtensionError {
                code: ErrorCode::TooLarge,
                ..
            },
            ..
        }
    ));
    assert!(fixture.root.join(ROOT_MARKER).is_file());
    assert!(list_installed(&fixture.root).unwrap().is_empty());
}

#[test]
fn management_actions_round_trip_install_list_toggle_and_uninstall() {
    let fixture = Fixture::new();
    let review = fixture.review();
    let root = NativePath::from_path(&fixture.root);
    let invoke = |action| {
        let mut output = Vec::new();
        let request = ManagementRequest {
            protocol_version: MANAGEMENT_PROTOCOL_VERSION,
            action,
        };
        management_worker_with_io(
            serde_json::to_vec(&request).unwrap().as_slice(),
            &mut output,
        )
        .unwrap();
        match serde_json::from_slice::<ManagementResponse>(&output).unwrap() {
            ManagementResponse::Success { result, .. } => result,
            ManagementResponse::Error { error, .. } => panic!("Management failed: {error}"),
        }
    };
    let ManagementResult::Installation { result } = invoke(ManagementAction::Install {
        root: root.clone(),
        source: NativePath::from_path(&fixture.source),
        expected_manifest_sha256: review.package.manifest_sha256,
        expected_snapshot_sha256: review.snapshot_sha256,
    }) else {
        panic!("Expected installation result");
    };
    let id = result.package.id;
    let ManagementResult::Enabled { package } = invoke(ManagementAction::SetEnabled {
        root: root.clone(),
        id: id.clone(),
        enabled: true,
    }) else {
        panic!("Expected enable result");
    };
    assert!(package.enabled);
    let ManagementResult::Packages { entries } =
        invoke(ManagementAction::List { root: root.clone() })
    else {
        panic!("Expected listing");
    };
    assert_eq!(entries.len(), 1);
    let ManagementResult::Uninstalled { id: removed } = invoke(ManagementAction::Uninstall {
        root,
        id: id.clone(),
    }) else {
        panic!("Expected uninstall result");
    };
    assert_eq!(removed, id);
    assert!(list_installed(&fixture.root).unwrap().is_empty());
}

#[test]
fn unrecognized_root_entries_are_refused_without_deletion() {
    let fixture = Fixture::new();
    fs::write(fixture.root.join("personal.txt"), b"keep").unwrap();
    assert_code(list_installed(&fixture.root), ErrorCode::UnexpectedEntry);
    assert_eq!(
        fs::read(fixture.root.join("personal.txt")).unwrap(),
        b"keep"
    );
}

#[cfg(windows)]
#[test]
fn windows_reparse_entries_cannot_be_uninstalled() {
    use std::os::windows::fs::symlink_file;
    let fixture = Fixture::new();
    fs::write(fixture.source.join("LICENSE"), b"original").unwrap();
    let package = fixture.install().package;
    let path = fixture.root.join(package.id.as_str()).join("LICENSE");
    fs::remove_file(&path).unwrap();
    match symlink_file(fixture.source.join("LICENSE"), &path) {
        Ok(()) => {
            assert_code(
                uninstall_package(&fixture.root, &package.id),
                ErrorCode::UnsafePath,
            );
            assert_eq!(
                fs::read(fixture.source.join("LICENSE")).unwrap(),
                b"original"
            );
        }
        Err(e) if e.raw_os_error() == Some(1314) => {
            eprintln!("Windows managed symlink test unavailable: no symlink privilege.");
        }
        Err(e) => panic!("Could not create scoped test symlink: {e}"),
    }
}

#[cfg(unix)]
#[test]
fn unix_symlink_entries_cannot_be_uninstalled() {
    use std::os::unix::fs::symlink;
    let fixture = Fixture::new();
    fs::write(fixture.source.join("LICENSE"), b"original").unwrap();
    let package = fixture.install().package;
    let path = fixture.root.join(package.id.as_str()).join("LICENSE");
    fs::remove_file(&path).unwrap();
    symlink(fixture.source.join("LICENSE"), &path).unwrap();
    assert_code(
        uninstall_package(&fixture.root, &package.id),
        ErrorCode::UnsafePath,
    );
    assert_eq!(
        fs::read(fixture.source.join("LICENSE")).unwrap(),
        b"original"
    );
}
