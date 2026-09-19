# Desktop release procedure

The current version is **0.1.0-rc.7**. Release candidates are optimized builds for
controlled evaluation of the documented feature set, not a declaration of full
Notepad++ parity or completion of every platform/release gate.

## Build a candidate

From the repository root with the pinned prerequisites installed:

```powershell
cargo run --locked -p xtask -- lint
cargo run --locked -p xtask -- test
cargo run --locked -p xtask -- package-release
cargo run --locked -p xtask -- installer-release
```

The last command is Windows-only. The release profile enables Rust optimization,
thin LTO, one code-generation unit and abort-on-panic; the native libraries retain
their optimized build and C++ exception cleanup. Unlike development builds, the
candidate window has no developer banner. About/version output still identifies
the candidate honestly.

Windows output is `dist\windows-x64-0.1.0-rc.7`, plus its `candidate-setup.exe`.
macOS output contains `Notepad Star.app`. Each package includes
`release-manifest.json`, file hashes, version/profile/channel information, notices
and release notes. A corresponding source ZIP is produced beside the package.
The Mac bundle is also archived with `ditto` to preserve executable permissions
and framework links; distribute that ZIP, not an artifact service's re-zipped
raw `.app` directory.
The manifest records uncommitted source state rather than pretending a local
candidate came from a clean release commit.

Packaging uses a staging directory and publishes the final directory only after
the actual candidate executable passes its relocated smoke check. A failed
staging directory is retained for inspection; existing packages are never
overwritten. The installer refuses payload/profile/hash mismatches.

## Qualification before a publisher-signed build

Complete the applicable entries in `release-gates.json` with **real evidence**,
including installer lifecycle, native platform behavior, physical input and
accessibility, and acceptance of the supported feature set. Do not mark a gate
passed merely because an unsigned build exists.

Two gates cover the security posture of the shipped bytes:

- `dependency_vulnerability_audit` — record the advisory scan of the committed
  `Cargo.lock` and of the pinned Qt/Scintilla/Lexilla/Boost/pugixml sources, with
  the date, the advisory database revision and the disposition of every hit.
- `exploit_mitigations_verified` — record that the produced binary carries the
  mitigations configured in `native/CMakeLists.txt` and `.cargo/config.toml`
  (`dumpbin /headers /loadconfig` on Windows for Control Flow Guard, DEP, ASLR
  and high-entropy VA; `otool -hv` plus `codesign -d --entitlements -` on macOS
  for PIE, the hardened runtime and the absence of loosening entitlements).

The `Desktop release candidates` workflow builds both targets. On its disposable
Windows runner, `windows\test-install.ps1` exercises install, running-app
protection, reinstall, uninstall, and preservation of profile/unknown user files.
The script refuses to run on a normal developer profile. A hosted runner result
does not substitute for stock clean-machine and physical accessibility checks.

Commit the exact reviewed source and lockfile. A signed build rejects a dirty
worktree, pending/missing gates, and absent publisher configuration:

```powershell
cargo run --locked -p xtask -- release-check
cargo run --locked -p xtask -- package-signed
cargo run --locked -p xtask -- installer-signed
```

Use a fresh output directory/checkout when switching from unsigned to signed
artifacts. Do not rename an unsigned candidate to imply it is signed.

### Windows publisher setup

Provision a real code-signing certificate/private key in `CurrentUser\My` on a
trusted publisher machine. Configure these environment variables outside source:

- `NOTEPAD_STAR_CERTIFICATE_SHA1`: certificate thumbprint.
- `NOTEPAD_STAR_SIGNTOOL`: Windows SDK `signtool.exe`.
- `NOTEPAD_STAR_TIMESTAMP_URL`: the publisher's HTTPS RFC 3161 timestamp service.

`sign.ps1` checks certificate validity, private-key availability and code-signing
usage, then verifies the resulting trusted, timestamped publisher signature.
The application, installer and generated uninstaller are signed. No self-signed
substitute, Defender exclusion or reputation guarantee is supplied.

### macOS publisher setup

Use a real Mac with a Developer ID Application identity in its keychain and a
pre-provisioned `notarytool` keychain profile:

- `NOTEPAD_STAR_MAC_IDENTITY`
- `NOTEPAD_STAR_NOTARY_PROFILE`

`tools\sign_macos.py` signs nested Mach-O code/bundles inside-out with hardened
runtime and timestamps, verifies signatures, waits for accepted notarization,
staples the ticket and performs Gatekeeper assessment. It fails on any missing
credential or rejected result. It does not remove quarantine or relax Gatekeeper.
This path still requires actual Mac execution; Windows cross-checks do not qualify it.

The publisher workflow uses a **protected `desktop-release` environment** and
trusted self-hosted runners labeled `notepad-star-release`, with matching OS/CPU.
Provision Qt, publisher credentials and notarization profiles there. It runs only
from the repository default branch, never from pull requests, and uploads signed
candidates rather than automatically publishing an unreviewed public release.

## Deploy and preserve user data

Close an existing editor normally before upgrading its installed or portable
directory. The current running process cannot change its UI when a new binary is
built. The existing internal application-data/bundle identity is deliberately
retained, even though the visible product name is now Notepad Star, so profiles,
sessions and recovery are not stranded by a cosmetic rename.

The Windows installer is per-user and does not require elevation. It registers
the application and Start Menu shortcut without taking over text-file defaults.
Uninstall removes its named payload files, not profiles/documents or unknown files.
Verify publisher signatures and distribute the matching source archive/notices.
Checksums alone do not authenticate an unsigned download.

At the time this pipeline was added, publisher credentials, a native Mac run,
stock clean-machine qualification and physical accessibility acceptance were not
available in the development session. Those gates intentionally remain pending.
