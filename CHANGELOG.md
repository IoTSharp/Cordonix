# Cordonix Changelog

## Unreleased

- Added native read-only Windows `query_processes` to the C API, CLI and asynchronous MCP worker, preserving original command lines and exact creation times with explicit partial/timeout/cancellation/output-limit states. Linux returns `not_supported` without an interpreter fallback.
- Added 19 real native checks and three Windows/two Linux MCP checks for the new operation, plus MSVC static analysis and AddressSanitizer coverage.

## 0.0.4 - 2026-10-30

- Added GitHub Actions OIDC artifact provenance for both release archives through the official, commit-pinned attestation action. Sigstore bundles accompany the immutable archive assets on GitHub and Gitee.
- Independent verification binds the artifact digest, repository, workflow, source commit and release tag. This is artifact provenance signing, not Windows Authenticode.

## 0.0.3 - 2026-10-30

- Added per-child environment overrides, including deletion and empty values, without mutating the parent. Real process checks cover Unicode, platform name matching and simultaneous child isolation.
- Added parsed external ripgrep JSON and Git porcelain v2 NUL adapters, with raw byte preservation, rename paths and distinct no-match/failure/limit states.
- Added independent Linux client acceptance across ext4/tmpfs/9p, a 60-round/600-second finite soak, and explicit crash-before-commit disk readback.
- Added actual Windows/Linux natural scheduler acceptance scripts and failure/cleanup regressions; first natural invocations have passed while hourly recurrence is recorded separately.
- Added embedded build revision/dirty readback, clean-release provenance checks, official SPDX 2.3 schema validation and same-environment compiler reproducibility gates.
- Added a fixed 128 KiB fresh-versus-memory read benchmark. OS I/O counters include pipes; physical disk I/O and model token counts remain unmeasured.

- Clarified flat argv versus PowerShell named array binding, call-scoped cleanup for persistent browser launchers, and output decoding versus process status in MCP tool descriptions and bilingual usage guidance.
- Exposed existing active-process observations before and after cleanup through `execute_program`; retained process ownership and exit-status behavior. Added real MCP coverage for successful-root descendant cleanup and independent encoding/process results.

- Added native `create_file` to CLI/MCP and the public C API: strict declared encoding, same-directory temporary writes, no replacement of existing paths, cancellation and explicit write/cleanup results.
- Added 20 native create-file checks on each platform and eight shared real MCP checks; the original create-file batch passed 225 Windows and 211 Linux checks.
- Added a separate real ext4 MCP gate and CI step for invalid UTF-8 directory entries, owner/group/mode and external permission changes. ACL/xattr non-preservation remains explicit.
- Verified real Windows Task Scheduler registration, one manually triggered hourly-update action, completion and removal in an isolated public-v0.0.2 installation (seven checks). Natural periodic firing and Linux scheduler registration remain separate.

## 0.0.2 - 2026-10-29

- Published Windows x64 ZIP and Linux x64 tar.gz on GitHub and Gitee from `c41ab22` through a fully passing tagged workflow.
- Added verified bootstrap installation, hourly user update support and a copyable AI setup prompt; both platforms passed real 0.0.1-to-0.0.2 package upgrade checks.
- Fixed original annotated-tag mirroring, Linux checksum paths/SPDX metadata and the reserved Windows encoding fixture filename.

## 0.0.1 - 2026-10-29

Initial native runtime release, published on both platforms from `586a240`. The detailed implementation and follow-up additions are listed below; the install/update and mirror fixes are included in 0.0.2.

### Added

- Windows and Linux bootstrap installers with exact release-asset selection, SHA-256 and native runtime verification, bounded GitHub/Gitee fallback, preserved previous versions and stable MCP launch paths.
- Optional hourly user updates through PowerShell 7 / Task Scheduler on Windows and systemd user service/timer on Linux; isolated HTTP, rollback and scheduler-definition gates.
- A recovery workflow to mirror an existing GitHub release to Gitee without rebuilding or overwriting published assets.

