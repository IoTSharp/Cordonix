# Cordonix Changelog

## Unreleased

### Added

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
- MCP encoding verification reuses the active worker for notification/EOF cancellation; busy messages describe an active operation.
- Roadmap progress uses emoji markers with separate PASS/FAIL/UNKNOWN/NOT_RUN evidence.

### Fixed

- Linux UTF-8 validation rejects historic iconv sequences above U+10FFFF.
- Linux execution preserves the calling process when a child closes stdin, accepts root cwd, and keeps cleanup bounded after the root exits or closes its streams.
- Linux MCP stops dispatching buffered requests after output failure or interruption, cancels active work on EOF, and restores inherited stdout flags.
- Windows packaging resolves one Git executable when multiple installations are available and emits UTF-8 subprocess diagnostics.
- Encoding verification rejects a BOM-selected decoder that conflicts with the declared encoding instead of reporting a false declared-encoding pass.
