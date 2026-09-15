use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    env, fs,
    io::Read,
    path::{Path, PathBuf},
    process::{Command, ExitCode},
};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const BASELINE: &str = "572650c1894501ace6050ae5c90a9c70cb4691dc";
const QT_VERSION: &str = "6.8.3";

fn workspace() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .to_owned()
}

fn checked(command: &mut Command) -> Result<()> {
    let status = command.status()?;
    if !status.success() {
        return Err(format!("{command:?} failed with {status}").into());
    }
    Ok(())
}

fn command_output(command: &mut Command) -> Result<String> {
    let result = command.output()?;
    if !result.status.success() {
        return Err(format!(
            "{command:?} failed: {}",
            String::from_utf8_lossy(&result.stderr)
        )
        .into());
    }
    Ok(String::from_utf8(result.stdout)?.trim().into())
}

fn executable(name: &str) -> String {
    if cfg!(windows) {
        format!("{name}.exe")
    } else {
        name.into()
    }
}

struct Environment {
    qt: PathBuf,
    cmake: PathBuf,
}

fn discover() -> Result<Environment> {
    let qt = env::var_os("QT_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            workspace()
                .join("build/qt")
                .join(QT_VERSION)
                .join(if cfg!(windows) {
                    "msvc2022_64"
                } else {
                    "macos"
                })
        });
    let qmake = qt.join("bin").join(executable("qmake"));
    if !qmake.is_file() {
        return Err(format!(
            "Qt {QT_VERSION} was not found at {}. Install Qt Widgets and qt5compat, then set QT_ROOT to the matching SDK directory. See README.md.",
            qt.display()
        ).into());
    }
    let version = command_output(Command::new(&qmake).args(["-query", "QT_VERSION"]))?;
    if version != QT_VERSION {
        return Err(format!("Expected Qt {QT_VERSION}; found {version}.").into());
    }
    if !qt
        .join("lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake")
        .is_file()
    {
        return Err("Qt Core5Compat is missing; install the qt5compat module.".into());
    }
    let cmake = if let Some(path) = env::var_os("CMAKE") {
        PathBuf::from(path)
    } else if Command::new("cmake").arg("--version").output().is_ok() {
        PathBuf::from("cmake")
    } else if cfg!(windows) {
        let program_files =
            env::var_os("ProgramFiles(x86)").ok_or("ProgramFiles(x86) is not set.")?;
        let vswhere =
            PathBuf::from(program_files).join("Microsoft Visual Studio/Installer/vswhere.exe");
        let found = command_output(Command::new(vswhere).args([
            "-latest",
            "-products",
            "*",
            "-find",
            r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        ]))?;
        if found.is_empty() {
            return Err("CMake was not found in Visual Studio.".into());
        }
        PathBuf::from(found.lines().next().unwrap())
    } else {
        return Err("CMake is missing. Install CMake and the Xcode command line tools.".into());
    };
    command_output(Command::new(&cmake).arg("--version"))?;
    let target = command_output(Command::new("rustc").arg("-vV"))?;
    let expected = if cfg!(windows) {
        "x86_64-pc-windows-msvc"
    } else {
        "aarch64-apple-darwin"
    };
    if !target
        .lines()
        .any(|line| line == format!("host: {expected}"))
    {
        return Err(format!("This foundation requires the native {expected} toolchain.").into());
    }
    Ok(Environment { qt, cmake })
}

impl Environment {
    fn configure(&self, command: &mut Command) -> Result<()> {
        command.env("QT_ROOT", &self.qt).env("CMAKE", &self.cmake);
        if cfg!(target_os = "macos") {
            command.env("MACOSX_DEPLOYMENT_TARGET", "13.0");
        }
        let mut paths = vec![self.qt.join("bin")];
        if let Some(parent) = self.cmake.parent() {
            if !parent.as_os_str().is_empty() {
                paths.push(parent.into());
            }
        }
        paths.extend(env::split_paths(&env::var_os("PATH").unwrap_or_default()));
        command.env("PATH", env::join_paths(paths)?);
        Ok(())
    }
}

fn cargo(environment: &Environment, arguments: &[&str]) -> Result<()> {
    let mut command = Command::new(env::var_os("CARGO").unwrap_or_else(|| "cargo".into()));
    command.current_dir(workspace()).args(arguments);
    environment.configure(&mut command)?;
    checked(&mut command)
}

fn build(environment: &Environment) -> Result<()> {
    cargo(environment, &["build", "--locked", "-p", "notepad-star"])
}
fn build_release(environment: &Environment) -> Result<()> {
    cargo(
        environment,
        &["build", "--locked", "--release", "-p", "notepad-star"],
    )
}
fn profile_binary(release: bool) -> PathBuf {
    workspace()
        .join(if release {
            "target/release"
        } else {
            "target/debug"
        })
        .join(executable("notepad-star"))
}

fn binary() -> PathBuf {
    workspace()
        .join("target/debug")
        .join(executable("notepad-star"))
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
struct Reference {
    file: String,
    line: usize,
}

#[derive(Debug, PartialEq, Serialize, Deserialize)]
struct Feature {
    id: String,
    kind: String,
    label: String,
    milestone: String,
    windows: String,
    macos: String,
    references: Vec<Reference>,
    evidence: Vec<String>,
}

#[derive(Debug, PartialEq, Serialize, Deserialize)]
struct Inventory {
    schema: u32,
    baseline_commit: String,
    baseline_version: String,
    scope: String,
    features: Vec<Feature>,
}

fn milestone(id: &str) -> &'static str {
    if id.starts_with("IDM_LANG") {
        "R4"
    } else if id.starts_with("IDM_SEARCH") {
        "R2-R3"
    } else if id.starts_with("IDM_FILE") {
        "R1-R3"
    } else if id.starts_with("IDM_VIEW") || id.starts_with("IDM_EDIT") {
        "R1-R5"
    } else if id.starts_with("IDM_FORMAT") {
        "R1-R3"
    } else if id.contains("PLUGIN") {
        "R6"
    } else {
        "R5-R7"
    }
}

