# Cordonix

Development adds `vnc_screenshot` for operations: remote IP, default port 5900,
and a new PNG path. Try no password first; when authentication is required,
reuse the current user's saved credential or show a masked password dialog.
Supplied passwords are saved only after successful authentication. See the
[VNC screenshot contract](docs/native-vnc.md). The development MCP surface has
35 tools; this does not update an existing installed connection.

Version 0.3.0 adds `stat` and `find_program` to the existing data, discovery and file-operation tools, bringing the MCP surface to 34 tools. Start with the [tool selection and client discovery guide](docs/mcp-tool-guide.md), then the [batch scope and validation](docs/capability-gaps-20261106.zh-CN.md), [data query](docs/data-query.md), [file metadata](docs/file-stat.md), [program discovery](docs/program-discovery.md), [file discovery](docs/file-discovery.md) and [regular-file operations](docs/file-operations.md).

Windows screenshot support provides native PNG capture for desktop, region, window and foreground targets, plus bounded window discovery. See [capture methods and outcomes](docs/native-screenshot.md). Existing installed connections keep their original tool schema until a new runtime is loaded.

The published 0.3.1 patch repairs unset PATH with explicit candidates, Windows pre-1970 timestamps, POSIX symlink/`..` metadata paths, directory hidden-name flags and virtual-environment interpreter discovery. It also retains discovery interruption/errors and rejects legacy Windows executable formats. Validation and publication states are recorded separately in [HANDOFF.md](HANDOFF.md).

