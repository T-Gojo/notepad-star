# Packaging guide

Producing developer packages and installers. Versioned release candidates and
release gates are covered by the [release procedure](../packaging/RELEASE.md).

## Developer packaging

Packaging needs the official Qt source license notices in addition to the SDK.
For a project-local Windows installation, from the repository root:

```powershell
python -m aqt install-src windows 6.8.3 --archives qtbase qt5compat qtsvg --outputdir build\qt
cargo run --locked -p xtask -- package
```

Use `cargo run --locked -p xtask -- package r1` to build a separate labeled
package without changing an older package that may still be running.

On Windows, after creating a package, compile a per-user development installer:

```powershell
cargo run --locked -p xtask -- installer r1
```

This requires NSIS 3 (`MAKENSIS` may override its path). It does not install the
application on the build machine. The installer uses a separate application
directory and registry identity, and uninstall preserves user documents/settings.
Installer lifecycle testing on a clean Windows machine remains a release gate.

`packaging\windows\sign.ps1` provides a fail-closed signing hook for a publisher
certificate and timestamp service. It requires `NOTEPAD_STAR_CERTIFICATE_SHA1`,
`NOTEPAD_STAR_SIGNTOOL` and `NOTEPAD_STAR_TIMESTAMP_URL`; the standard developer
installer explicitly remains unsigned. No signing credentials are included.

Use `mac` in place of `windows` on macOS. Set `QT_SOURCE_ROOT` if the source
tree is not beside the SDK as `6.8.3\Src`.

`package` stages an unsigned developer artifact, runs Qt's deployment tool,
includes source/license provenance and Rust/Qt license notices, then launches
the packaged smoke test without SDK library search paths. It refuses to
overwrite an existing destination; move a previous package aside explicitly.

Windows output: `dist\windows-x64\notepad-star.exe`.
macOS output: the application bundle inside `dist\macos-arm64`.
macOS bundles are **not notarized or signed for public distribution**.
No Gatekeeper or Defender settings are changed.