- Initial Cordonix product charter, short form `cdx`, roadmap identifiers and eight agent rules.
- Native C11 CLI JSON request mode and persistent MCP stdio mode.
- Windows process ownership, literal argv, bounded output, cancellation and whole-Job cleanup.
- Session documents with exact SHA/count edits, repeated memory reads/search and explicit single-file commits.
- Strict UTF-8/BOM, UTF-16LE/BE BOM and explicit CP936/GBK preservation and lossless transcoding.
- Linux backend with literal UTF-8 argv, process-group ownership, nonblocking pipes, timeout/cancellation and bounded cleanup observation.
- Linux regular-file document commits using same-directory temporary files, fsync, rename, identity/SHA checks and readback; owner/group/mode preservation.
- Linux C11 build and native process/document/JSON/stdio gates, including output backpressure and SIGTERM cases.
- Human-readable Chinese MCP tool titles that distinguish memory edits from disk commits, while retaining tool identifiers.
- Deterministic Windows portable ZIP, SHA-256 manifest and sidecar, packaging-checkout provenance, SPDX 2.3 file inventory, and extraction/CLI validation.
- Versioned builds through the `CDX_VERSION` compile contract, with tagged `vMAJOR.MINOR.PATCH` Windows/Linux release packaging and checksum assets.
- GitHub `IoTSharp/Cordonix` release workflow with optional Gitee source mirroring and dual-platform asset publication through a protected `GITEE_TOKEN` secret.
- Installation and update guidance with a copyable AI prompt, exact asset selection, SHA-256 verification, bounded GitHub-to-Gitee fallback and rollback-aware Codex configuration switching.
- Apache-2.0 license, contributor/security guidance and bilingual README.

- One-shot `read_file` with explicit encoding and 1-based line ranges, also available through CLI `--request`.
- Line-range mode for `workspace_read`, with whole-line byte limits, EOF/next-line metadata and disk/memory source distinction.

- Tail (`tail_lines`) and complete decoded-text (`raw`) modes for disk and memory reads, with explicit output-limit errors instead of partial complete-read results.
- Portable ZIP includes the first-lines, line-range, tail and raw request examples referenced by the README.

- Native `list_directory` and `resolve_path` tools for bounded, nonrecursive hidden-aware directory queries and normalization of existing absolute filesystem paths, with explicit missing/permission/encoding states.

- Native `verify_encoding` batches with explicit encoding, expected path count, lexical root preflight, per-file metadata and conservative failure/NOT_RUN reporting.
- Pure path normalization and containment helpers shared by filesystem queries and batch preflight, without filesystem access during validation.

### Changed

- Shared document edit, conversion, encoding/EOL metadata and cleanup logic between Windows and Linux; kept file and encoding APIs in platform sources.
- Portable public path and cancellation types preserve the Windows C types; Linux execution results use `posix_error`.
- Shared native JSON and MCP behavior tests run on both platforms.
- GitHub Actions now defines Windows portable-package and Linux native gates.
- Tagged release CI now builds and verifies Windows x64 ZIP and Linux x64 tar.gz assets, publishes the GitHub release, mirrors the tag to Gitee and uploads the same assets when the Gitee repository and secret are configured.
- Canonical repository links use `IoTSharp/Cordonix`; the former `maikebing/Cordonix` URL remains only as a redirect target.
- MCP encoding verification reuses the active worker for notification/EOF cancellation; busy messages describe an active operation.
- Roadmap progress uses emoji markers with separate PASS/FAIL/UNKNOWN/NOT_RUN evidence.

### Fixed

- Gitee source mirroring preserves the original annotated tag object instead of the lightweight event tag produced by Actions checkout.
- Linux package checksums resolve relative filenames from the package directory; SPDX verification codes, package relationships and checkout provenance now carry actual content evidence.
- Windows encoding fixtures avoid the reserved `nul.txt` device name, so the hosted Windows gate exercises a real NUL-containing file.

- Linux UTF-8 validation rejects historic iconv sequences above U+10FFFF.
- Linux execution preserves the calling process when a child closes stdin, accepts root cwd, and keeps cleanup bounded after the root exits or closes its streams.
- Linux MCP stops dispatching buffered requests after output failure or interruption, cancels active work on EOF, and restores inherited stdout flags.
- Windows packaging resolves one Git executable when multiple installations are available and emits UTF-8 subprocess diagnostics.
- Encoding verification rejects a BOM-selected decoder that conflicts with the declared encoding instead of reporting a false declared-encoding pass.
