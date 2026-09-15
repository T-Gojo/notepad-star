use std::{env, fs, path::PathBuf};

fn main() {
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    let root = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap()).join("../..");
    let icon = root
        .join("resources/icons/notepad-star.ico")
        .canonicalize()
        .expect("Generate the application icon.");
    let manifest = root
        .join("packaging/windows/app.manifest")
        .canonicalize()
        .unwrap();
    let version = env::var("CARGO_PKG_VERSION").unwrap();
    let prerelease = env::var("CARGO_PKG_VERSION_PRE").unwrap();
    let revision = if prerelease.is_empty() {
        0
    } else {
        prerelease
            .rsplit('.')
            .next()
            .unwrap()
            .parse::<u16>()
            .expect("Prerelease version must end in a numeric Windows resource revision")
    };
    let numeric = [
        env::var("CARGO_PKG_VERSION_MAJOR").unwrap(),
        env::var("CARGO_PKG_VERSION_MINOR").unwrap(),
        env::var("CARGO_PKG_VERSION_PATCH").unwrap(),
        revision.to_string(),
    ]
    .join(",");
    let resource = format!(
        r#"
#include <windows.h>
1 ICON "{}"
1 RT_MANIFEST "{}"
1 VERSIONINFO
FILEVERSION {numeric}
PRODUCTVERSION {numeric}
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEFLAGS VS_FF_PRERELEASE
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904B0"
  BEGIN
   VALUE "CompanyName", "Notepad Star contributors\0"
   VALUE "FileDescription", "Notepad Star\0"
   VALUE "FileVersion", "{version}\0"
   VALUE "InternalName", "notepad-star\0"
   VALUE "OriginalFilename", "notepad-star.exe\0"
   VALUE "ProductName", "Notepad Star\0"
   VALUE "ProductVersion", "{version}\0"
   VALUE "LegalCopyright", "Copyright (C) 2026 Notepad Star contributors; GPL-3.0-or-later\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x0409, 1200
 END
END
"#,
        icon.display().to_string().replace('\\', "\\\\"),
        manifest.display().to_string().replace('\\', "\\\\")
    );
    let output = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("application.rc");
    fs::write(&output, resource).unwrap();
    embed_resource::compile(&output, embed_resource::NONE)
        .manifest_required()
        .expect("Windows application resources must compile.");
    println!("cargo:rerun-if-changed={}", icon.display());
    println!("cargo:rerun-if-changed={}", manifest.display());
    println!("cargo:rerun-if-changed=build.rs");
}
