# MCP tool discovery and selection

Use the connected server's tool names and `inputSchema` as the execution
contract. This guide describes the source tree, including development
capabilities; a skill file, successful build or changed configuration does not
add a tool to an already-running MCP connection.

## Discover the connected contract

Call `tool_info` once when verifying the runtime. Record its version, platform
and `runtime=C11`, then inspect the actual `tools/list` for the connection.
Clients can display qualified names such as `mcp__cordonix__data_query`; the
server tool name in `tools/list` is `data_query`. Use the schema actually
returned, including required fields, enums and bounds. Do not infer support
from a fixed total tool count or a source checkout's documentation.

If the client cannot expose the raw tool list, use its tool-discovery interface
and record the visible tool names and schemas. A missing interface is an
evidence limitation. Testing a separate binary proves that binary's contract,
not the current client's adoption.

The [installation guide](install-update.md) provides stable Windows and Linux
client configuration and the fresh-process procedure. Windows `launch.ps1`
reads `current.json` for each launch. Linux follows `current/cdx`. Updating
either pointer does not replace an existing MCP process. Start a new client
connection under its lifecycle owner; do not terminate shared MCP services.

## Select a tool by the requested result

| Need | Tool and minimum fields | Result boundary |
| --- | --- | --- |
| Filter, project, count or group JSON, JSONL or CSV records | `data_query`: `format` and exactly one `path` or `text` | Native read-only parsing. Field selectors are RFC6901 pointers. Counts describe the examined prefix until `scan_complete=true`. |
| Recursively discover filenames with optional glob filters | `find_files`: absolute ripgrep `program`, absolute `cwd`, absolute `roots` | Uses literal `rg --no-config --files --null`. Ordinary ignore rules apply. Sorted pages require a complete scan; offsets are not snapshots. |
| Compute a binary SHA-256 | `hash_file`: absolute `path` | Exact bytes, without text decoding. Regular files only; source links/reparse points are rejected. |
| Copy a file to a new destination | `copy_file`: absolute `source`, `destination` | No overwrite; existing destination parent required. Inspect publication, cleanup and metadata flags. |
| Relocate a file within one filesystem | `move_file`: absolute `source`, `destination` | Atomic rename without replacement. No cross-filesystem copy/delete fallback; inspect `source_removed`. |
| Read several known text files | `read_files`: 1–32 `files` items, each with `path` | Ordered fresh disk reads with per-item encoding and line/tail/raw modes. Preserve `partial` and `not_run`; not an atomic multi-file snapshot. |
| Create a directory | `create_directory`: absolute `path` | `parents` and `exist_ok` default false. Inspect `created` and `directories_created`; parents already created remain after a later failure. |
| Capture a Windows desktop, region or window | `screenshot`: `path` and the selected target's fields | Saves a new PNG. Linux returns `not_supported`. `content_verified=false` requires visual inspection. |

Use `list_directory` for immediate children, `read_file` for one decoded text
read, and `search_text` for matches within files. Keep program paths, arguments,
working directories and file paths in their separate typed fields. A missing
PowerShell expression feature is not a reason to bypass a supported native
operation. Git, ripgrep and compilers remain explicit external executables.

Detailed contracts: [data queries](data-query.md), [file discovery](file-discovery.md),
[regular-file operations](file-operations.md), [batch reads](batch-read.md), and
[Windows screenshots](native-screenshot.md).

## Minimal argument examples

These are MCP tool **arguments**, without a CLI `operation` envelope. Replace
the example absolute paths and executable paths with actual ones before use.

Count matching CSV rows without creating a fixture file:

```json
{
  "format": "csv",
  "text": "state,id\nfailed,001\nok,002\nfailed,003\n",
  "operation": "count",
  "where": {"field": "/state", "equals": "failed"}
}
```

Discover C source names with the installed ripgrep executable:

```json
{
  "program": "C:/Tools/ripgrep/rg.exe",
  "cwd": "C:/repo",
  "roots": ["C:/repo/src"],
  "globs": ["*.c", "!**/generated/**"],
  "offset": 0,
  "limit": 200
}
```

Hash a regular file:

```json
{"path":"C:/artifacts/app.zip"}
```

Copy and move are writes. Use new destinations in a caller-owned scratch
directory for validation, and preserve the original source when choosing
which operation to run:

```json
{"source":"C:/artifacts/app.zip","destination":"C:/scratch/app-copy.zip"}
```

