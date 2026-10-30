# Cordonix

**A native execution and memory workspace for AI coding agents.**

[![Windows checks](https://github.com/IoTSharp/Cordonix/actions/workflows/ci.yml/badge.svg)](https://github.com/IoTSharp/Cordonix/actions/workflows/ci.yml)

[简体中文](README.zh-CN.md) · [Roadmap](ROADMAP.md) · [Install/update](docs/install-update.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)

Cordonix, abbreviated **cdx**, gives agents a direct path to local programs and source files. Program arguments remain data. Text keeps its declared encoding. Repeated reads and edits can stay in memory until an explicit commit.

Install from a published Windows x64 ZIP or Linux x64 tar.gz using the [installation guide](docs/install-update.md) or the [copyable AI prompt](examples/cordonix-install-update-prompt.md). The installers verify SHA-256 and the actual executable version, use Gitee when GitHub is unavailable, and can enable hourly user updates. Published tags use `vMAJOR.MINOR.PATCH`; both [GitHub](https://github.com/IoTSharp/Cordonix/releases) and [Gitee](https://gitee.com/IoTSharp/Cordonix/releases) carry the same assets.

The implementation is **pure C11 for Windows and Linux**. Its executable does not need Python or PowerShell at runtime. Windows builds use MSVC; the Linux backend uses GCC, libc/iconv and pthreads. Cordonix is an independent open-source project, not an OpenAI product.

## Why it exists

AI coding workflows repeatedly encounter the same boundaries: shell quoting changes arguments, legacy encodings become replacement characters, stale edits overwrite newer content, and cancelled commands leave child processes running.

Cordonix makes those boundaries explicit through typed requests and observable outcomes:

- Start an executable with `program`, `args[]`, `cwd` and a timeout. Windows Job Objects or a Linux process group own the launched workload.
- Load source once, inspect and edit its memory revision, and commit only when requested.
- Preserve UTF-8, UTF-16, BOMs, line endings and explicitly selected CP936/GBK without silent lossy conversion.
- Check the expected SHA-256 and occurrence count before an edit; check the disk revision again before commit.
- Keep process exit codes, validation errors, encoding errors, conflicts and dirty memory state separate.

Git, ripgrep, compilers and Codex's `apply_patch` remain useful companions. Cordonix provides the execution and document boundary around them.

## Status

Cordonix is a development MVP. Windows has first-party Codex usage evidence; Linux has local native and MCP stdio validation in WSL Ubuntu 24.04.

| Area | Current evidence | Remaining boundary |
|---|---|---|
| Windows process core | 35 checks passed locally, including isolated child environments | Broader application and long-running workload coverage continues |
| Encoding and memory document core | 36 core checks plus 25 integration/encoding edge checks passed locally | External tools read disk; they do not see uncommitted memory revisions |
| CLI and MCP | Seventeen native tools; 96 Windows and 100 Linux real stdio checks; 12 current official Python SDK checks | Actual installed-client adoption and Codex UI title rendering have separate gates |
| New text files | 20 native checks on each platform plus eight shared MCP checks | Existing paths are never overwritten and parent directories must exist |
| Linux | 276 checks including full Git/rg adapters; independent MCP client 44/44 across ext4/tmpfs/9p and 60 rounds over 600 seconds | Native rg and Codex CLI are installed; model-backed Linux Codex calls still need authentication. Other distributions and longer soak are separate |
| Windows/Linux portable packages | ZIP/tar.gz, checksums, embedded build revision/dirty readback, official SPDX schema validation | Unsigned packages; build metadata is not a signed attestation. Published v0.0.1/v0.0.2 remain immutable |

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
| `search_text` | Parse JSON results from an explicitly supplied ripgrep executable |
| `git_status` | Parse Git porcelain v2 NUL records, including both rename paths |
| `create_file` | Create a new text file with strict encoding and no overwrite; parent directory must exist |
| `list_directory` | List immediate directory entries, with hidden-item and result-count controls |
| `resolve_path` | Normalize an existing absolute filesystem path and report its type |
| `verify_encoding` | Verify a bounded batch of disk files with a declared encoding, count and optional root constraint |
| `workspace_open` | Load a file using an explicit encoding or strict BOM/UTF-8 detection |
| `workspace_read` | Read the memory revision by byte/line range, last N lines or complete decoded text |
| `read_file` | Read a fresh disk snapshot by path and encoding, using a line range, tail or raw text |
| `workspace_edit` | Preview or apply an exact memory-only replacement with SHA/count preconditions |
| `workspace_convert_encoding` | Preview or perform lossless UTF-8/UTF-16/CP936 conversion in memory |
| `workspace_commit` | Validate the current disk revision and commit one document |
| `workspace_status` | Inspect memory revision and dirty state without reloading disk |
| `workspace_search` | Find literal UTF-8 text in the memory revision |
| `workspace_close` | Release a document, with explicit handling of dirty memory |

MCP tool definitions include a human-readable `title`, such as `执行本地程序`, `编辑文本（仅内存）` and `提交文件到磁盘`. Tool identifiers stay unchanged. The client decides whether to display these titles; the server cannot assign a dynamic title to each invocation. Cordonix executes local programs and does not infer that an arbitrary script is a remote method or a file creation operation.

## Execution boundaries

`execute_program` owns one bounded invocation. When its root process exits, Cordonix cleans up remaining owned processes even after exit code 0. Start persistent browsers, daemons and services through their existing lifecycle owner. `completed` confirms the command's exit status; it does not promise that a launched browser remains open. Results expose `active_processes_before_cleanup` and `active_processes_after_cleanup` alongside `cleanup_complete`.

`args` is a flat native argv array. PowerShell 7 `-File` does not reconstruct a named `[string[]]` script parameter from subsequent argv entries; construct that array inside a saved script or use a data interface the target script explicitly supports. `output_encoding` selects how Cordonix decodes captured bytes, without changing the child program. On `encoding_error`, inspect `process_status`/`exit_code` and the preserved Base64 bytes before any retry. See [execution boundaries and examples](docs/execution-boundaries.md).

## Child environment and external adapters

Optional `execute_program.env` is an object of child-only overrides, for example `{"HTTPS_PROXY":"http://127.0.0.1:7890","REMOVE_ME":null,"EMPTY":""}`. Other parent variables are inherited; the parent is not modified. Null removes a variable; an empty string keeps an empty value. At most 64 overrides are accepted. Names must be nonempty, contain no `=`, and fit 255 native characters/bytes; values fit 32767. Windows names are case insensitive; POSIX names are case sensitive. Invalid text and duplicate names under those rules fail before spawning.

`search_text` requires absolute `program` (rg), `cwd`, `patterns` (1–32) and absolute `roots` (1–8). Optional `globs` accepts up to 16 patterns; `literal` defaults true. Arguments use `--no-config --json`, a separate `-e` for every pattern and `--` before roots. Matches retain rg's `path`, `lines` and `submatches` as `{text}` or Base64 `{bytes}`. Exit 1 returns `no_match`. Default `encoding=none` preserves bytes; explicit UTF-8/UTF-16/GBK conversion is delegated to rg and may replace invalid input. Use `verify_encoding` for strict encoding verification.

`git_status` requires absolute `program` (Git) and `cwd`. It runs `--no-optional-locks status --porcelain=v2 --branch -z`, parses branch metadata and ordinary/renamed/unmerged/untracked entries, and keeps both rename paths. Output paths may contain spaces, quotes and newlines. Invalid UTF-8 output returns `encoding_error` with raw Base64, without guessing a replacement encoding. `untracked` is `normal` (default), `all` or `no`.

Both adapters accept `timeout_ms` (1–60000, default 30000), `output_limit_bytes` (1–262144) and `max_entries` (1–1000, default 200). Search also has `max_matches_per_file` (1–1000, default 50), disclosed in the result. Limits, cancellation and timeout do not become a complete successful result. Git and rg remain separately installed external programs.

## Create a new text file

The `create_file` operation writes one new file immediately. Supply an absolute literal `path`, a UTF-8 `content` string and optional `encoding` (default `utf-8`). Supported encodings are UTF-8, UTF-8 BOM, UTF-16LE/BE BOM and CP936/GBK; `auto` is rejected. Empty content is valid. NUL, invalid UTF-8 and characters the chosen encoding cannot represent are rejected before writing. The C API caps both input and encoded output at 8 MiB; CLI/MCP requests also obey the existing JSON frame limit.

```json
{"operation":"create_file","arguments":{"path":"D:\\project\\new.txt","content":"中文\r\n","encoding":"cp936"}}
```

The parent directory must already exist. An existing file, directory or link returns `already_exists`. The implementation flushes a same-directory temporary file and publishes it without replacing the destination. It preserves the supplied text and line endings, uses a new file's inherited Windows permissions or Linux mode `0600`, and does not open a memory workspace document.

Results report `written_to_disk`, `cleanup_complete`, encoded `bytes`, `encoding` and, on success, `disk_sha256`. Cancellation and a 15-second write deadline are checked between bounded I/O operations; a stalled synchronous OS call is not forcibly preempted. A failure after publication can retain `written_to_disk=true`; read back that path before retrying. The `document_commits` counter remains specific to memory document commits.

## Read a line range

`read_file` replaces a disk read followed by a skip/take pipeline. For example, skipping 950 lines and taking 245 means `start_line: 951`, `line_count: 245`. Run the saved [request example](examples/read-lines.json) with `build\bin\cdx.exe --request examples\read-lines.json`, adjusting the absolute path for your checkout. Set the known source encoding explicitly.

For an already-open document, pass the same line fields to `workspace_read` with its `document_id`; this reads the current memory revision, including uncommitted edits. `read_file` instead reads current disk bytes and closes its temporary document.

Line numbers are 1-based. LF, CRLF and CR delimiters are retained; an empty file has zero lines, and a final newline does not invent another line. Results include `lines_returned`, `next_line` (null at EOF), `eof`, encoding, EOL, SHA and `source`. `line_count` defaults to 200 (maximum 1000); `max_bytes` defaults to 32768 UTF-8 bytes (maximum 262144). The page stops at a whole-line boundary and reports `truncated=true` if the byte budget prevents returning the requested lines. A first line larger than the budget returns `line_too_long`; raise the bound or use existing workspace byte paging. Line fields cannot be mixed with `offset`/`limit`.

Reading the first N lines uses the same native range: `start_line: 1, line_count: N`. The [First 100 example](examples/read-first.json) reads `encoding_posix.c` directly with no shell pipeline.

| PowerShell read | Cordonix read fields |
|---|---|
| `Get-Content -First N` | `start_line: 1, line_count: N` |
| `Select-Object -Skip M -First N` after a file read | `start_line: M + 1, line_count: N` |
| `Get-Content -Tail N` | `tail_lines: N` |
| `Get-Content -Raw` | `raw: true` |

## Read the tail or complete text

Use `tail_lines: 55` to read the final 55 lines, or `raw: true` to read the entire decoded text. Both `read_file` and `workspace_read` support these modes. Saved CLI examples are [read-tail.json](examples/read-tail.json) and [read-raw.json](examples/read-raw.json); run each with `cdx --request` after adjusting its absolute path.

Tail accepts 1–1000 lines and returns the entire file if it has fewer lines. Raw has no line-count cap. Both preserve the original line delimiters and final newline; encoding BOMs are represented by metadata, rather than included in decoded text. Success reports `mode`, actual `start_line`, `lines_returned`, `eof=true`, `next_line=null` and `truncated=false`.

The UTF-8 output budget remains `max_bytes` (default 32768, maximum 262144). Tail/raw require their complete result to fit. Otherwise they return `output_limit` with `bytes_required` and `max_bytes`, without a partial `text` value. Increase the bound or use range paging. Do not combine tail/raw with `start_line`, `line_count`, or byte `offset`/`limit`; `raw=false` keeps line-range semantics.

## List a directory or resolve a path

`list_directory` accepts an absolute literal `path`, `include_hidden` (default false), and `max_entries` (default 200, range 1–1000). Setting `include_hidden: true` includes Windows HIDDEN/SYSTEM entries or POSIX dotfiles, corresponding to the filesystem use of `Get-ChildItem -Force`. It returns immediate entries with `name`, `type`, ordinary-file `size_bytes` (otherwise null), `hidden` and `system`. Enumeration is nonrecursive and order is unspecified. Extra visible entries set `truncated=true`; an empty directory succeeds with an empty array.

`resolve_path` normalizes separators and dot segments in an absolute path, verifies that the directory entry exists, and returns `path` and `type`. Link names are preserved. Root links to directories can be listed; child links are not traversed. Windows reparse entries and POSIX symbolic links are labeled `link`. These tools operate on filesystem paths, without wildcard expansion or PowerShell providers.

Missing paths return `not_found`, distinct from empty directories; permission, encoding, I/O, timeout and cancellation remain separate statuses. Enumeration has at most 100,000 steps and a 5-second deadline checked between synchronous OS calls. The examples [list-directory.json](examples/list-directory.json) and [resolve-path.json](examples/resolve-path.json) work through `cdx --request` as well as MCP.

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

## Verify an encoding batch

`verify_encoding` checks fresh disk bytes using the existing document core: strict decode, no text NUL, and byte-exact decode/encode round-trip. Supply `paths` (1–32 absolute path entries) and an explicit `encoding`; `auto` is rejected. Optional `expected_count` must equal the number of supplied entries, including duplicates. Optional `allowed_root` constrains normalized paths at or below that lexical root; it does not constrain symlink targets. All path/count/scope preconditions are checked before file contents are read.

For a seven-file CP936 workflow, set `encoding: "cp936"`, `expected_count: 7`, the workflow directory as `allowed_root`, and pass the caller's seven paths. The [runnable request example](examples/verify-encoding.json) uses one UTF-8 repository file and can be adapted to the required batch.

An incompatible BOM reports `encoding_mismatch`; declared UTF-8 also accepts UTF-8 BOM. Passing proves compatibility with the declared encoding, not the file's historical encoding. Each attempted entry reports status and PASS/FAIL, with bytes, SHA and actual encoding when document loading succeeded. The summary reports `files`, `checked`, `passed`, `failed`, `not_run`, and `written_to_disk: false`. Decode/NUL/round-trip summaries are PASS only for a fully successful batch; otherwise they remain UNKNOWN rather than inventing a sub-check failure.

The default deadline is 10000 ms (allowed 1–60000), checked between file operations. MCP cancellation and EOF use the existing active worker; remaining entries retain NOT_RUN. A stalled OS file operation is not forcibly preempted. Verification prints no source text, performs no edits/commits, and consumes no persistent document slots.

## Build and run

Clone [IoTSharp/Cordonix](https://github.com/IoTSharp/Cordonix) and use the repository's existing MSVC build entry points:

```text
eng\build.cmd
eng\test.cmd
build\bin\cdx.exe --version
build\bin\cdx.exe --request examples\request.json
```

Use an x64 MSVC developer prompt (Visual Studio 2022 or newer, with the C++ desktop workload), or let the script use the known Community/Enterprise installation locations. `/TC /std:c11 /W4 /WX /O2 /MT` builds a native Windows executable; the runtime has no Python/PowerShell dependency.

On Linux with GCC, libc development headers and GNU `timeout`:

```sh
sh eng/build-linux.sh
sh eng/test-linux.sh
build/linux/bin/cdx --version
```

Both platforms use the same JSON requests and seventeen MCP tools. Linux paths are absolute UTF-8 paths, and execution errors report `posix_error`; Windows keeps `win32_error`. The Linux executable is dynamically linked to system libraries. Build a local Windows ZIP with PowerShell 7 using `./eng/test-package.ps1` after `eng\test.cmd`; see [packaging](docs/packaging.md).

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
- Linux cleanup covers the created process group and checks `/proc` within explicit bounds. Programs that deliberately leave that group are outside this guarantee; incomplete observation reports `cleanup_complete=false`.
- Linux document commits support regular files with one hard link and reject symlinks. They preserve owner/group/mode, while write time changes. ACLs and extended attributes are not preserved by this MVP; do not use it to edit files that require those metadata guarantees.
- A timeout after an external side effect does not establish whether that side effect completed. Inspect the target state before repeating it.
- No speedup, disk-I/O reduction percentage or token-saving percentage is claimed without a measured baseline.

## Contribute

Start with [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md). Contributions should address one observed problem or roadmap item, preserve the current toolchain, and demonstrate behavior through real fixtures.

Licensed under [Apache-2.0](LICENSE).
