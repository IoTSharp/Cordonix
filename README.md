# Cordonix

**A native execution and memory workspace for AI coding agents.**

[![Windows checks](https://github.com/maikebing/Cordonix/actions/workflows/ci.yml/badge.svg)](https://github.com/maikebing/Cordonix/actions/workflows/ci.yml)

[简体中文](README.zh-CN.md) · [Roadmap](ROADMAP.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)

Cordonix, abbreviated **cdx**, gives agents a direct path to local programs and source files. Program arguments remain data. Text keeps its declared encoding. Repeated reads and edits can stay in memory until an explicit commit.

The current implementation is **pure C11 for Windows**. Its executable does not need Python or PowerShell at runtime. Windows development uses MSVC; Linux support is planned. Cordonix is an independent open-source project, not an OpenAI product.

## Why it exists

AI coding workflows repeatedly encounter the same boundaries: shell quoting changes arguments, legacy encodings become replacement characters, stale edits overwrite newer content, and cancelled commands leave child processes running.

Cordonix makes those boundaries explicit through typed requests and observable outcomes:

- Start an executable with `program`, `args[]`, `cwd` and a timeout. Windows Job Objects own the created process tree.
- Load source once, inspect and edit its memory revision, and commit only when requested.
- Preserve UTF-8, UTF-16, BOMs, line endings and explicitly selected CP936/GBK without silent lossy conversion.
- Check the expected SHA-256 and occurrence count before an edit; check the disk revision again before commit.
- Keep process exit codes, validation errors, encoding errors, conflicts and dirty memory state separate.

Git, ripgrep, compilers and Codex's `apply_patch` remain useful companions. Cordonix provides the execution and document boundary around them.

## Status

Cordonix is a development MVP. We use it in our own Windows coding workflow before widening platform and packaging support.

| Area | Current evidence | Remaining boundary |
|---|---|---|
| Windows process core | 15 checks passed locally | Broader application and long-running workload coverage continues |
| Encoding and memory document core | 36 core checks plus 25 integration/encoding edge checks passed locally | External tools read disk; they do not see uncommitted memory revisions |
| CLI and MCP | Ten native tools; real stdio and official SDK interoperability verified | These interfaces are not yet a stable release contract |
| Linux | Planned | No Linux implementation or acceptance claim |
| Public packages | Planned | No installer or release artifact is advertised here |

These are local validation results, not performance benchmarks or a zero-error guarantee. See [quality gates](docs/quality-gates.md) for the separate acceptance boundaries.

## The workspace lifecycle

```text
workspace_open -> workspace_read / workspace_search
               -> workspace_edit (preview or memory change)
               -> workspace_commit -> external build / Git operation
               -> workspace_close
```

`workspace_edit` previews by default. With `dry_run=false`, it updates memory only. `workspace_commit` rereads the disk identity and SHA before replacing one file. A conflict leaves the disk file unchanged.

Responses distinguish `dirty_in_memory` from `written_to_disk`. Closing a dirty document requires `discard=true` or a successful commit. Ending a session discards uncommitted memory; it never performs an implicit commit.

The memory workspace belongs to one running session. MCP keeps that process alive. Separate `cdx --request` invocations do not share document IDs or uncommitted changes.

## Tool surface

| Tool | Purpose |
|---|---|
| `tool_info` | Version, platform and document-load/commit counters |
| `execute_program` | Literal argv execution with bounded output, timeout and owned-process cleanup |
| `workspace_open` | Load a file using an explicit encoding or strict BOM/UTF-8 detection |
| `workspace_read` | Read a bounded UTF-8 page from the memory revision |
| `workspace_edit` | Preview or apply an exact memory-only replacement with SHA/count preconditions |
| `workspace_convert_encoding` | Preview or perform lossless UTF-8/UTF-16/CP936 conversion in memory |
| `workspace_commit` | Validate the current disk revision and commit one document |
| `workspace_status` | Inspect memory revision and dirty state without reloading disk |
| `workspace_search` | Find literal UTF-8 text in the memory revision |
| `workspace_close` | Release a document, with explicit handling of dirty memory |

## Encoding is an explicit choice

`auto` recognizes a BOM or strict UTF-8. It does not guess that invalid UTF-8 must be GBK. For legacy Chinese source, choose `cp936` or `gbk` explicitly:

```json
{
  "operation": "workspace_open",
  "arguments": {
    "path": "D:\\project\\legacy.c",
    "encoding": "cp936"
  }
}
```

An unsupported or unrepresentable edit produces an encoding error. Cordonix does not drop bytes or replace characters to make a write succeed. `workspace_edit` preserves the current encoding. `workspace_convert_encoding` explicitly changes the memory encoding using a fresh SHA and `dry_run=false`; `workspace_commit` is still required to write disk. CP936 cannot represent Emoji: that conversion is rejected without changing memory or disk. AUTO is an interpretation policy, not proof of the historical encoding of ASCII-only data.

## Build and run

Clone [maikebing/Cordonix](https://github.com/maikebing/Cordonix) and use the repository's existing MSVC build entry points:

```text
eng\build.cmd
eng\test.cmd
build\bin\cdx.exe --version
build\bin\cdx.exe --request examples\request.json
```

Use an x64 MSVC developer prompt (Visual Studio 2022 or newer, with the C++ desktop workload), or let the script use the known Community/Enterprise installation locations. `/TC /std:c11 /W4 /WX /O2 /MT` builds a native Windows executable; the runtime has no Python/PowerShell dependency.

The CLI exposes these forms:

```text
cdx --version
cdx --request C:\absolute\request.json
cdx --mcp
```

`--request` reads one UTF-8 JSON operation and returns its result. `--mcp` runs the persistent stdio adapter. The executable is `build/bin/cdx.exe`. See [the Codex configuration example](examples/codex-config.toml); set its command to your absolute executable path, then start a new Codex session to load the server.

## Scope and limits

- Memory caching reduces repeated reads within Cordonix. It does not intercept every OS file access or make Git/compilers read the memory revision.
- A session holds at most 16 documents; each encoded file is at most 8 MiB, with additional memory for original bytes, edited bytes and the UTF-8 view. Read pages default to at most 32 KiB.
- A commit applies to one file. It is not a multi-file transaction.
- `workspace_status` reports the session's known state; commit performs the fresh external-change check.
- Process ownership is a lifecycle guarantee, not a security sandbox. An authorized executable retains its normal OS permissions.
- A timeout after an external side effect does not establish whether that side effect completed. Inspect the target state before repeating it.
- No speedup, disk-I/O reduction percentage or token-saving percentage is claimed without a measured baseline.

## Contribute

Start with [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md). Contributions should address one observed problem or roadmap item, preserve the current toolchain, and demonstrate behavior through real fixtures.

Licensed under [Apache-2.0](LICENSE).
