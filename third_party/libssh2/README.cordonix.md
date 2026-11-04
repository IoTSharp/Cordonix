# libssh2 source dependency

Cordonix statically links the unchanged C sources from libssh2 1.11.1.
Windows uses its WinCNG backend and system bcrypt/crypt32/Winsock libraries;
no external SSH process, OpenSSL DLL, .NET or product-specific client is used.

- Upstream: https://github.com/libssh2/libssh2/releases/tag/libssh2-1.11.1
- Archive: https://github.com/libssh2/libssh2/releases/download/libssh2-1.11.1/libssh2-1.11.1.tar.gz
- SHA-256: `d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7`
- License: BSD-3-Clause, retained verbatim in `COPYING`.
- Regenerate copied sources with PowerShell 7: `eng/vendor-libssh2.ps1 -ArchivePath <verified-archive>`.

Only upstream source/header files, the source list and license are copied.
`eng/build-ssh.cmd` supplies the native build configuration; edit that source
or the version/hash in the vendor script and regenerate, never patch copied
upstream files independently. Linux currently reports `not_supported` for
the SSH operation while its existing gates remain available.