fn inventory() -> Result<Inventory> {
    let root = workspace().parent().unwrap().to_owned();
    checked(Command::new("git").current_dir(&root).args([
        "--no-pager", "diff", "--quiet", BASELINE, "--",
        "PowerEditor/src/menuCmdID.h",
        "PowerEditor/src/Notepad_plus.rc",
        "PowerEditor/src/WinControls/Preference/preference.rc",
        "PowerEditor/src/resource.h",
    ])).map_err(|error| format!("The pinned reference files changed; review the baseline before regenerating the inventory: {error}"))?;
    let mut entries: BTreeMap<String, Feature> = BTreeMap::new();
    let header = "PowerEditor/src/menuCmdID.h";
    let command_source = fs::read_to_string(root.join(header))?;
    for (index, line) in command_source.lines().enumerate() {
        let mut words = line.split_whitespace();
        if words.next() != Some("#define") {
            continue;
        }
        let Some(id) = words.next() else { continue };
        if !id.starts_with("IDM_") {
            continue;
        }
        entries.insert(
            id.into(),
            Feature {
                id: id.into(),
                kind: "command-definition".into(),
                label: id.into(),
                milestone: milestone(id).into(),
                windows: "planned".into(),
                macos: "planned".into(),
                references: vec![Reference {
                    file: header.into(),
                    line: index + 1,
                }],
                evidence: Vec::new(),
            },
        );
    }
    for source in [
        "PowerEditor/src/Notepad_plus.rc",
        "PowerEditor/src/WinControls/Preference/preference.rc",
    ] {
        let contents = fs::read_to_string(root.join(source))?;
        for (index, line) in contents.lines().enumerate() {
            let trimmed = line.trim();
            if trimmed.starts_with("//") {
                continue;
            }
            let prefix = if source.ends_with("preference.rc") {
                "IDC_"
            } else {
                "IDM_"
            };
            let Some(offset) = line.find(prefix) else {
                continue;
            };
            let id: String = line[offset..]
                .chars()
                .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
                .collect();
            let label = line.split('"').nth(1).unwrap_or(&id).to_owned();
            let feature = entries.entry(id.clone()).or_insert_with(|| Feature {
                id: id.clone(),
                kind: "preference-control".into(),
                label: label.clone(),
                milestone: "R5".into(),
                windows: "planned".into(),
                macos: "planned".into(),
                references: Vec::new(),
                evidence: Vec::new(),
            });
            if feature.label == feature.id {
                feature.label = label;
            }
            feature.references.push(Reference {
                file: source.into(),
                line: index + 1,
            });
        }
    }
    for command in star_core::commands()? {
        if let Some(reference) = command.upstream {
            let entry = entries
                .get_mut(&reference)
                .ok_or_else(|| format!("Unknown upstream command: {reference}"))?;
            entry.windows = "prototype-unqualified".into();
            entry.macos = "prototype-unvalidated".into();
            entry.evidence.push(
                "native/qt-shell/shell.cpp; qualify against reference before marking parity".into(),
            );
        }
    }
    for (id, label, phase) in [
        (
            "behavior.sessions-recovery",
            "Session restore, unsaved buffers, backups and crash recovery",
            "R2",
        ),
        (
            "behavior.cli",
            "Command-line flags, launch forwarding and OS open-document events",
            "R5",
        ),
        (
            "behavior.udl",
            "User-defined language parsing, execution, editing, import and export",
            "R4",
        ),
        (
            "behavior.function-list",
            "Language-specific function parsing and upstream fixtures",
            "R4",
        ),
        (
            "behavior.macros-import",
            "Recorded macros and configuration compatibility",
            "R5",
        ),
        (
            "behavior.extensions",
            "New cross-platform extension API (not native DLL compatibility)",
            "R6",
        ),
        (
            "behavior.bundled-plugins",
            "NppExport, Converter and mimeTools user workflows",
            "R6",
        ),
        (
            "behavior.accessibility",
            "Narrator/VoiceOver, keyboard navigation, IME and RTL",
            "R0-R5",
        ),
        (
            "behavior.large-files",
            "Large-file, long-line, memory and cancellation qualification",
            "R7",
        ),
        (
            "behavior.updates-packaging",
            "Independent trusted updates, signing and native distribution",
            "R7",
        ),
    ] {
        entries.insert(
            id.into(),
            Feature {
                id: id.into(),
                kind: "behavior-family".into(),
                label: label.into(),
                milestone: phase.into(),
                windows: "planned".into(),
                macos: "planned".into(),
                references: Vec::new(),
                evidence: Vec::new(),
            },
        );
    }
    Ok(Inventory {
        schema: 1, baseline_commit: BASELINE.into(), baseline_version: "8.9.8".into(),
        scope: "Seed inventory: command definitions (includes aliases/ranges), resource occurrences, preference controls and non-menu families. Manual semantic reconciliation is still required; counts are not feature parity.".into(),
        features: entries.into_values().collect(),
    })
}

fn parity(write: bool) -> Result<()> {
    let inventory = inventory()?;
    let path = workspace().join("compat/features.json");
    if write {
        fs::create_dir_all(path.parent().unwrap())?;
        fs::write(&path, serde_json::to_string_pretty(&inventory)? + "\n")?;
    } else {
        let committed: Inventory = serde_json::from_slice(&fs::read(&path)?)?;
        if committed != inventory {
            return Err("Inventory drift: review and run `xtask inventory`.".into());
        }
    }
    let ids: BTreeSet<_> = inventory.features.iter().map(|entry| &entry.id).collect();
    if ids.len() != inventory.features.len() {
        return Err("Duplicate inventory IDs.".into());
    }
    println!(
        "{} inventory entries; semantic reconciliation and full parity are NOT complete.",
        ids.len()
    );
    Ok(())
}

