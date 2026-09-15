use super::*;
use std::{
    fs,
    io::{self, Cursor, Read},
    path::{Path, PathBuf},
    sync::atomic::{AtomicUsize, Ordering},
};

static NEXT_DIRECTORY: AtomicUsize = AtomicUsize::new(0);

pub(crate) struct TestDirectory(pub(crate) PathBuf);

impl TestDirectory {
    pub(crate) fn new() -> Self {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).join(".test-artifacts");
        fs::create_dir_all(&root).unwrap();
        let path = root.join(format!(
            "{}-{}",
            std::process::id(),
            NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }

    fn install(&self, wat: &str) -> PackageMetadata {
        let bytes = wat::parse_str(wat).unwrap();
        self.install_bytes(&bytes);
        inspect_package(&self.0).unwrap()
    }

    fn install_bytes(&self, bytes: &[u8]) {
        fs::write(self.0.join("test.wasm"), bytes).unwrap();
        let manifest = Manifest {
            schema_version: SCHEMA_VERSION,
            api_version: API_VERSION,
            name: "Test transform".into(),
            module: "test.wasm".into(),
            sha256: package::digest(bytes),
            capabilities: vec![Capability::SelectedText],
        };
        fs::write(
            self.0.join(MANIFEST_FILE),
            serde_json::to_vec(&manifest).unwrap(),
        )
        .unwrap();
    }

    fn manifest(&self, change: impl FnOnce(&mut serde_json::Value)) {
        let path = self.0.join(MANIFEST_FILE);
        let mut value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
        change(&mut value);
        fs::write(path, serde_json::to_vec(&value).unwrap()).unwrap();
    }
}

impl Drop for TestDirectory {
    fn drop(&mut self) {
        fn remove_owned(path: &Path) {
            for entry in fs::read_dir(path).unwrap() {
                let entry = entry.unwrap();
                let kind = entry.file_type().unwrap();
                if kind.is_dir() && !kind.is_symlink() {
                    remove_owned(&entry.path());
                } else {
                    fs::remove_file(entry.path()).unwrap();
                }
            }
            fs::remove_dir(path).unwrap();
        }
        // Only the unique directory created by this fixture is ever removed.
        remove_owned(&self.0);
    }
}

fn request(metadata: &PackageMetadata, text: &str) -> WorkerRequest {
    WorkerRequest {
        protocol_version: PROTOCOL_VERSION,
        package: metadata.directory.clone(),
        expected_manifest_sha256: metadata.manifest_sha256.clone(),
        granted_capabilities: vec![Capability::SelectedText],
        selected_text: text.into(),
    }
}

fn module(alloc: &str, transform: &str, memory: &str, extra: &str) -> String {
    format!(
        r#"(module
            {extra}
            (memory (export "memory") {memory})
            (func (export "alloc") (param i32) (result i32) {alloc})
            (func (export "transform") (param i32 i32) (result i64) {transform})
        )"#
    )
}

fn identity() -> String {
    module("i32.const 0", "local.get 1 i64.extend_i32_u", "4 4", "")
}

fn run_wat(wat: &str, text: &str) -> Result<EditProposal> {
    let dir = TestDirectory::new();
    let metadata = dir.install(wat);
    execute(request(&metadata, text))
}

fn inspect_wat(wat: &str) -> Result<PackageMetadata> {
    let dir = TestDirectory::new();
    dir.install_bytes(&wat::parse_str(wat).unwrap());
    inspect_package(&dir.0)
}

fn assert_code<T: std::fmt::Debug>(result: Result<T>, expected: ErrorCode) {
    assert_eq!(result.unwrap_err().code, expected);
}

#[test]
fn demo_is_runnable_and_preserves_utf8() {
    let parent = TestDirectory::new();
    let directory = parent.0.join("demo");
    let metadata = create_example(&directory).unwrap();
    assert_eq!(metadata.manifest.name, "ASCII uppercase");
    assert_eq!(
        execute(request(&metadata, "Hello, é世界🙂!\nabc\0")).unwrap(),
        EditProposal {
            replacement: "HELLO, é世界🙂!\nABC\0".into()
        }
    );
    assert_eq!(execute(request(&metadata, "")).unwrap().replacement, "");
    let input = "a".repeat(MAX_INPUT_BYTES);
    assert_eq!(
        execute(request(&metadata, &input)).unwrap().replacement,
        "A".repeat(MAX_INPUT_BYTES)
    );
    let before = fs::read(directory.join("uppercase.wasm")).unwrap();
    assert_code(create_example(&directory), ErrorCode::Io);
    assert_eq!(before, fs::read(directory.join("uppercase.wasm")).unwrap());
}

