# Development and first-party use

Build on Windows with the x64 MSVC C toolchain (`eng\build.cmd`). Run `eng\test.cmd` before submitting a change. The same command is used by GitHub Actions; it builds the application and exercises real native fixtures. The executable is `build/bin/cdx.exe`.

The runtime has no Python, Node, PowerShell or third-party C-library dependency. Build setup uses the existing Visual Studio command files. Windows system libraries provide process handles, Job Objects, Unicode conversion and SHA-256. The public C sources are maintained source files, not generated artifacts.

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

The first local gate passed 139 native checks: 15 process, 36 document, 46 JSON, 25 document/protocol edges and 17 real stdio checks. An independent official MCP SDK client additionally passed 14 workflow checks. These counts describe the recorded test tree; rerun the gate for a modified tree.

Logical document loads and commits are counted in memory. They do not measure physical disk traffic: the OS has its own cache, commit validation rereads disk, and external tools perform their own IO. Measure physical IO, time and token usage before publishing percentage improvements.

The initial CLI request mode handles one operation. Persistent documents require the MCP process. A session stops at stdin EOF, 100,000 messages or 24 hours; uncommitted memory is discarded. There is no recovery journal. File commits are single-file replacements, with a documented final comparison/replacement interval for noncooperating external writers. This does not claim an OS sandbox or a filesystem transaction.

## Source layout

- `include/cdx`: public C contracts.
- `src/cdx-core`: Windows process lifecycle and structured process results.
- `src/cdx-workspace`: encoding, source revisions and explicit commits.
- `src/cdx-protocol`: JSON and operation dispatch.
- `src/cdx-cli`: one-shot CLI and persistent MCP stdio transport.
- `tests`: native observable-behavior tests.
- `eng`: the single MSVC build/test workflow.