fn copy_tree(source: &Path, destination: &Path) -> Result<()> {
    fs::create_dir_all(destination)?;
    for entry in fs::read_dir(source)? {
        let entry = entry?;
        let target = destination.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &target)?;
        } else {
            fs::copy(entry.path(), target)?;
        }
    }
    Ok(())
}

fn copy_license_files(source: &Path, destination: &Path) -> Result<usize> {
    let mut count = 0;
    for entry in fs::read_dir(source)? {
        let entry = entry?;
        let name = entry.file_name().to_string_lossy().to_ascii_lowercase();
        if name.starts_with("license")
            || name.starts_with("copying")
            || name.starts_with("copyright")
            || name.starts_with("notice")
        {
            fs::create_dir_all(destination)?;
            if entry.file_type()?.is_dir() {
                copy_tree(&entry.path(), &destination.join(entry.file_name()))?;
            } else {
                fs::copy(entry.path(), destination.join(entry.file_name()))?;
            }
            count += 1;
        }
    }
    Ok(count)
}

fn licenses(environment: &Environment, output_directory: &Path) -> Result<()> {
    let pugixml = output_directory.join("licenses/pugixml");
    fs::create_dir_all(&pugixml)?;
    fs::copy(
        workspace().join("packaging/licenses/pugixml-MIT.txt"),
        pugixml.join("LICENSE.txt"),
    )?;
    let sources = env::var_os("QT_SOURCE_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|| environment.qt.parent().unwrap().join("Src"));
    for module in ["qtbase", "qt5compat", "qtsvg"] {
        let source = sources.join(module);
        if !source.join("LICENSES").is_dir() {
            return Err(format!(
                "Missing Qt source notices at {}. See README.md packaging prerequisites.",
                source.display()
            )
            .into());
        }
        let destination = output_directory.join("licenses/Qt").join(module);
        copy_tree(&source.join("LICENSES"), &destination)?;
        if source.join("REUSE.toml").is_file() {
            fs::copy(source.join("REUSE.toml"), destination.join("REUSE.toml"))?;
        }
        let file = format!("{module}-{QT_VERSION}.spdx.json");
        let source_metadata = environment.qt.join("sbom").join(&file);
        fs::copy(&source_metadata, destination.join(file)).map_err(|error| {
            format!(
                "Cannot copy Qt license metadata {}: {error}",
                source_metadata.display()
            )
        })?;
        // Some Qt modules publish only a binary SPDX report.
        let file = format!("{module}-{QT_VERSION}.source.spdx");
        let source_metadata = environment.qt.join("sbom").join(&file);
        if source_metadata.is_file() {
            fs::copy(source_metadata, destination.join(file))?;
        }
    }
    let metadata = command_output(
        Command::new(env::var_os("CARGO").unwrap_or_else(|| "cargo".into()))
            .current_dir(workspace())
            .args([
                "metadata",
                "--locked",
                "--format-version",
                "1",
                "--filter-platform",
                if cfg!(windows) {
                    "x86_64-pc-windows-msvc"
                } else {
                    "aarch64-apple-darwin"
                },
            ]),
    )?;
    let metadata: serde_json::Value = serde_json::from_str(&metadata)?;
    let packages = metadata["packages"]
        .as_array()
        .ok_or("Cargo metadata packages missing")?;
    let root = packages
        .iter()
        .find(|package| package["name"] == "notepad-star")
        .and_then(|package| package["id"].as_str())
        .ok_or("Desktop package missing")?;
    let nodes = metadata["resolve"]["nodes"]
        .as_array()
        .ok_or("Cargo dependency graph missing")?;
    let mut reachable = BTreeSet::new();
    let mut stack = vec![root];
    while let Some(id) = stack.pop() {
        if !reachable.insert(id) {
            continue;
        }
        let node = nodes
            .iter()
            .find(|node| node["id"] == id)
            .ok_or("Dependency graph node missing")?;
        for dependency in node["deps"].as_array().ok_or("Dependency list missing")? {
            if dependency["dep_kinds"]
                .as_array()
                .ok_or("Dependency kinds missing")?
                .iter()
                .any(|kind| kind["kind"] != "dev")
            {
                stack.push(
                    dependency["pkg"]
                        .as_str()
                        .ok_or("Dependency package ID missing")?,
                );
            }
        }
    }
    for package in packages {
        if package["source"].is_null()
            || !reachable.contains(package["id"].as_str().ok_or("Package ID missing")?)
        {
            continue;
        }
        let manifest = Path::new(
            package["manifest_path"]
                .as_str()
                .ok_or("Missing package manifest")?,
        );
        let name = format!(
            "{}-{}",
            package["name"].as_str().ok_or("Missing package name")?,
            package["version"]
                .as_str()
                .ok_or("Missing package version")?
        );
        let destination = output_directory.join("licenses/Rust").join(name);
        if copy_license_files(manifest.parent().unwrap(), &destination)? == 0 {
            let vcs_path = manifest.parent().unwrap().join(".cargo_vcs_info.json");
            let vcs: serde_json::Value = serde_json::from_slice(&fs::read(&vcs_path)?)?;
            let group = if vcs["git"]["sha1"] == "e66235859a6ec0502bf6f9dcc358953eda4cafcc"
                && package["license"] == "Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT"
            {
                "wasm-tools"
            } else if vcs["git"]["sha1"] == "50ac76771d631f642e8c4c61bf44c99546024461"
                && package["license"] == "MIT/Apache-2.0"
            {
                "wasmi"
            } else {
                return Err(format!(
                    "No verified license notice found for {}.",
                    manifest.display()
                )
                .into());
            };
            fs::create_dir_all(&destination)?;
            fs::copy(
                workspace().join(format!("packaging/licenses/{group}-MIT.txt")),
                destination.join("LICENSE-MIT"),
            )?;
            fs::copy(
                workspace().join(format!("packaging/licenses/{group}-source.txt")),
                destination.join("SOURCE.txt"),
            )?;
        }
    }
    Ok(())
}

fn valid_label(label: &str) -> bool {
    !label.is_empty()
        && label.len() <= 64
        && label
            .as_bytes()
            .first()
            .is_some_and(u8::is_ascii_alphanumeric)
        && label
            .as_bytes()
            .last()
            .is_some_and(u8::is_ascii_alphanumeric)
        && label
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '-' | '.'))
}
fn release_revision() -> Result<u16> {
    let prerelease = env!("CARGO_PKG_VERSION_PRE");
    Ok(if prerelease.is_empty() {
        0
    } else {
        prerelease.rsplit('.').next().unwrap().parse()?
    })
}
fn hash_file(path: &Path) -> Result<String> {
    let mut input = fs::File::open(path)?;
    let mut hash = Sha256::new();
    let mut buffer = [0u8; 65536];
    loop {
        let count = input.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        hash.update(&buffer[..count]);
    }
    Ok(format!("{:x}", hash.finalize()))
}
#[derive(Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct PayloadFile {
    path: String,
    sha256: String,
    bytes: u64,
    link: Option<String>,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct PackageManifest {
    schema: u32,
    application: String,
    version: String,
    platform: String,
    profile: String,
    channel: String,
    publisher_signed: bool,
    source_commit: String,
    source_dirty: bool,
    source_archive: Option<PayloadFile>,
    files: Vec<PayloadFile>,
}
fn payload_files(root: &Path) -> Result<Vec<PayloadFile>> {
    fn visit(root: &Path, directory: &Path, files: &mut Vec<PayloadFile>) -> Result<()> {
        for entry in fs::read_dir(directory)? {
            let entry = entry?;
            let path = entry.path();
            let relative = path
                .strip_prefix(root)?
                .to_str()
                .ok_or("Package path is not Unicode")?
                .replace('\\', "/");
            if relative == "release-manifest.json" {
                continue;
            }
            let kind = entry.file_type()?;
            if kind.is_symlink() {
                let target = fs::read_link(&path)?;
                if !fs::canonicalize(&path)?.starts_with(fs::canonicalize(root)?) {
                    return Err("Package link escapes its root.".into());
                }
                let link = target
                    .to_str()
                    .ok_or("Non-Unicode package link")?
                    .to_owned();
                files.push(PayloadFile {
                    path: relative,
                    sha256: format!("{:x}", Sha256::digest(link.as_bytes())),
                    bytes: link.len() as u64,
                    link: Some(link),
                });
            } else if kind.is_dir() {
                visit(root, &path, files)?;
            } else if kind.is_file() {
                files.push(PayloadFile {
                    path: relative,
                    sha256: hash_file(&path)?,
                    bytes: entry.metadata()?.len(),
                    link: None,
                });
            } else {
                return Err("Package contains a nonregular entry.".into());
            }
        }
        Ok(())
    }
    let mut files = Vec::new();
    visit(root, root, &mut files)?;
    files.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(files)
}
fn write_package_manifest(
    root: &Path,
    release: bool,
    signed: bool,
    source: Option<&Path>,
) -> Result<()> {
    let repository = workspace().parent().unwrap().to_owned();
    let manifest = PackageManifest {
        schema: 1,
        application: "Notepad Star".into(),
        version: env!("CARGO_PKG_VERSION").into(),
        platform: if cfg!(windows) {
            "windows-x64"
        } else {
            "macos-arm64"
        }
        .into(),
        profile: if release { "release" } else { "debug" }.into(),
        channel: if release {
            "release-candidate"
        } else {
            "development"
        }
        .into(),
        publisher_signed: signed,
        source_commit: command_output(
            Command::new("git")
                .current_dir(&repository)
                .args(["rev-parse", "HEAD"]),
        )?,
        source_dirty: !command_output(
            Command::new("git")
                .current_dir(&repository)
                .args(["status", "--porcelain"]),
        )?
        .is_empty(),
        source_archive: source
            .map(|path| -> Result<PayloadFile> {
                Ok(PayloadFile {
                    path: path
                        .file_name()
                        .ok_or("Missing source archive filename")?
                        .to_string_lossy()
                        .into_owned(),
                    sha256: hash_file(path)?,
                    bytes: fs::metadata(path)?.len(),
                    link: None,
                })
            })
            .transpose()?,
        files: payload_files(root)?,
    };
    fs::write(
        root.join("release-manifest.json"),
        serde_json::to_vec_pretty(&manifest)?,
    )?;
    Ok(())
}
fn verify_package_manifest(root: &Path, release: bool, signed: bool) -> Result<()> {
    let manifest: PackageManifest =
        serde_json::from_slice(&fs::read(root.join("release-manifest.json"))?)?;
    if manifest.schema != 1
        || manifest.version != env!("CARGO_PKG_VERSION")
        || manifest.profile != if release { "release" } else { "debug" }
        || manifest.publisher_signed != signed
        || manifest.files != payload_files(root)?
    {
        return Err("Package manifest, profile or payload verification failed.".into());
    }
    if signed && cfg!(windows) {
        checked(
            Command::new("powershell.exe")
                .args(["-NoProfile", "-NonInteractive", "-File"])
                .arg(workspace().join("packaging/windows/sign.ps1"))
                .arg("-VerifyOnly")
                .arg("-Path")
                .arg(root.join("notepad-star.exe")),
        )?;
    }
    Ok(())
}
fn sign_windows(path: &Path) -> Result<()> {
    checked(
        Command::new("powershell.exe")
            .args(["-NoProfile", "-NonInteractive", "-File"])
            .arg(workspace().join("packaging/windows/sign.ps1"))
            .arg("-Path")
            .arg(path),
    )
}
fn release_guard() -> Result<()> {
    let platform = if cfg!(windows) {
        "windows-x64"
    } else {
        "macos-arm64"
    };
    let document: serde_json::Value =
        serde_json::from_slice(&fs::read(workspace().join("packaging/release-gates.json"))?)?;
    let gates = document[platform]
        .as_object()
        .ok_or("Missing release qualification gates")?;
    let required: &[&str] = if cfg!(windows) {
        &[
            "clean_machine_install_upgrade_uninstall",
            "physical_input_accessibility",
            "supported_feature_acceptance",
        ]
    } else {
        &[
            "native_build_runtime_and_relocation",
            "finder_input_accessibility",
            "install_upgrade_uninstall",
            "supported_feature_acceptance",
        ]
    };
    let mut missing = Vec::new();
    if document["schema"] != 1 {
        missing.push("unsupported gate schema".into());
    }
    for name in required {
        if !gates.contains_key(*name) {
            missing.push(format!("missing gate {name}"));
        }
    }
    for (name, gate) in gates {
        if gate["status"] != "passed"
            || gate["evidence"]
                .as_str()
                .is_none_or(|value| value.trim().is_empty())
        {
            missing.push(name.clone());
        }
    }
    let dirty = command_output(
        Command::new("git")
            .current_dir(workspace())
            .args(["status", "--porcelain"]),
    )?;
    if !dirty.is_empty() {
        missing.push("clean committed source tree".into());
    }
    for key in if cfg!(windows) {
        &[
            "NOTEPAD_STAR_CERTIFICATE_SHA1",
            "NOTEPAD_STAR_SIGNTOOL",
            "NOTEPAD_STAR_TIMESTAMP_URL",
        ][..]
    } else {
        &["NOTEPAD_STAR_MAC_IDENTITY", "NOTEPAD_STAR_NOTARY_PROFILE"][..]
    } {
        if env::var_os(key).is_none_or(|value| value.is_empty()) {
            missing.push((*key).into());
        }
    }
    if !missing.is_empty() {
        return Err(format!("Signed release is blocked: {}.", missing.join(", ")).into());
    }
    if cfg!(windows) {
        checked(
            Command::new("powershell.exe")
                .args(["-NoProfile", "-NonInteractive", "-File"])
                .arg(workspace().join("packaging/windows/sign.ps1"))
                .arg("-CheckOnly"),
        )?;
    }
    Ok(())
}
fn package(
    environment: &Environment,
    label: Option<&str>,
    release: bool,
    signed: bool,
) -> Result<()> {
    if signed {
        release_guard()?;
    }
    if release {
        build_release(environment)?;
    } else {
        build(environment)?;
    }
    let platform = if cfg!(windows) {
        "windows-x64"
    } else {
        "macos-arm64"
    };
    let name = match label {
        Some(label) if !label.is_empty() && valid_label(label) => {
            format!("{platform}-{label}")
        }
        Some(_) => {
            return Err("Package label must contain only ASCII letters, digits and hyphens.".into())
        }
        None => platform.into(),
    };
    let destination = workspace().join("dist").join(&name);
    if destination.exists() {
        return Err(format!(
            "Package destination already exists: {}. Move it aside before packaging again.",
            destination.display()
        )
        .into());
    }
    let output = workspace().join("dist").join(format!(".staging-{name}"));
    if output.exists() {
        return Err(format!(
            "An incomplete package is retained at {}. Inspect it before retrying.",
            output.display()
        )
        .into());
    }
    fs::create_dir_all(&output)?;
    let executable_path;
    if cfg!(windows) {
        executable_path = output.join("notepad-star.exe");
        fs::copy(profile_binary(release), &executable_path)?;
        let mut deploy = Command::new(environment.qt.join("bin/windeployqt.exe"));
        deploy
            .args([
                "--release",
                "--no-translations",
                "--no-opengl-sw",
                "--no-system-d3d-compiler",
                "--no-system-dxc-compiler",
                "--no-compiler-runtime",
                "--skip-plugin-types",
                "generic,networkinformation,tls",
            ])
            .arg(&executable_path);
        environment.configure(&mut deploy)?;
        checked(&mut deploy)?;
        let vswhere =
            PathBuf::from(env::var_os("ProgramFiles(x86)").ok_or("ProgramFiles(x86) is missing")?)
                .join("Microsoft Visual Studio/Installer/vswhere.exe");
        let runtime_files = command_output(Command::new(vswhere).args([
            "-latest",
            "-products",
            "*",
            "-find",
            r"VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT\*.dll",
        ]))?;
        if runtime_files.is_empty() {
            return Err("The redistributable MSVC x64 CRT files are missing.".into());
        }
        for file in runtime_files.lines() {
            let path = Path::new(file);
            fs::copy(
                path,
                output.join(path.file_name().ok_or("Invalid runtime file path")?),
            )?;
        }
    } else {
        let bundle = output.join("Notepad Star.app");
        fs::create_dir_all(bundle.join("Contents/MacOS"))?;
        executable_path = bundle.join("Contents/MacOS/notepad-star");
        fs::copy(profile_binary(release), &executable_path)?;
        let plist = fs::read_to_string(workspace().join("packaging/macos/Info.plist"))?
            .replace(
                "<string>0.1.0</string>",
                &format!(
                    "<string>{}.{}.{}</string>",
                    env!("CARGO_PKG_VERSION_MAJOR"),
                    env!("CARGO_PKG_VERSION_MINOR"),
                    env!("CARGO_PKG_VERSION_PATCH")
                ),
            )
            .replace(
                "<key>CFBundleVersion</key><string>1</string>",
                &format!(
                    "<key>CFBundleVersion</key><string>{}</string>",
                    release_revision()?
                ),
            );
        fs::write(bundle.join("Contents/Info.plist"), plist)?;
        let mut deploy = Command::new(environment.qt.join("bin/macdeployqt"));
        deploy
            .arg(&bundle)
            .arg("-always-overwrite")
            .arg("-no-codesign");
        environment.configure(&mut deploy)?;
        checked(&mut deploy)?;
    }
    let root = workspace().parent().unwrap().to_owned();
    for (source, name) in [
        ("LICENSE", "LICENSE.txt"),
        ("scintilla/License.txt", "Scintilla-LICENSE.txt"),
        ("lexilla/License.txt", "Lexilla-LICENSE.txt"),
        ("notepad-star-rust/NOTICE.txt", "NOTICE.txt"),
        (
            "notepad-star-rust/native/languages/NOTICE.txt",
            "LanguageAssets-NOTICE.txt",
        ),
        (
            "notepad-star-rust/native/function_list/NOTICE.txt",
            "FunctionList-NOTICE.txt",
        ),
    ] {
        fs::copy(root.join(source), output.join(name))?;
    }
    licenses(environment, &output)?;
    fs::copy(
        root.join("scintilla/test/unit/LICENSE_1_0.txt"),
        output.join("Boost-LICENSE.txt"),
    )?;
    fs::copy(
        workspace().join("compat/upstream.json"),
        output.join("upstream.json"),
    )?;
    fs::copy(
        workspace().join("compat/gates.json"),
        output.join("gates.json"),
    )?;
    fs::copy(workspace().join("Cargo.lock"), output.join("Cargo.lock"))?;
    if cfg!(target_os = "macos") {
        let resources = output.join("Notepad Star.app/Contents/Resources");
        fs::create_dir_all(&resources)?;
        for file in [
            "LICENSE.txt",
            "Scintilla-LICENSE.txt",
            "Lexilla-LICENSE.txt",
            "Boost-LICENSE.txt",
            "NOTICE.txt",
            "LanguageAssets-NOTICE.txt",
            "FunctionList-NOTICE.txt",
            "upstream.json",
            "gates.json",
            "Cargo.lock",
        ] {
            fs::copy(output.join(file), resources.join(file))?;
        }
        copy_tree(&output.join("licenses"), &resources.join("licenses"))?;
        fs::copy(
            workspace().join("resources/icons/notepad-star.icns"),
            resources.join("notepad-star.icns"),
        )?;
    }
    fs::copy(
        workspace().join("packaging/RELEASE-NOTES.txt"),
        output.join("RELEASE-NOTES.txt"),
    )?;
    if release {
        let notice = fs::read_to_string(output.join("NOTICE.txt"))?
            .replace("Notepad Star Rust - Development preview", "Notepad Star - Release candidate")
            .replace("This preview does not implement", "This release candidate does not implement")
            .replace("Developer artifacts are unsigned and are not verified public releases.",
                "Check release-manifest.json and the operating-system publisher signature before deployment.");
        fs::write(output.join("NOTICE.txt"), notice)?;
    }
    if cfg!(target_os = "macos") {
        let resources = output.join("Notepad Star.app/Contents/Resources");
        fs::copy(output.join("NOTICE.txt"), resources.join("NOTICE.txt"))?;
        fs::copy(
            output.join("RELEASE-NOTES.txt"),
            resources.join("RELEASE-NOTES.txt"),
        )?;
    }
    if signed {
        if cfg!(windows) {
            sign_windows(&executable_path)?;
        } else {
            checked(
                Command::new("python3")
                    .arg(workspace().join("tools/sign_macos.py"))
                    .arg(output.join("Notepad Star.app")),
            )?;
        }
    }
    // Validate the packaged executable without SDK search paths.
    let mut test = Command::new(&executable_path);
    test.arg("--smoke-test")
        .env_remove("QT_PLUGIN_PATH")
        .env_remove("QTDIR")
        .env_remove("QT_ROOT")
        .env_remove("DYLD_LIBRARY_PATH")
        .env_remove("DYLD_FRAMEWORK_PATH");
    if cfg!(windows) {
        let system = PathBuf::from(env::var_os("SystemRoot").ok_or("SystemRoot is missing")?);
        test.env("PATH", env::join_paths([system.join("System32"), system])?);
    }
    checked(&mut test)?;
    let reported_version = command_output(Command::new(&executable_path).arg("--version"))?;
    if release && !reported_version.contains("release candidate") {
        return Err("A release package contains a development executable.".into());
    }
    let source = if release {
        let archive = workspace()
            .join("dist")
            .join(format!("notepad-star-{name}-source.zip"));
        checked(
            Command::new(if cfg!(windows) { "python" } else { "python3" })
                .arg(workspace().join("tools/package_source.py"))
                .arg(&archive),
        )?;
        Some(archive)
    } else {
        None
    };
    write_package_manifest(&output, release, signed, source.as_deref())?;
    verify_package_manifest(&output, release, signed)?;
    fs::rename(&output, &destination)?;
    if cfg!(target_os = "macos") && release {
        let archive = workspace()
            .join("dist")
            .join(format!("notepad-star-{name}.zip"));
        if archive.exists() {
            return Err("The candidate bundle archive already exists.".into());
        }
        checked(
            Command::new("ditto")
                .args(["-c", "-k", "--sequesterRsrc", "--keepParent"])
                .arg(&destination)
                .arg(&archive),
        )?;
        fs::write(
            archive.with_extension("sha256"),
            format!(
                "{}  {}\n",
                hash_file(&archive)?,
                archive.file_name().unwrap().to_string_lossy()
            ),
        )?;
    }
    println!(
        "{} package: {}",
        if signed {
            "Publisher-signed release candidate"
        } else if release {
            "Unsigned release candidate"
        } else {
            "Unsigned development"
        },
        destination.display()
    );
    Ok(())
}

