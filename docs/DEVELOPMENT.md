# Development guide

Prerequisites and day-to-day commands for building Notepad Star from source.
See the [packaging guide](PACKAGING.md) for producing distributable artifacts.

## Toolchain

- Rust **1.97.1**, pinned in `rust-toolchain.toml`.
- Qt **6.8.3**: Core, Gui, Widgets, Core5Compat, Test, PrintSupport and Network.
- CMake 3.24 or newer.
- Windows: x64 MSVC, Visual Studio 2022 C++ tools and Windows SDK.
- macOS: native Apple Silicon Rust toolchain, Xcode command line tools;
  deployment target macOS 13.0. An actual Mac/runner is required for validation.

`QT_ROOT` must identify the matching Qt SDK. `xtask` also recognizes the
project-local paths below and discovers Visual Studio's CMake on Windows.
Override `CMAKE` if necessary. No SDK installation occurs implicitly during builds.

If Qt is not installed, these explicit provisioning commands use the official
Qt package repositories and a pinned provisioning tool:

```powershell
# From the repository root, on Windows:
python -m pip install aqtinstall==3.3.0
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qt5compat --outputdir .\build\qt
cargo run --locked -p xtask -- doctor
cargo run --locked -p xtask -- run --preview
```

On an Apple Silicon Mac, run the equivalent commands from the repository root:

```sh
python3 -m pip install aqtinstall==3.3.0
python3 -m aqt install-qt mac desktop 6.8.3 clang_64 -m qt5compat --outputdir build/qt
cargo run --locked -p xtask -- doctor
cargo run --locked -p xtask -- run --preview
```

Qt's `clang_64` distribution is used for the macOS SDK; CMake explicitly builds
our native code for `arm64`, and `doctor` rejects an Intel Rust host for this
initial target. Do not infer Apple Silicon correctness from the SDK label.
The portable Rust crates also type-check for `aarch64-apple-darwin` on the Windows
development host. This checks target-specific Rust code only: it does not build
or execute the Qt `.app`, run Unix tests, or qualify macOS behavior.

## Development commands

All commands run from the repository root:

```text
cargo run --locked -p xtask -- doctor
cargo run --locked -p xtask -- build
cargo run --locked -p xtask -- run --preview
cargo run --locked -p xtask -- test
cargo run --locked -p xtask -- lint
cargo run --locked -p xtask -- parity
cargo run --locked -p xtask -- language-test
cargo run --locked -p xtask -- function-test
cargo run --locked -p xtask -- text-test
cargo run --locked -p xtask -- long-line-test
```

The native build is currently Release-mode Qt/C++ linked into a Rust development
binary. `xtask` configures library paths for development launches. To launch the
executable independently, use the packaged directory rather than relying on
the developer's Qt installation.
C++ exception unwinding is explicitly enabled in both the CXX bridge and native
CMake targets: Cargo's native-build flags must not disable RAII cleanup on errors.
Release native targets also set optimization and `NDEBUG` explicitly, since the
Cargo/CMake adapter can otherwise replace CMake's normal Release compiler flags.

The smoke test exercises a real Qt window: file/session safety, split views,
synthetic IME, isolated regex and disk search, zero-width navigation, cancellation,
macro replay/undo, conversions, PDF output, persisted settings and a real Wasm
extension invocation. It is not a
substitute for physical IME, accessibility or full compatibility testing.

`test` uses disposable documents and packages; it does not manipulate unrelated
application windows. Search workers have a 5-second deadline, 32 MiB input limit
and 10,000-match cap. Directory search examines at most 10,000 entries and 1,000
matching files, without following symlinks. Regex parity fixtures remain incomplete.
Extension workers use tighter 2 MiB protocol limits, 256 KiB selected-text/output
limits, instruction fuel, fixed linear memory and no guest imports/WASI.

