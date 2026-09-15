use std::{env, path::PathBuf};

fn main() {
    let native = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap()).join("../../native");
    let qt = env::var_os("QT_ROOT")
        .map(PathBuf::from)
        .expect("QT_ROOT is required. Run `cargo run -p xtask -- doctor` for setup instructions.");
    let output = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let target = env::var("CARGO_CFG_TARGET_OS").unwrap();
    assert!(
        target == "windows" || target == "macos",
        "Only Windows and macOS are supported by the foundation."
    );

    let mut bridge = cxx_build::bridge("src/lib.rs");
    bridge.include(&native).std("c++17");
    if target == "windows" {
        bridge.flag("/EHsc");
    } else {
        bridge.flag("-fexceptions");
    }
    bridge.compile("star-cxx");
    let mut build = cmake::Config::new(&native);
    build
        .profile("Release")
        .define("CMAKE_PREFIX_PATH", &qt)
        .define("STAR_CXX_INCLUDE", output.join("cxxbridge/include"))
        .define("CMAKE_INSTALL_LIBDIR", "lib");
    if target == "macos" {
        build
            .define("CMAKE_OSX_ARCHITECTURES", "arm64")
            .define("CMAKE_OSX_DEPLOYMENT_TARGET", "13.0");
    }
    let artifacts = build.build();
    println!(
        "cargo:rustc-link-search=native={}",
        artifacts.join("lib").display()
    );
    for library in [
        "star-shell",
        "star-pugixml",
        "star-languages",
        "star-function-list",
        "star-scintilla",
        "star-lexilla",
    ] {
        println!("cargo:rustc-link-lib=static={library}");
    }
    if target == "windows" {
        println!(
            "cargo:rustc-link-search=native={}",
            qt.join("lib").display()
        );
        for library in [
            "Qt6Widgets",
            "Qt6Gui",
            "Qt6Core5Compat",
            "Qt6Test",
            "Qt6PrintSupport",
            "Qt6Network",
            "Qt6Core",
        ] {
            println!("cargo:rustc-link-lib=dylib={library}");
        }
    } else {
        println!(
            "cargo:rustc-link-search=framework={}",
            qt.join("lib").display()
        );
        for library in [
            "QtWidgets",
            "QtGui",
            "QtCore5Compat",
            "QtTest",
            "QtPrintSupport",
            "QtNetwork",
            "QtCore",
        ] {
            println!("cargo:rustc-link-lib=framework={library}");
        }
        println!("cargo:rustc-link-lib=c++");
        println!(
            "cargo:rustc-link-arg=-Wl,-rpath,{}",
            qt.join("lib").display()
        );
    }
    println!("cargo:rerun-if-env-changed=QT_ROOT");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed={}", native.display());
    println!(
        "cargo:rerun-if-changed={}",
        native.join("../resources/icons").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        native.join("../resources/brand").display()
    );
    let upstream = native.join("../reference/notepad-plus-plus");
    for source in ["scintilla", "lexilla", "boostregex"] {
        println!("cargo:rerun-if-changed={}", upstream.join(source).display());
    }
    for source in [
        "PowerEditor/src/langs.model.xml",
        "PowerEditor/src/pugixml",
        "PowerEditor/src/stylers.model.xml",
        "PowerEditor/src/ScintillaComponent",
        "PowerEditor/installer/APIs",
        "PowerEditor/installer/functionList",
        "PowerEditor/installer/themes/DarkModeDefault.xml",
        "PowerEditor/bin/userDefineLangs",
    ] {
        println!("cargo:rerun-if-changed={}", upstream.join(source).display());
    }
}