fn native_fixture_tests(environment: &Environment, module: &str) -> Result<()> {
    let output = workspace().join("build").join(match module {
        "languages" => "language-tests",
        "function_list" => "function-list-tests",
        _ => return Err("Unknown native fixture module.".into()),
    });
    let mut configure = Command::new(&environment.cmake);
    configure
        .arg("-S")
        .arg(workspace().join("native").join(module))
        .arg("-B")
        .arg(&output)
        .arg(format!("-DCMAKE_PREFIX_PATH={}", environment.qt.display()));
    if cfg!(windows) {
        configure.args(["-G", "Visual Studio 17 2022", "-A", "x64"]);
    } else {
        configure.args([
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
        ]);
    }
    environment.configure(&mut configure)?;
    checked(&mut configure)?;
    let mut build = Command::new(&environment.cmake);
    build
        .arg("--build")
        .arg(&output)
        .args(["--config", "Release", "--parallel", "2"]);
    environment.configure(&mut build)?;
    checked(&mut build)?;
    let ctest = environment
        .cmake
        .parent()
        .unwrap_or(Path::new(""))
        .join(executable("ctest"));
    let mut tests = Command::new(ctest);
    tests
        .arg("--test-dir")
        .arg(&output)
        .args(["-C", "Release", "--output-on-failure"]);
    environment.configure(&mut tests)?;
    checked(&mut tests)
}

