# Notepad Star

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="notepad-star-rust/resources/brand/notepad-star-wordmark-dark.svg">
  <img alt="Notepad Star" src="notepad-star-rust/resources/brand/notepad-star-wordmark.svg" width="540">
</picture>

The new **Rust + Qt application** is in
[`notepad-star-rust`](notepad-star-rust/README.md). It targets Windows x64 and
macOS Apple Silicon with a native, Windows-like editor layout. The Windows
release candidate includes guarded saves, sessions/recovery, split views,
bounded regex/file search, column editing, basic macro import, printing and
local Wasm extensions. **Full Notepad++ parity and macOS qualification remain
in progress.**

To open the packaged Windows candidate, double-click **Open Notepad Star.cmd**
in this folder. The current application is kept at
`notepad-star-rust\dist\windows-x64-0.1.0-rc.6\notepad-star.exe`; no build tools are needed
to run that packaged copy.
See the [release procedure](notepad-star-rust/packaging/RELEASE.md) for publisher
signing, deployment validation, checksums and remaining qualification gates.

With the prerequisites in the [Rust development guide](notepad-star-rust/README.md)
installed, build and open that preview:

```powershell
Set-Location .\notepad-star-rust
cargo run --locked -p xtask -- run --preview
```

The earlier [Windows C++ preview](app/README.md) remains available independently.
Neither application replaces the Notepad++ application or installer. The
unchanged upstream sources below are a reference, not this application's
publisher identity. Developer packages are unsigned; no OS protections are
disabled.

What is Notepad++ ?
===================

[![GitHub release](https://img.shields.io/github/release/notepad-plus-plus/notepad-plus-plus.svg)](../../releases/latest)&nbsp;&nbsp;&nbsp;&nbsp;[![Build Status](https://img.shields.io/github/actions/workflow/status/notepad-plus-plus/notepad-plus-plus/CI_build.yml)](https://github.com/notepad-plus-plus/notepad-plus-plus/actions/workflows/CI_build.yml)
&nbsp;&nbsp;&nbsp;&nbsp;[![Join the discussions at https://community.notepad-plus-plus.org/](https://notepad-plus-plus.org/assets/images/NppCommunityBadge.svg)](https://community.notepad-plus-plus.org/)

Notepad++ is a free (free as in both "free speech" and "free beer") source code
editor and Notepad replacement that supports several programming languages and
natural languages. Running in the MS Windows environment, its use is governed by
[GPL License](LICENSE).

See the [Notepad++ official site](https://notepad-plus-plus.org/) for more information.


Notepad++ GPG Release Key
-------------------------
_Since the release of version 7.6.5 Notepad++ is signed using GPG with the following key:_

- **Signer:** Notepad++
- **E-mail:** don.h@free.fr
- **Key ID:** 0x8D84F46E
- **Key fingerprint:** 14BC E436 2749 B2B5 1F8C 7122 6C42 9F1D 8D84 F46E
- **Key type:** RSA 4096/4096
- **Created:** 2019-03-11
- **Expires:** 2027-03-13

https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/nppGpgPub.asc


Supported OS
------------

All the Windows systems still supported by Microsoft are supported by Notepad++. However, not all Notepad++ users can or want to use the newest system. Here is the [Supported systems information](SUPPORTED_SYSTEM.md) you may need in case you are one of them.




Build Notepad++
---------------

Please follow [build guide](BUILD.md) to build Notepad++ from source.


Contribution
------------

Contributions are welcome. Be mindful of our [Contribution Rules](CONTRIBUTING.md) to increase the likelihood of your contribution getting accepted.

[Notepad++ Contributors](https://github.com/notepad-plus-plus/notepad-plus-plus/graphs/contributors)