#[test]
fn import_functions_memories_tables_and_globals_are_forbidden() {
    for import in [
        r#"(import "wasi_snapshot_preview1" "fd_write" (func))"#,
        r#"(import "host" "table" (table 1 funcref))"#,
        r#"(import "host" "global" (global i32))"#,
    ] {
        let wat = module("i32.const 0", "i64.const 0", "1 1", import);
        assert_code(inspect_wat(&wat), ErrorCode::ImportsForbidden);
    }
    let wat = identity().replace(
        r#"(memory (export "memory") 4 4)"#,
        r#"(import "env" "memory" (memory 4 4)) (export "memory" (memory 0))"#,
    );
    assert_code(inspect_wat(&wat), ErrorCode::ImportsForbidden);
}

#[test]
fn invalid_exports_and_start_functions_are_rejected_without_execution() {
    for wat in [
        "(module)".into(),
        identity().replace(r#"(export "memory")"#, ""),
        identity().replace(r#"(export "alloc")"#, ""),
        identity().replace(r#"(export "transform")"#, ""),
        identity().replace("(result i32) i32.const 0", "(result i64) i64.const 0"),
    ] {
        assert_code(inspect_wat(&wat), ErrorCode::InvalidExports);
    }
    let wat = module(
        "i32.const 0",
        "i64.const 0",
        "1 1",
        "(func $start (loop br 0)) (start $start)",
    );
    assert_code(inspect_wat(&wat), ErrorCode::StartForbidden);
}

#[test]
fn invalid_binary_is_not_accepted_as_wat() {
    let dir = TestDirectory::new();
    for bytes in [b"not wasm".as_slice(), identity().as_bytes()] {
        dir.install_bytes(bytes);
        assert_code(inspect_package(&dir.0), ErrorCode::InvalidModule);
    }
}

#[test]
fn allocation_pointer_is_checked_as_unsigned_and_before_writing() {
    for alloc in ["i32.const -1", "i32.const 65536", "i32.const 65535"] {
        assert_code(
            run_wat(&module(alloc, "i64.const 0", "1 1", ""), "ab"),
            ErrorCode::InvalidMemoryRange,
        );
    }
}

#[test]
fn nonzero_packed_output_pointer_round_trips_and_never_changes_package_files() {
    let dir = TestDirectory::new();
    let wat = module(
        "i32.const 16",
        "local.get 0 i64.extend_i32_u i64.const 32 i64.shl local.get 1 i64.extend_i32_u i64.or",
        "1 1",
        "",
    );
    let metadata = dir.install(&wat);
    let before = fs::read(dir.0.join("test.wasm")).unwrap();
    assert_eq!(
        execute(request(&metadata, "aé\0")).unwrap().replacement,
        "aé\0"
    );
    assert_eq!(before, fs::read(dir.0.join("test.wasm")).unwrap());
}

#[test]
fn packed_output_pointer_length_utf8_and_empty_range_are_checked() {
    for (result, expected) in [
        ("i64.const -4294967295", ErrorCode::InvalidMemoryRange),
        ("i64.const 281474976710657", ErrorCode::InvalidMemoryRange),
        ("i64.const -4294967296", ErrorCode::InvalidMemoryRange),
        ("i64.const 262145", ErrorCode::OutputTooLarge),
        ("i64.const 4294967295", ErrorCode::OutputTooLarge),
        ("i64.const 65537", ErrorCode::InvalidMemoryRange),
    ] {
        assert_code(
            run_wat(&module("i32.const 0", result, "1 1", ""), ""),
            expected,
        );
    }
    let wat = module(
        "i32.const 0",
        "i64.const 1",
        "1 1",
        r#"(data (i32.const 0) "\ff")"#,
    );
    assert_code(run_wat(&wat, ""), ErrorCode::InvalidUtf8);
    assert_eq!(
        run_wat(
            &module("i32.const 0", "i64.const 281474976710656", "1 1", ""),
            ""
        )
        .unwrap()
        .replacement,
        ""
    );
}

#[test]
fn fuel_is_shared_by_allocator_and_transform() {
    for (alloc, transform) in [
        ("(loop br 0) unreachable", "i64.const 0"),
        ("i32.const 0", "(loop br 0) unreachable"),
    ] {
        assert_code(
            run_wat(&module(alloc, transform, "1 1", ""), ""),
            ErrorCode::FuelExhausted,
        );
    }
}

#[test]
fn initial_memory_and_even_small_memory_growth_are_rejected() {
    assert_code(
        inspect_wat(&module("i32.const 0", "i64.const 0", "129", "")),
        ErrorCode::MemoryLimit,
    );
    for memory in ["1 1", "1 2", "0 2", "128"] {
        let wat = module(
            "i32.const 1 memory.grow drop i32.const 0",
            "i64.const 0",
            memory,
            "",
        );
        assert_code(inspect_wat(&wat), ErrorCode::MemoryLimit);
    }
    let wat = module(
        "i32.const 0",
        "i32.const 1 memory.grow drop i64.const 0",
        "1 2",
        "",
    );
    assert_code(inspect_wat(&wat), ErrorCode::MemoryLimit);
}

#[test]
fn table_allocation_and_growth_are_limited() {
    assert_code(
        inspect_wat(&module(
            "i32.const 0",
            "i64.const 0",
            "1 1",
            "(table 1025 funcref)",
        )),
        ErrorCode::MemoryLimit,
    );
    let wat = module(
        "i32.const 0",
        "ref.null func i32.const 1 table.grow drop i64.const 0",
        "1 1",
        "(table 1 2 funcref)",
    );
    assert_code(inspect_wat(&wat), ErrorCode::MemoryLimit);
    let wat = module(
        "i32.const 0",
        "i64.const 0",
        "1 1",
        "(table 1 funcref) (table 1 funcref)",
    );
    assert!(inspect_wat(&wat).is_err());
}

#[test]
fn traps_and_recursion_never_become_success() {
    let wat = module("i32.const 0", "unreachable", "1 1", "");
    assert_code(run_wat(&wat, ""), ErrorCode::Trap);
    let wat = module(
        "i32.const 0",
        "call $recurse i64.const 0",
        "1 1",
        "(func $recurse call $recurse nop)",
    );
    assert_code(run_wat(&wat, ""), ErrorCode::Trap);
}

#[test]
fn unknown_fields_capabilities_and_versions_are_rejected() {
    let dir = TestDirectory::new();
    let bytes = wat::parse_str(identity()).unwrap();
    for (field, value, code) in [
        ("extra", serde_json::json!(true), ErrorCode::InvalidManifest),
        (
            "capabilities",
            serde_json::json!(["network"]),
            ErrorCode::InvalidManifest,
        ),
        (
            "capabilities",
            serde_json::json!([]),
            ErrorCode::UnsupportedCapability,
        ),
        (
            "capabilities",
            serde_json::json!(["selected_text", "selected_text"]),
            ErrorCode::UnsupportedCapability,
        ),
        (
            "schema_version",
            serde_json::json!(2),
            ErrorCode::UnsupportedVersion,
        ),
        (
            "api_version",
            serde_json::json!(2),
            ErrorCode::UnsupportedVersion,
        ),
        (
            "name",
            serde_json::json!("bad\nname"),
            ErrorCode::InvalidManifest,
        ),
    ] {
        dir.install_bytes(&bytes);
        dir.manifest(|manifest| manifest[field] = value);
        assert_code(inspect_package(&dir.0), code);
    }
    fs::write(
        dir.0.join(MANIFEST_FILE),
        br#"{"schema_version":1,"schema_version":1}"#,
    )
    .unwrap();
    assert_code(inspect_package(&dir.0), ErrorCode::InvalidManifest);
}

#[test]
fn traversal_absolute_windows_device_and_nested_module_names_are_rejected() {
    let dir = TestDirectory::new();
    dir.install(&identity());
    for name in [
        "../test.wasm",
        "..\\test.wasm",
        "/test.wasm",
        "\\test.wasm",
        "C:\\test.wasm",
        "C:test.wasm",
        "\\\\server\\share\\test.wasm",
        "nested/test.wasm",
        "nested\\test.wasm",
        "test.wasm:stream",
        "CON.wasm",
        "LPT1.wasm",
        ".hidden.wasm",
        "test.wasm ",
        "test.wasm\0",
    ] {
        dir.manifest(|manifest| manifest["module"] = name.into());
        assert_code(inspect_package(&dir.0), ErrorCode::UnsafePath);
    }
}

#[test]
fn module_tampering_and_post_confirmation_manifest_replacement_are_rejected() {
    let dir = TestDirectory::new();
    let metadata = dir.install(&identity());
    let original = fs::read(dir.0.join("test.wasm")).unwrap();
    fs::write(dir.0.join("test.wasm"), b"tampered").unwrap();
    assert_code(execute(request(&metadata, "a")), ErrorCode::HashMismatch);
    fs::write(dir.0.join("test.wasm"), original).unwrap();
    dir.manifest(|manifest| manifest["name"] = "Different author/package".into());
    assert_code(execute(request(&metadata, "a")), ErrorCode::PackageChanged);
}

#[test]
fn package_input_and_protocol_budgets_are_enforced() {
    let dir = TestDirectory::new();
    let metadata = dir.install(&identity());
    assert_code(
        execute(request(&metadata, &"x".repeat(MAX_INPUT_BYTES + 1))),
        ErrorCode::TooLarge,
    );
    let mut unapproved = request(&metadata, "a");
    unapproved.granted_capabilities.clear();
    assert_code(execute(unapproved), ErrorCode::PermissionDenied);
    let mut future = request(&metadata, "a");
    future.protocol_version += 1;
    assert_code(execute(future), ErrorCode::UnsupportedVersion);
    fs::write(dir.0.join("test.wasm"), vec![0; MAX_MODULE_BYTES + 1]).unwrap();
    assert_code(inspect_package(&dir.0), ErrorCode::TooLarge);
    fs::write(
        dir.0.join(MANIFEST_FILE),
        vec![b' '; MAX_MANIFEST_BYTES + 1],
    )
    .unwrap();
    assert_code(inspect_package(&dir.0), ErrorCode::TooLarge);
}

#[test]
fn worker_returns_exactly_one_bounded_response_and_rejects_extra_json() {
    let dir = TestDirectory::new();
    let metadata = dir.install(&identity());
    let text = "\0".repeat(MAX_INPUT_BYTES);
    let bytes = serde_json::to_vec(&request(&metadata, &text)).unwrap();
    let mut output = Vec::new();
    worker_with_io(Cursor::new(&bytes), &mut output).unwrap();
    assert!(output.len() <= MAX_RESPONSE_BYTES);
    assert_eq!(output.iter().filter(|&&b| b == b'\n').count(), 1);
    assert_eq!(
        serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
        WorkerResponse::Success {
            protocol_version: PROTOCOL_VERSION,
            proposal: EditProposal { replacement: text }
        }
    );
    let mut extra = bytes.clone();
    extra.extend_from_slice(b"{}");
    for bad in [b"".as_slice(), b"{", extra.as_slice()] {
        output.clear();
        worker_with_io(bad, &mut output).unwrap();
        assert!(matches!(
            serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
            WorkerResponse::Error { .. }
        ));
    }
}

#[test]
fn worker_stops_reading_after_its_cap_and_reports_read_and_write_errors() {
    struct HugeInput(usize);
    impl Read for HugeInput {
        fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
            assert!(self.0 < MAX_REQUEST_BYTES + 1);
            self.0 += buffer.len();
            buffer.fill(b' ');
            Ok(buffer.len())
        }
    }
    let mut input = HugeInput(0);
    let mut output = Vec::new();
    worker_with_io(&mut input, &mut output).unwrap();
    assert_eq!(input.0, MAX_REQUEST_BYTES + 1);
    assert!(matches!(
        serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
        WorkerResponse::Error {
            error: ExtensionError {
                code: ErrorCode::TooLarge,
                ..
            },
            ..
        }
    ));
    struct BrokenInput;
    impl Read for BrokenInput {
        fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
            Err(io::Error::other("test"))
        }
    }
    output.clear();
    worker_with_io(BrokenInput, &mut output).unwrap();
    assert!(matches!(
        serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
        WorkerResponse::Error {
            error: ExtensionError {
                code: ErrorCode::Io,
                ..
            },
            ..
        }
    ));
    assert!(worker_with_io(b"".as_slice(), &mut [0u8; 0][..]).is_err());
}

#[test]
fn worker_rejects_unknown_request_and_native_path_fields() {
    let dir = TestDirectory::new();
    let metadata = dir.install(&identity());
    for in_path in [false, true] {
        let mut value = serde_json::to_value(request(&metadata, "a")).unwrap();
        if in_path {
            value["package"]["unrecognized"] = true.into();
        } else {
            value["unrecognized"] = true.into();
        }
        let mut output = Vec::new();
        worker_with_io(serde_json::to_vec(&value).unwrap().as_slice(), &mut output).unwrap();
        assert!(matches!(
            serde_json::from_slice::<WorkerResponse>(&output).unwrap(),
            WorkerResponse::Error {
                error: ExtensionError {
                    code: ErrorCode::InvalidRequest,
                    ..
                },
                ..
            }
        ));
    }
}

#[test]
fn native_paths_reject_nul_and_other_platform_units() {
    let dir = TestDirectory::new();
    let metadata = dir.install(&identity());
    for path in [
        NativePath::Windows(vec![0]),
        NativePath::Unix(vec![0]),
        NativePath::Windows(vec![]),
        NativePath::Unix(vec![]),
    ] {
        let mut request = request(&metadata, "");
        request.package = path;
        assert_code(execute(request), ErrorCode::UnsafePath);
    }
    let mut foreign = request(&metadata, "");
    #[cfg(windows)]
    {
        foreign.package = NativePath::Unix(b"/package".to_vec());
    }
    #[cfg(unix)]
    {
        foreign.package = NativePath::Windows(vec![67, 58, 92, 120]);
    }
    assert_code(execute(foreign), ErrorCode::UnsafePath);
}

#[cfg(windows)]
#[test]
fn unc_and_device_package_paths_fail_before_any_network_or_device_access() {
    for path in [
        r"\\server\share\package",
        r"\\.\NUL",
        r"\\?\UNC\server\share",
    ] {
        assert_code(inspect_package(Path::new(path)), ErrorCode::UnsafePath);
    }
}

#[cfg(windows)]
#[test]
fn native_windows_paths_round_trip_unpaired_utf16_without_loss() {
    use std::{ffi::OsString, os::windows::ffi::OsStringExt};
    let path = PathBuf::from(OsString::from_wide(&[67, 58, 92, 0xd800]));
    let encoded = serde_json::to_vec(&NativePath::from_path(&path)).unwrap();
    let decoded: NativePath = serde_json::from_slice(&encoded).unwrap();
    assert_eq!(decoded.to_path().unwrap(), path);
}

#[cfg(unix)]
#[test]
fn native_unix_paths_round_trip_non_utf8_and_packages_work() {
    use std::{ffi::OsString, os::unix::ffi::OsStringExt};
    let parent = TestDirectory::new();
    let directory = parent.0.join(OsString::from_vec(vec![b'x', 0xff]));
    let metadata = create_example(&directory).unwrap();
    let bytes = serde_json::to_vec(&request(&metadata, "hi")).unwrap();
    let decoded: WorkerRequest = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(decoded.package.to_path().unwrap(), directory);
    assert_eq!(execute(decoded).unwrap().replacement, "HI");
}

#[cfg(unix)]
#[test]
fn symlinked_module_manifest_and_package_are_rejected() {
    use std::os::unix::fs::symlink;
    let dir = TestDirectory::new();
    dir.install(&identity());
    let real_module = dir.0.join("real.wasm");
    fs::rename(dir.0.join("test.wasm"), &real_module).unwrap();
    symlink(&real_module, dir.0.join("test.wasm")).unwrap();
    assert_code(inspect_package(&dir.0), ErrorCode::UnsafePath);
    fs::remove_file(dir.0.join("test.wasm")).unwrap();
    fs::rename(real_module, dir.0.join("test.wasm")).unwrap();
    let real_manifest = dir.0.join("real.json");
    fs::rename(dir.0.join(MANIFEST_FILE), &real_manifest).unwrap();
    symlink(&real_manifest, dir.0.join(MANIFEST_FILE)).unwrap();
    assert_code(inspect_package(&dir.0), ErrorCode::UnsafePath);
    let parent = TestDirectory::new();
    symlink(&dir.0, parent.0.join("link")).unwrap();
    assert_code(
        inspect_package(&parent.0.join("link")),
        ErrorCode::UnsafePath,
    );
}

#[cfg(windows)]
#[test]
fn windows_reparse_module_is_rejected_when_symlink_creation_is_available() {
    use std::os::windows::fs::symlink_file;
    let dir = TestDirectory::new();
    dir.install(&identity());
    fs::rename(dir.0.join("test.wasm"), dir.0.join("real.wasm")).unwrap();
    match symlink_file(dir.0.join("real.wasm"), dir.0.join("test.wasm")) {
        Ok(()) => assert_code(inspect_package(&dir.0), ErrorCode::UnsafePath),
        Err(error) if error.raw_os_error() == Some(1314) => {
            eprintln!("Windows symlink test unavailable: no CreateSymbolicLink privilege.");
        }
        Err(error) => panic!("Could not create test symlink: {error}"),
    }
}