The [three-stage roadmap](ROADMAP.md#active-three-stage-program-2026-11-05) and [implementation/acceptance plan](docs/implementation-plan-2026-11-05.zh-CN.md) track local deliveries and later work on task execution, coding/data workflows, and design/office delivery. Remaining stages require their own evidence and acceptance.

Version 0.2.0 also includes native `create_directory` and bounded sequential `read_files` on Windows/Linux. HTTP adds optional `representation=both|text|base64`, retaining `both` by default and preserving raw Base64 when text decoding fails. See [batch read limits](docs/batch-read.md), [HTTP representation](docs/native-http.md) and the [MCP compatibility audit](docs/mcp-compatibility.md). Release publication and installed-client adoption have separate evidence in HANDOFF.md.

The current MCP adapters also reject malformed or unknown `tools/call` requests with JSON-RPC `-32602`. Deterministic operation failures expose outer `isError: true` while keeping the original status JSON; partial, preview, no-match, unsupported-platform and unknown-outcome results remain text results. This compatibility slice and the 34-tool surface are covered by real Windows/Linux MCP fixtures, including asynchronous program-discovery cancellation. Current counts and separate installed-client gates are recorded in HANDOFF.md.

Release 0.1.0 adds native SSH, HTTP, SFTP, TCP/Telnet/MQTT and serial access, automatic local proxy evaluation and per-call summaries: 24 tools total, retaining all 19 v0.0.5 tools and their published fields. Windows/Linux native and MCP gates, real protocol fixtures and portable packages pass; signed tagged publication and public readback are recorded in [HANDOFF.md](HANDOFF.md).

Version 0.0.5 adds Windows process queries, experimental native phone access and Microsoft Store/portable PowerShell 7 compatibility. Its release and natural installed-update checks are recorded separately in HANDOFF.md; the completed v0.0.4 acceptance below is historical evidence.

On 2026-11-02 this machine's existing hourly task naturally upgraded 0.0.4 to 0.0.5 through Gitee. A fresh MCP connection through the stable launcher exposes all 19 tools and the clean release revision; existing connections retain their earlier executable.

WinGet distribution is being submitted as `IoTSharp.Cordonix`. See [the WinGet guide](docs/winget.md) for catalog availability, installation boundaries and automatic release PRs.

Verified release: [GitHub v0.4.1](https://github.com/IoTSharp/Cordonix/releases/tag/v0.4.1) / [Gitee v0.4.1](https://gitee.com/IoTSharp/Cordonix/releases/tag/v0.4.1), from clean source `d8a67cf`. It adds daily background update checks, complete MCP tool guidance and precise `create_file` argument diagnostics. All 12 asset pairs match across both hosts; original archives pass exact-tag signing verification and tampered copies fail. See the [release notes](docs/release-notes.md) and [acceptance receipts](HANDOFF.md). The following 0.3.1 record is the prior verified baseline.

All 34 MCP tools and their 244 named input properties have usage descriptions. The [tool guide](docs/mcp-tool-guide.md) explains selection, defaults, conditional fields and memory/disk boundaries; the real MCP suite checks description coverage before release.

Verified release: [v0.3.1 on GitHub](https://github.com/IoTSharp/Cordonix/releases/tag/v0.3.1) and [Gitee](https://gitee.com/IoTSharp/Cordonix/releases/tag/v0.3.1), from clean source `0c50aab`. All 12 uploaded assets match across both hosts; four original archives pass exact-tag OIDC verification and four tampered copies fail. Downloaded Windows/Linux binaries expose all 34 MCP tools and pass 165/166 real protocol checks respectively, plus the patch regressions. Current evidence is in [HANDOFF.md](HANDOFF.md); installed-client adoption remains separate, and v0.3.0/v0.2.0 assets are preserved. WinGet submissions remain subject to Microsoft's separate validation and review.


**A native execution and memory workspace for AI coding agents.**

[![Windows checks](https://github.com/IoTSharp/Cordonix/actions/workflows/ci.yml/badge.svg)](https://github.com/IoTSharp/Cordonix/actions/workflows/ci.yml)

[简体中文](README.zh-CN.md) · [Roadmap](ROADMAP.md) · [Install/update](docs/install-update.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)

Cordonix, abbreviated **cdx**, gives agents a direct path to local programs and source files. Program arguments remain data. Text keeps its declared encoding. Repeated reads and edits can stay in memory until an explicit commit.

Install from a published Windows x64 ZIP or Linux x64 tar.gz using the [installation guide](docs/install-update.md) or the [copyable AI prompt](examples/cordonix-install-update-prompt.md). The installers verify SHA-256 and the actual executable version and use Gitee when GitHub is unavailable. Version 0.4.1 enables a separate background `cdx --checkupgrade` instance on the first launch each day, preserves explicit opt-out and records results. The updated launcher selects the native command in 0.4.1; older 0.3.1 binaries retain the updater-script compatibility path. Windows/Linux package gates exercise the native command. Existing installations need one migration to the new launcher. Published tags use `vMAJOR.MINOR.PATCH`; both [GitHub](https://github.com/IoTSharp/Cordonix/releases) and [Gitee](https://gitee.com/IoTSharp/Cordonix/releases) carry the same assets.

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
| CLI and MCP | Thirty-four development tools, including `stat` and `find_program`; integrated validation is recorded in HANDOFF.md | Installed-client adoption, protocol migration and Codex UI rendering have separate gates |
| Native SSH client | Windows loopback protocol 27/27; repeated on an AddressSanitizer client; static libssh2 with CNG | Deployed bastions NOT_RUN; Linux SSH NOT_SUPPORTED; current backend excludes Ed25519 |
| New text files | 20 native checks on each platform plus eight shared MCP checks | Existing paths are never overwritten and parent directories must exist |
| Linux | Earlier CI evidence includes 276 checks with full Git/rg adapters; the October 31 WSL rerun passed 102 MCP checks and 17/17 Git adapter checks while marking rg `NOT_RUN` because native rg is absent | Model-backed Linux Codex calls still need authentication. Other distributions and longer soak are separate |
| Windows/Linux portable packages | v0.3.1 ZIP/tar.gz, checksums, clean build revision, official SPDX validation and independently verified OIDC provenance | Windows Authenticode and broader target environments retain separate gates; published tags/assets remain immutable |

These are local validation results, not performance benchmarks or a zero-error guarantee. See [quality gates](docs/quality-gates.md) for the separate acceptance boundaries.

## What the local Codex session audit changed

On October 31, 2026, the local Codex history contained 109 sessions. Their recorded tool activity included 11,568 external command calls, 983 failure records, 192 explicit `.ps1` file changes and 179 explicit `.py` file changes. The repeated work was process and browser/task observation, JSON state readback, Git/ripgrep checks and bounded file batches. These counts identify repetition; they do not prove that every script should be replaced.

The first adopted gap is `query_processes`. It is a read-only Windows C11 operation that returns a bounded process snapshot with PID, parent PID, executable path, best-effort original command line and exact creation `FILETIME`. It keeps inaccessible metadata, cancellation, timeout, output limits and partial enumeration explicit. Linux returns `not_supported` and never falls back to a shell, Python or PowerShell script. The operation is included in v0.0.5; load the updated runtime after installation to use its MCP surface.

## Native phone access (experimental)

The new `phone` tool implements Windows Bluetooth MAP SMS, PBAP contact/recent-call pages and HFP call control in C11, using Windows system libraries. It includes bounded waits for inbox changes, incoming calls and Apple Notification Center Service (ANCS) application events, snapshot-checked SMS/verification-code reads, and preview-by-default send/dial/answer/hangup operations. It does not require the earlier .NET phone bridge. Linux device operations return `not_supported`.

The 2026-11-01 follow-up opens a bounded native `GattSession` before ANCS discovery. An earlier check returned `connection_failed` (`0x8007048F`) at `ancs_service_discovery`, recording a Windows BLE link failure rather than a permission verdict. The retry acceptance below restored access without changing the pairing. A previous fresh-pairing run did read a new WeChat event; see the dated acceptance record in [the phone contract](docs/phone-native.md).

ANCS initial service discovery now retries an unreachable link up to five times after the first attempt, with a cancellable 500 ms interval and one shared request deadline. Results expose attempt/retry counts and whether the retry limit was exhausted. The GUI allows up to 60 seconds for the complete notification request and displays the counts.

The subsequent 2026-11-01 live check succeeded on attempt 2 after one retry, subscribed to both ANCS characteristics and read three existing Apple Phone notifications. No new WeChat event was observed during that request.

After the user removed and recreated the pairing, ANCS subscription and content retrieval succeeded for existing Apple Phone notifications. The earlier `0x8007048F` service failure was not reproduced in that GUI run. The receiver now collects up to `max_entries` events under one deadline without resubscribing, discards the oldest replayed events when the bounded source queue is full, reserves a result slot for a new event and preserves completed events on a later failure. A live GUI run captured a new WeChat event (`com.tencent.xin`, `preexisting=false`) after discarding 511 preexisting events. The GUI waits up to 60 seconds or 20 events and labels existing/new/updated/removed notifications.

On an already paired iPhone, one authorized SMS and short outgoing call were confirmed received by the user; call state was read back as active and then idle after hangup. A later PBAP page returned one contact with a telephone field, and MAP returned five metadata entries. Incoming events, answering, private message/code reads and computer telephone audio remain unverified or unsupported. The Windows `--phone-ui` window now supports adapter display, discovery, topmost human pairing confirmation, deleting pairing and an owner-scoped HFP connection with a lease/status display. Actual Windows pairing removal and foreground confirmation passed. After switching pairing to the Windows device-pairing C ABI, the second iPhone paired successfully and retained an HFP connection; the user confirmed the computer appeared connected on the phone. The resizable native window now uses Windows visual styles, the system font, per-monitor DPI, application light/dark mode and high-contrast colors. It separates pairing from five actual access checks (call state, SMS, contacts, recent calls and ANCS application notifications), includes iPhone authorization guidance and paged readable previews, and offers a cancellable 30-second SMS-list wait. Access checks do not read or change iOS permission switches; ANCS application notifications are current-window events and do not provide history. See [the phone contract and acceptance matrix](docs/phone-native.md).

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
| `ssh` | Native C SSH direct, direct-tcpip jump and configurable interactive bastion execution; Windows CNG backend |
| `http` | Native HTTP/HTTPS requests with WinHTTP, typed headers/body/proxy and bounded asynchronous cancellation |
| `sftp` | Native libssh2 stat, bounded binary read and exclusive file creation over direct/jump SSH |
| `network` | Bounded TCP/Telnet exchange and MQTT 3.1.1 CONNECT/QoS0 PUBLISH |
| `serial` | Passive COM enumeration/diagnosis and explicitly requested active probes or byte exchange |
| `phone` | Experimental native Windows pairing/removal, retained connection, SMS, contact pages, call control and bounded waits |
| `execute_program` | Literal argv execution with bounded output, timeout and owned-process cleanup |
| `query_processes` | Read-only Windows PID/parent/creation-time/original-command-line snapshots; Linux reports not_supported |
| `search_text` | Parse JSON results from an explicitly supplied ripgrep executable |
| `find_files` | Sort and page a complete bounded ripgrep file scan |
| `data_query` | Project, filter, count and group JSON, JSONL or CSV from strict file decoding or inline text |
| `hash_file` | Stream SHA-256 over exact binary file bytes |
| `copy_file` | Copy a regular file to a new destination without replacement |
| `move_file` | Atomically rename a regular file within a supported filesystem without replacement |
| `git_status` | Parse Git porcelain v2 NUL records, including both rename paths |
| `create_file` | Create a new text file with strict encoding and no overwrite; parent directory must exist |
| `create_directory` | Create a directory and return its absolute path; explicitly opt into missing parents and existing directories |
| `list_directory` | List immediate directory entries, with hidden-item and result-count controls |
| `resolve_path` | Normalize an existing absolute filesystem path and report its type |
| `stat` | Read one absolute path's type, size, mode, flags and timestamps without mutation |
| `find_program` | Resolve bounded absolute executable candidates or PATH entries without executing them |
| `verify_encoding` | Verify a bounded batch of disk files with a declared encoding, count and optional root constraint |
| `workspace_open` | Load a file using an explicit encoding or strict BOM/UTF-8 detection |
| `workspace_read` | Read the memory revision by byte/line range, last N lines or complete decoded text |
| `read_file` | Read a fresh disk snapshot by path and encoding, using a line range, tail or raw text |
| `read_files` | Read 1–32 disk files under one deadline and explicit text/output budgets, preserving individual outcomes |
| `workspace_edit` | Preview or apply an exact memory-only replacement with SHA/count preconditions |
| `workspace_convert_encoding` | Preview or perform lossless UTF-8/UTF-16/CP936 conversion in memory |
| `workspace_commit` | Validate the current disk revision and commit one document |
| `workspace_status` | Inspect memory revision and dirty state without reloading disk |
| `workspace_search` | Find literal UTF-8 text in the memory revision |
| `workspace_close` | Release a document, with explicit handling of dirty memory |

MCP tool definitions include a human-readable `title`, such as `执行本地程序`, `编辑文本（仅内存）` and `提交文件到磁盘`. Tool identifiers stay unchanged. Titles are fixed; Cordonix separately derives a short summary from each call's existing arguments, such as `git.exe：查看仓库修改状态 · Cordonix` or `读取文件 · Services/DingTalkOptions.cs · 第 61～90 行`.

The summary is returned in result `_meta["cordonix/summary"]` and sent before execution as a standard progress message when the client supplies `_meta.progressToken`. Formatting is bounded C11 code with no model request, new input field, file inspection or repeated summary in model-visible `content`. The client chooses whether to show it in collapsed rows; desktop adoption remains a separate gate. See [call summaries and the client contract](docs/call-summaries.md).

## Execution boundaries

Native communication tools use Windows system APIs plus the existing static libssh2 dependency. See the [protocol assessment](docs/communication-protocols.md) for exact scope. MQTT currently excludes TLS/authentication/subscriptions; physical UART acceptance remains separate. FTP/FTPS is evaluated and deferred pending a concrete compatibility requirement. New communication operations explicitly return `not_supported` on Linux.

Network tools now default to `proxy="auto"`: direct network connection first, then bounded probing of local ports 7890/1080 with actual HTTP CONNECT/SOCKS5 verification after a connection failure. `proxy="direct"` opts out; an explicit proxy selects one route. Target authentication/TLS/host-key errors, HTTP status errors and uncertain dispatched operations do not trigger replay. See [local proxy routing](docs/local-proxy.md). COM device access remains local.

The development `ssh` tool embeds a C SSH client with static libssh2 and Windows CNG. It supports direct IP connections, a second SSH session through a bastion's direct-tcpip channel, and configurable PTY target selection. Authentication uses Windows credential aliases or supported private keys. See [native SSH](docs/native-ssh.md) for host-key pinning, remote-outcome semantics, examples and the Windows/platform validation boundary. This capability is separate from the installed release cited above.

`execute_program` owns one bounded invocation. When its root process exits, Cordonix cleans up remaining owned processes even after exit code 0. Start persistent browsers, daemons and services through their existing lifecycle owner. `completed` confirms the command's exit status; it does not promise that a launched browser remains open. Results expose `active_processes_before_cleanup` and `active_processes_after_cleanup` alongside `cleanup_complete`.

`args` is a flat native argv array. PowerShell 7 `-File` does not reconstruct a named `[string[]]` script parameter from subsequent argv entries; construct that array inside a saved script or use a data interface the target script explicitly supports. `output_encoding` selects how Cordonix decodes captured bytes, without changing the child program. On `encoding_error`, inspect `process_status`/`exit_code` and the preserved Base64 bytes before any retry. See [execution boundaries and examples](docs/execution-boundaries.md).

Windows saved PowerShell scripts can use the installed PowerShell 7 `pwsh.exe` at any absolute path, including Microsoft Store and portable installations. Use `-NoProfile -File` with an existing absolute script path; command strings, encoded commands and Windows PowerShell 5.1 remain rejected. The caller selects the PowerShell version. This compatibility fix is included in v0.0.5; v0.0.4 restricts the executable to the MSI path.

Check `tool_info` on the actual connection after updating: existing MCP processes keep their earlier version. The child `timeout_ms` (up to 30 minutes) is separate from the client's tool-call deadline. For analyses longer than that deadline, use a pollable terminal session or configure the client deadline before starting. A transport timeout without a result leaves the outcome unknown; inspect original processes and artifacts before retrying. See the [failure diagnosis and long-task guidance](docs/execution-boundaries.md#child-deadlines-and-mcp-client-deadlines).

`query_processes` uses Windows process enumeration and native process metadata APIs. Its `name` and `command_contains` filters are literal ASCII case-insensitive comparisons. Protected or already-exited processes can have `null` path, command line or creation time and contribute to `partial`/`inaccessible` results. The original command line is a best-effort observation through the Windows process environment block; it is not a security boundary and is not implemented on the POSIX backend. `max_entries`, `timeout_ms` and `output_limit_bytes` are bounded, and a capped result is reported as `output_limit` with `truncated=true`.

## Child environment and external adapters

Optional `execute_program.env` is an object of child-only overrides, for example `{"HTTPS_PROXY":"http://127.0.0.1:7890","REMOVE_ME":null,"EMPTY":""}`. Other parent variables are inherited; the parent is not modified. Null removes a variable; an empty string keeps an empty value. At most 64 overrides are accepted. Names must be nonempty, contain no `=`, and fit 255 native characters/bytes; values fit 32767. Windows names are case insensitive; POSIX names are case sensitive. Invalid text and duplicate names under those rules fail before spawning.

`search_text` requires absolute `program` (rg), `cwd`, `patterns` (1–32) and absolute `roots` (1–8). Optional `globs` accepts up to 16 patterns; `literal` defaults true. Arguments use `--no-config --json`, a separate `-e` for every pattern and `--` before roots. Matches retain rg's `path`, `lines` and `submatches` as `{text}` or Base64 `{bytes}`. Exit 1 returns `no_match`. Default `encoding=none` preserves bytes; explicit UTF-8/UTF-16/GBK conversion is delegated to rg and may replace invalid input. Use `verify_encoding` for strict encoding verification.

`git_status` requires absolute `program` (Git) and `cwd`. It runs `--no-optional-locks status --porcelain=v2 --branch -z`, parses branch metadata and ordinary/renamed/unmerged/untracked entries, and keeps both rename paths. Output paths may contain spaces, quotes and newlines. Invalid UTF-8 output returns `encoding_error` with raw Base64, without guessing a replacement encoding. `untracked` is `normal` (default), `all` or `no`.

Both adapters accept `timeout_ms` (1–60000, default 30000), `output_limit_bytes` (1–262144) and `max_entries` (1–1000, default 200). Search also has `max_matches_per_file` (1–1000, default 50), disclosed in the result. Limits, cancellation and timeout do not become a complete successful result. Git and rg remain separately installed external programs.

## Read-only Windows process queries

The CLI/MCP `query_processes` operation preserves the v0.0.5 contract. Optional `name` filters an executable basename and `command_contains` filters a literal command-line substring; both compare ASCII without case sensitivity.

```json
{"command_contains":"Publish-QueuedBlogs","max_entries":10,"timeout_ms":5000}
```

Results return `entries` with PID, parent PID, executable name/path, best-effort original command line and exact numeric `creation_filetime`. JSON consumers must preserve that integer's original decimal text or use an integer-aware parser; a JavaScript Number can round FILETIME values. Protected or exited processes can return null metadata. Read `status`, `inaccessible`, `skipped` and `truncated`; incomplete empty results remain UNKNOWN. This observation does not authorize process termination. Linux returns `not_supported` without an interpreter fallback.

## Create a new text file

The `create_file` operation writes one new file immediately. Supply an absolute literal `path`, a UTF-8 `content` string and optional `encoding` (default `utf-8`). Supported encodings are UTF-8, UTF-8 BOM, UTF-16LE/BE BOM and CP936/GBK; `auto` is rejected. Empty content is valid. NUL, invalid UTF-8 and characters the chosen encoding cannot represent are rejected before writing. The C API caps both input and encoded output at 8 MiB; CLI/MCP requests also obey the existing JSON frame limit.

The input field is `content`; the `text` field returned by reading tools is not an alias. Missing or mistyped `path`, `content` and `encoding` report the affected parameter before writing. A request with `text` instead of `content` explicitly asks the caller to rename it. Only an actual `encoding: "auto"` request receives the auto-encoding diagnostic.

```json
{"operation":"create_file","arguments":{"path":"D:\\project\\new.txt","content":"中文\r\n","encoding":"cp936"}}
```

The parent directory must already exist. An existing file, directory or link returns `already_exists`. The implementation flushes a same-directory temporary file and publishes it without replacing the destination. It preserves the supplied text and line endings, uses a new file's inherited Windows permissions or Linux mode `0600`, and does not open a memory workspace document.

Results report `written_to_disk`, `cleanup_complete`, encoded `bytes`, `encoding` and, on success, `disk_sha256`. Cancellation and a 15-second write deadline are checked between bounded I/O operations; a stalled synchronous OS call is not forcibly preempted. A failure after publication can retain `written_to_disk=true`; read back that path before retrying. The `document_commits` counter remains specific to memory document commits.

## Read a line range

`read_file` replaces a disk read followed by a skip/take pipeline. For example, skipping 950 lines and taking 245 means `start_line: 951`, `line_count: 245`. Run the saved [request example](examples/read-lines.json) with `build\bin\cdx.exe --request examples\read-lines.json`, adjusting the absolute path for your checkout. Set the known source encoding explicitly.

For `Get-Content -LiteralPath 'D:/project/file.cs' | Select-Object -Skip 1990 -First 67`, call the MCP `read_file` tool with these arguments:

```json
{"path":"D:/project/file.cs","encoding":"utf-8","start_line":1991,"line_count":67}
```

This reads lines 1991 through 2057. The same mapping gives `start_line: 61, line_count: 30` for `-Skip 60 -First 30`, and `start_line: 291, line_count: 27` for `-Skip 290 -First 27`. Supply the literal file path in `path`; the tool accepts structured fields. For CLI use, wrap those arguments as `{"operation":"read_file","arguments":{...}}` in a JSON request file and pass it to `cdx --request`.

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

## Create a directory

`create_directory` accepts an absolute literal `path`. `parents: true` creates missing parents; `exist_ok: true` accepts an existing directory. Both default to false. A same-name file returns `not_directory`; an existing directory without `exist_ok` returns `already_exists`. It uses native Windows/POSIX filesystem APIs without starting PowerShell or another shell.

This single request ensures the directory exists and returns its normalized absolute `path`, replacing directory creation followed by `Get-Item | Select-Object -ExpandProperty FullName`:

```json
{
  "operation": "create_directory",
  "arguments": {
    "path": "D:\\work\\center-query",
    "parents": true,
    "exist_ok": true
  }
}
```

On success, `type` is `directory`. `created` reports whether this call created the requested leaf; `directories_created` counts all directories it created. An accepted existing directory returns `created: false` and count zero. These mutation fields are retained on filesystem errors, cancellation and timeout; created parents remain on disk. Reconcile them before retrying a failed call. Creation is limited to 256 missing directories, with no writes if preflight exceeds that bound. `timeout_ms` defaults to 5000, range 1–60000; the deadline and cancellation are checked between synchronous OS calls, without hard preemption of a stalled filesystem driver. Windows inherits normal directory security; POSIX uses mode 0777 subject to umask. Existing directory links follow platform behavior; lexical normalization does not provide physical containment.

This operation is available in local development builds and is not included in published v0.1.0 or the retained 0.0.5 MCP connection. An updated installed runtime and a fresh client tool list are separate release/adoption gates.

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

Both platforms expose the same eighteen MCP tools and JSON contracts; the Windows-only process query reports `not_supported` on Linux. Linux paths are absolute UTF-8 paths, and execution errors report `posix_error`; Windows keeps `win32_error`. The Linux executable is dynamically linked to system libraries. Build a local Windows ZIP with PowerShell 7 using `./eng/test-package.ps1` after `eng\test.cmd`; see [packaging](docs/packaging.md).

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