fn installer(label: Option<&str>, release: bool, signed: bool) -> Result<()> {
    if !cfg!(windows) {
        return Err(
            "NSIS installer generation is Windows-only; macOS uses the application bundle.".into(),
        );
    }
    let suffix = label.unwrap_or("");
    if !suffix.is_empty() && !valid_label(suffix) {
        return Err("Invalid package label.".into());
    }
    let name = if suffix.is_empty() {
        "windows-x64".to_string()
    } else {
        format!("windows-x64-{suffix}")
    };
    let package = workspace().join("dist").join(&name);
    if !package.join("notepad-star.exe").is_file() {
        return Err("Create the corresponding developer package first.".into());
    }
    verify_package_manifest(&package, release, signed)?;
    if signed {
        release_guard()?;
    }
    let tool = env::var_os("MAKENSIS")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(env::var_os("ProgramFiles(x86)").unwrap_or_default())
                .join("NSIS/makensis.exe")
        });
    if !tool.is_file() {
        return Err("NSIS 3 is required. Set MAKENSIS to its executable path.".into());
    }
    let output = workspace().join("dist").join(format!(
        "notepad-star-{name}-{}-setup.exe",
        if release { "candidate" } else { "development" }
    ));
    if output.exists() {
        return Err("Installer output already exists; choose a new package label.".into());
    }
    fn collect(
        root: &Path,
        files: &mut Vec<PathBuf>,
        directories: &mut Vec<PathBuf>,
    ) -> Result<()> {
        for entry in fs::read_dir(root)? {
            let entry = entry?;
            let kind = entry.file_type()?;
            if kind.is_symlink() {
                return Err("Installer packages cannot contain symlinks.".into());
            }
            if kind.is_dir() {
                collect(&entry.path(), files, directories)?;
                directories.push(entry.path());
            } else if kind.is_file() {
                files.push(entry.path());
            }
        }
        Ok(())
    }
    let mut files = Vec::new();
    let mut directories = Vec::new();
    collect(&package, &mut files, &mut directories)?;
    files.sort();
    let mut script = String::new();
    for (index, file) in files.iter().enumerate() {
        let relative = file
            .strip_prefix(&package)?
            .to_str()
            .ok_or("Installer filename is not Unicode")?;
        if relative.contains(['$', '"', '\r', '\n']) {
            return Err(format!("Unsupported installer filename: {relative}").into());
        }
        script.push_str(&format!(
            "IfFileExists \"$INSTDIR\\{relative}\" 0 skip_{index}\nClearErrors\nDelete \"$INSTDIR\\{relative}\"\n${{If}} ${{Errors}}\nMessageBox MB_OK|MB_ICONSTOP \"A file could not be removed. Close the application and retry.\" /SD IDOK\nSetErrorLevel 4\nAbort\n${{EndIf}}\nskip_{index}:\n"
        ));
    }
    for directory in directories {
        let relative = directory
            .strip_prefix(&package)?
            .to_str()
            .ok_or("Invalid directory name")?;
        if relative.contains(['$', '"', '\r', '\n']) {
            return Err("Invalid installer directory name.".into());
        }
        script.push_str(&format!("RMDir \"$INSTDIR\\{relative}\"\n"));
    }
    let include = workspace()
        .join("build")
        .join(format!("uninstall-{name}.nsh"));
    fs::create_dir_all(include.parent().unwrap())?;
    fs::write(&include, script)?;
    println!(
        "Creating {} installer; no operating-system protections are changed.",
        if signed {
            "a signed candidate"
        } else {
            "an unsigned candidate/development"
        }
    );
    let mut command = Command::new(tool);
    command
        .arg("/V2")
        .arg(format!("/DPRODUCT_VERSION={}", env!("CARGO_PKG_VERSION")))
        .arg(format!(
            "/DPRODUCT_VERSION_NUMERIC={}.{}.{}.{}",
            env!("CARGO_PKG_VERSION_MAJOR"),
            env!("CARGO_PKG_VERSION_MINOR"),
            env!("CARGO_PKG_VERSION_PATCH"),
            release_revision()?
        ))
        .arg(format!("/DPACKAGE_DIR={}", package.display()))
        .arg(format!("/DOUTPUT_FILE={}", output.display()))
        .arg(format!("/DUNINSTALL_INCLUDE={}", include.display()));
    if signed {
        command.arg("/DSIGNED");
    }
    if release {
        command.arg("/DRELEASE_CANDIDATE");
    }
    command.arg(workspace().join("packaging/windows/setup.nsi"));
    checked(&mut command)?;
    fs::write(
        output.with_extension("sha256"),
        format!(
            "{}  {}\n",
            hash_file(&output)?,
            output.file_name().unwrap().to_string_lossy()
        ),
    )?;
    println!("Installer: {}", output.display());
    Ok(())
}