```json
{"source":"C:/scratch/app-copy.zip","destination":"C:/scratch/app-moved.zip"}
```

Read two known files, retaining independent results:

```json
{
  "files": [
    {"path":"C:/repo/README.md","start_line":1,"line_count":40},
    {"path":"C:/repo/HANDOFF.md","tail_lines":30}
  ]
}
```

Create a new directory, or explicitly accept an existing one:

```json
{"path":"C:/scratch/cordonix-check","parents":true,"exist_ok":true}
```

Capture the current foreground window on Windows to a new PNG:

```json
{"target":"active_window","path":"C:/scratch/cordonix-check/window.png"}
```

For a selected window, first call `screenshot` with
`{"action":"list_windows","max_entries":50}`, then pass the returned
`window_handle` as a string. Refresh handles after an application restart.
Capture success certifies a PNG write, not correct visual content.

On POSIX, use absolute native paths, such as `/usr/bin/rg` and
`/home/user/repo`. Screenshot device support remains Windows-specific.

## Client tool filters

Codex's official [configuration schema](https://github.com/openai/codex/blob/main/codex-rs/core/config.schema.json)
defines `mcp_servers` using `RawMcpServerConfig`, which includes the
`enabled_tools` and `disabled_tools` string arrays. Its plugin MCP overlay
schema explicitly describes an allow-list and a deny-list. Review existing
client filters when a connected server advertises a tool that the agent cannot
invoke. Add intended new names to an existing allow-list and retain deliberate
exclusions. Do not replace an established list with just the new tools.

Older copies of `examples/codex-config.toml` contained a fixed allow-list of
older tools. Reusing that list hides new capabilities. If no allow-list is
intended, omit `enabled_tools`; otherwise extend the existing list with every
required new name. Review `disabled_tools` as well.

For an intentionally restricted Cordonix connection, the following optional
line can go inside its existing `[mcp_servers.cordonix]` table:

```toml
enabled_tools = ['tool_info', 'read_file', 'read_files', 'list_directory', 'create_directory', 'screenshot', 'data_query', 'find_files', 'hash_file', 'copy_file', 'move_file']
```

This example restricts the connection to the listed tools. Omit it when no
allow-list is intended. Audit any existing `disabled_tools` independently;
configuration edits still require verification in a fresh client connection.
These are client configuration fields, not arguments accepted by Cordonix.

The configuration field names were checked against the official schema on
November 6, 2026. A particular client's restart controls, automatic schema
refresh and filter support still depend on its installed version. Consult its
[official MCP documentation](https://developers.openai.com/codex/mcp/)
and verify the observed result in a fresh connection.

## Fresh-client smoke check

1. After installation, start a new MCP connection using the stable entry from
   the [installation guide](install-update.md). Record the installation version
   separately from the connected `tool_info` result. Opening a chat can reuse
   a process, so a new chat alone is not a version check.
2. Verify the actual names and schemas for the five new tools in the selection
   table. Also check `read_files`, `create_directory` and `screenshot` if the
   intended workflow uses them. Presence and platform action support are
   separate observations.
3. Run the inline CSV `data_query` above. Require `count:2` and
   `scan_complete:true`. Record the returned status and scan fields; a partial
   scan does not prove a total count.
4. For file workflows, run `find_files` against a small known fixture and hash
   one known regular file. Require `scan_complete:true` before consuming a
   discovery page. Compare a known digest when validating hashing behavior.
   Read two known files with `read_files` and inspect every per-item result.
5. When writes are part of the authorized check, create a caller-owned scratch
   directory, copy one regular fixture there, verify its complete digest, then
   move that copy to another new path. Check `written_to_disk`,
   `source_removed`, `cleanup_complete` and copy metadata flags. Unsupported
   filesystem atomic operations are explicit boundaries, not reasons to use a
   replacing or copy/delete fallback. Screenshot checks on Windows also need
   inspection of the saved PNG.

Keep a compact receipt containing client identity, selected installation,
connected version/platform, required tool names and schema fields, smoke-check
results, and `PASS`/`FAIL`/`UNKNOWN`/`NOT_RUN` for each gate. A one-shot
`cdx --request` result can supplement that receipt but cannot certify that the
current Codex connection loaded the tool.

After a lost response to a mutation, inspect its effects before retrying.
`copy_file` can publish a destination and then report incomplete temporary-name
cleanup; `move_file` can already have removed its source. A transport timeout
does not prove either operation failed.
