# Development and first-party use

Build on Windows with the x64 MSVC C toolchain (`eng\build.cmd`). Run `eng\test.cmd` before submitting a change. The same command is used by GitHub Actions; it builds the application and exercises real native fixtures. The executable is `build/bin/cdx.exe`.

The runtime has no Python, Node or PowerShell dependency. Windows uses the existing Visual Studio build scripts and system libraries. Linux uses GCC, libc/iconv and pthreads; build with `sh eng/build-linux.sh`, then run `sh eng/test-linux.sh`. Linux output is `build/linux/bin/cdx`. The public C sources are maintained source files, not generated artifacts.

## Codex connection

Copy `examples/codex-config.toml` into your Codex MCP configuration and replace the command with your actual absolute `cdx.exe` path. `codex mcp get cordonix` confirms configuration, but a tool call in a new Codex session is a separate check. The tested native MCP protocol version is `2025-06-18`.

Use a small disposable source file for the first workflow:

1. `workspace_open` with the known encoding.
2. `workspace_read`, then `workspace_edit` with the returned memory SHA and `dry_run=true`.
3. Repeat the edit with `dry_run=false`; verify disk is unchanged.
4. Optionally use `workspace_convert_encoding` with a new target encoding and the current SHA.
5. `workspace_commit`, then independently inspect the disk content.
6. Start a native program with `execute_program`. External programs see the committed disk revision.
7. `workspace_close` when done. Uncommitted documents require explicit discard.

This procedure is also the maintenance team's adoption gate. Production sources are not migration test fixtures. The previous runtime can remain installed while the Cordonix session is verified.

## Evidence and limits

The current Windows gate passes 225 native checks (15 process, 36 document, 20 create-file, 46 JSON, 25 document/protocol edges, 83 stdio). The Linux gate passes 211 (16 process, 42 document, 20 create-file, 46 JSON, 87 stdio). Create-file checks also passed MSVC static analysis and Linux ASan/UBSan/leak detection. A separate `sh eng/test-filesystem-linux.sh "$PWD/build/linux/bin/cdx"` gate passes 14 checks on actual ext4 and reports NOT_RUN/77 on other filesystems; it requires the existing Python 3 test runtime and `findmnt`. The earlier Windows baseline's 14 official-SDK workflows and fresh Codex usage are separate historical evidence; rerun independent client acceptance for the changed executable.

Logical document loads and commits are counted in memory. They do not measure physical disk traffic: the OS has its own cache, commit validation rereads disk, and external tools perform their own IO. Measure physical IO, time and token usage before publishing percentage improvements.

The initial CLI request mode handles one operation. Persistent documents require the MCP process. A session stops at stdin EOF, 100,000 messages or 24 hours; uncommitted memory is discarded. There is no recovery journal. File commits are single-file replacements, with a documented final comparison/replacement interval for noncooperating external writers. This does not claim an OS sandbox or a filesystem transaction.

## Source layout

- `include/cdx`: public C contracts.
- `src/cdx-core`: Windows/Linux process lifecycles and structured results.
- `src/cdx-workspace`: encoding, source revisions and explicit commits.
- `src/cdx-protocol`: JSON and operation dispatch.
- `src/cdx-cli`: one-shot CLI and persistent MCP stdio transport.
- `tests`: native observable-behavior tests.
- `eng`: platform build/test scripts and Windows portable packaging.

## Tool labels and gradual migration

MCP `tools/list` includes a human-readable `title` for every tool, following the [MCP tools specification](https://modelcontextprotocol.io/specification/2025-06-18/server/tools). The interface keeps stable machine identifiers and does not claim dynamic per-call titles. [Progress messages](https://modelcontextprotocol.io/specification/2025-06-18/basic/utilities/progress) are optional client-requested protocol notifications; Cordonix does not currently emit them.

Prefer cdx for literal native execution and existing encoding-aware document edits. A saved PowerShell 7 script may be passed using `-File` and a literal argument array on Windows. Choose encoding before opening: known CP936/GBK must be explicit, and edits preserve BOM/EOL/encoding unless a conversion is requested. The unreleased `create_file` directly creates new text files with declared encoding and no overwrite. Keep predecessors where installed clients lack that operation, and for the remaining environment overrides and parsed rg/Git gaps. Generic execution of a script is not proof that its work is a remote API call or a native file creation.

Windows portable packaging is described in [packaging.md](packaging.md). Its provenance records the checkout at packaging time and deliberately keeps binary build provenance UNKNOWN; identical ZIP inputs do not establish compiler reproducibility.

For a one-off file slice, use `read_file` with an absolute `path`, known `encoding`, `start_line` and `line_count`; it does not consume a persistent workspace slot. The CLI accepts the same operation via `--request` and emits UTF-8 JSON directly. `workspace_read` accepts the same line fields for an open memory revision; byte mode remains available when line fields are omitted. See the README and `examples/read-lines.json` for skip/take migration.

For `Get-Content -Tail N`, select `tail_lines: N`; for `Get-Content -Raw`, select `raw: true`. These are mutually exclusive with line/byte range fields. Both disk and memory tools require a complete tail/raw result within `max_bytes`; `output_limit` reports the required size and does not include partial text. The saved `read-tail.json` and `read-raw.json` examples exercise the production CLI.

`Get-Content -First N` is the existing line-range operation with `start_line: 1` and `line_count: N`; no additional read mode is required. The `read-first.json` recipe documents that mapping and is included in the portable package.

Use `list_directory` with `include_hidden=true` for immediate filesystem entries including hidden/system items, and `resolve_path` for lexical normalization plus existence checking. Both take absolute literal paths. These are native OS operations; they do not execute PowerShell, recurse, read file contents or create missing directories. Results include names/types/sizes/flags and explicit truncation; they are observations rather than a transactional filesystem snapshot.

Use `verify_encoding` for read-only encoding acceptance rather than generating per-task decoder scripts. Supply explicit encoding plus a bounded paths array; pass task-specific `expected_count` and `allowed_root` when required. The batch validates all paths first, uses fresh disk reads, enforces BOM compatibility, and reports incomplete rows as NOT_RUN. The existing worker keeps MCP cancellation responsive, and source contents remain private to the checker. Full syntax and a runnable CLI request are in the README and `examples/verify-encoding.json`.