fn run() -> Result<()> {
    let arguments: Vec<_> = env::args().skip(1).collect();
    let action = arguments.first().map(String::as_str).unwrap_or("doctor");
    if action == "inventory" {
        return parity(true);
    }
    if action == "parity" {
        return parity(false);
    }
    if matches!(
        action,
        "installer" | "installer-release" | "installer-signed"
    ) {
        if arguments.len() > 2 {
            return Err("Usage: xtask installer [label]".into());
        }
        let release = action != "installer";
        return installer(
            arguments.get(1).map(String::as_str).or(if release {
                Some(env!("CARGO_PKG_VERSION"))
            } else {
                None
            }),
            release,
            action == "installer-signed",
        );
    }
    if action == "release-check" {
        return release_guard();
    }
    let environment = discover()?;
    match action {
        "doctor" => {
            println!("Qt {QT_VERSION}: {}", environment.qt.display());
            println!("CMake: {}", environment.cmake.display());
            println!("Native target: {}-{}", env::consts::ARCH, env::consts::OS);
            println!("macOS runtime qualification, real IME and accessibility checks remain separate gates.");
        }
        "build" => build(&environment)?,
        "build-release" => build_release(&environment)?,
        "language-test" => native_fixture_tests(&environment, "languages")?,
        "function-test" => native_fixture_tests(&environment, "function_list")?,
        "text-test" | "long-line-test" | "ui-tools-test" => {
            build(&environment)?;
            let scales: &[&str] = if action == "long-line-test" { &["1"] } else { &["1", "1.25", "1.5", "2"] };
            for scale in scales {
                let mut command = Command::new(binary());
                command.arg("--smoke-test").env(
                    match action {
                        "text-test" => "NOTEPAD_STAR_TEXT_MEASUREMENT_CHECK",
                        "ui-tools-test" => "NOTEPAD_STAR_UI_TOOLS_CHECK",
                        _ => "NOTEPAD_STAR_LONG_LINE_CHECK",
                    }, "1");
                if action != "long-line-test" { command.env("QT_SCALE_FACTOR", scale); }
                environment.configure(&mut command)?;
                checked(&mut command)?;
            }
        }
        "lint" => {
            cargo(&environment, &["fmt", "--all", "--", "--check"])?;
            cargo(
                &environment,
                &[
                    "clippy",
                    "--locked",
                    "--workspace",
                    "--all-targets",
                    "--",
                    "-D",
                    "warnings",
                ],
            )?;
        }
        "run" => {
            build(&environment)?;
            let mut command = Command::new(binary());
            command.args(&arguments[1..]);
            environment.configure(&mut command)?;
            checked(&mut command)?;
        }
        "test" => {
            cargo(
                &environment,
                &[
                    "test",
                    "--locked",
                    "-p",
                    "star-io",
                    "-p",
                    "star-core",
                    "-p",
                    "star-search",
                    "-p",
                    "star-extensions",
                    "-p",
                    "xtask",
                ],
            )?;
            parity(false)?;
            build(&environment)?;
            let mut command = Command::new(binary());
            command.arg("--smoke-test");
            environment.configure(&mut command)?;
            checked(&mut command)?;
        }
        "package" | "package-release" | "package-signed" => {
            if arguments.len() > 2 {
                return Err("Usage: xtask package [label]".into());
            }
            let release = action != "package";
            package(&environment, arguments.get(1).map(String::as_str).or(if release { Some(env!("CARGO_PKG_VERSION")) } else { None }), release, action == "package-signed")?;
        }
        _ => return Err("Usage: xtask doctor|inventory|parity|build|run|test|lint|language-test|function-test|text-test|long-line-test|ui-tools-test|package|installer".into()),
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("xtask: {error}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn release_payload_verification_rejects_changes_and_wrong_profiles() {
        let root = tempfile::tempdir().unwrap();
        let executable = root.path().join("notepad-star.exe");
        fs::write(&executable, b"abc").unwrap();
        assert_eq!(
            hash_file(&executable).unwrap(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        write_package_manifest(root.path(), true, false, None).unwrap();
        verify_package_manifest(root.path(), true, false).unwrap();
        assert!(verify_package_manifest(root.path(), false, false).is_err());
        fs::write(&executable, b"changed").unwrap();
        assert!(verify_package_manifest(root.path(), true, false).is_err());
        fs::write(&executable, b"abc").unwrap();
        fs::write(root.path().join("unexpected.dll"), b"extra").unwrap();
        assert!(verify_package_manifest(root.path(), true, false).is_err());
        assert!(valid_label("0.1.0-rc.1"));
        assert!(!valid_label("../release"));
        assert!(!valid_label("release."));
        assert!(!valid_label(".."));
    }
    #[test]
    fn inventory_is_deterministic_and_keeps_evidence() {
        let inventory = inventory().unwrap();
        assert!(inventory.features.len() > 500);
        assert!(inventory
            .features
            .windows(2)
            .all(|pair| pair[0].id < pair[1].id));
        assert!(inventory
            .features
            .iter()
            .any(|entry| entry.id == "IDM_SEARCH_FINDINFILES"));
        assert!(inventory
            .features
            .iter()
            .any(|entry| entry.kind == "preference-control"));
        assert!(inventory
            .features
            .iter()
            .all(|entry| entry.windows != "verified"));
    }
}
