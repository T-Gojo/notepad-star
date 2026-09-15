# Notepad++ (retained upstream checkout)

This is the unmodified [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus)
source this repository was forked from, pinned at commit
`572650c1894501ace6050ae5c90a9c70cb4691dc` (version 8.9.8).

**It is not the product built by this repository.** Notepad Star is a separate
application that lives at the repository root; see the top-level
[`README.md`](../../README.md) and [`reference/README.md`](../README.md) for what
is retained here and which parts the Notepad Star build actually consumes.

The upstream build instructions ([`BUILD.md`](BUILD.md)), contribution rules
([`CONTRIBUTING.md`](CONTRIBUTING.md)), supported-system notes
([`SUPPORTED_SYSTEM.md`](SUPPORTED_SYSTEM.md)), CI definitions (`.github/`,
`appveyor.yml`) and release key (`nppGpgPub.asc`) are kept alongside the sources
so the checkout stays self-describing. The workflows under `.github/` here are
inert: GitHub only runs workflows stored in the repository's root `.github/workflows`
directory.

Issues and contributions for Notepad++ itself belong in the upstream project,
not in this repository.

---

The original upstream README follows.

What is Notepad++ ?
===================

Notepad++ is a free (free as in both "free speech" and "free beer") source code
editor and Notepad replacement that supports several programming languages and
natural languages. Running in the MS Windows environment, its use is governed by
[GPL License](../../LICENSE).

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
