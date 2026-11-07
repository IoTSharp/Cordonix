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
reads `current.json` for each launch. Both launchers start a separate update worker on the first launch each day. Linux uses `launch.sh`, then follows `current/cdx`; the older direct entry bypasses startup checks. Updating
either pointer does not replace an existing MCP process. Start a new client
connection under its lifecycle owner; do not terminate shared MCP services.

## Select a tool by the requested result

| Need | Tool and minimum fields | Result boundary |
| --- | --- | --- |
| Identify the connected runtime | `tool_info`: `{}` | Version, build revision/dirty flag and update capability; counters are per-session logical operations. |
| Run one local program | `execute_program`: absolute `program`, literal `args`, absolute `cwd` | Bounded execution owns descendants. Inspect exit/status/cleanup; never launch persistent services here. |
| Read Git changes | `git_status`: absolute Git `program`, worktree `cwd` | Parsed porcelain paths, including renames. Does not stage or modify Git. |
| Inspect local processes | `query_processes`: optional name/command filters | Windows only, read-only. Partial access and truncated results are not a complete process inventory. |
| Search file contents | `search_text`: absolute rg `program`, `cwd`, `patterns`, `roots` | Fixed strings by default; use `literal:false` for regex. `no_match` differs from failure. |
| Read fresh disk text | `read_file`: absolute `path` | Select line, tail or raw mode. `encoding` defaults to BOM/strict UTF-8 auto detection. |
| List immediate children | `list_directory`: absolute `path` | No recursion; check `truncated`. Use `find_files` for recursive filenames. |
| Normalize an existing path | `resolve_path`: absolute `path` | Lexical normalization; does not resolve to a physical symlink target. |
| Verify declared file encodings | `verify_encoding`: `paths`, explicit `encoding` | Fresh-byte decoding and exact round-trip; `auto` forbidden. Incomplete rows remain `not_run`. |
| Open a memory document | `workspace_open`: absolute `path` | Returns a session-local `document_id`; close after use. Separate CLI requests do not share it. |
| Read memory text | `workspace_read`: `document_id` | Default byte mode or explicit line/tail/raw mode; does not reload disk. |
| Find memory text | `workspace_search`: `document_id`, literal `pattern` | Returns UTF-8 byte offsets, up to 100 matches; no regex. |
| Inspect a memory revision | `workspace_status`: `document_id` | The disk SHA is the loaded/last-committed snapshot, not a fresh disk check. |
| Replace memory text | `workspace_edit`: `document_id`, current `expected_sha256`, `old`, `new` | Expected count defaults 1. Preview by default; `dry_run:false` applies only in memory. |
| Convert encoding | `workspace_convert_encoding`: `document_id`, current `expected_sha256`, `encoding` | Lossless memory conversion; preview by default. Commit separately. |
| Save a memory document | `workspace_commit`: `document_id` | Rechecks disk identity/SHA before one-file replacement; inspect `written_to_disk`. |
| Release a memory document | `workspace_close`: `document_id` | Dirty memory requires a successful commit or explicit `discard:true`. |
| Probe or execute SSH | `ssh`: `target`; execute also needs `script` and verified authentication | Windows only. Direct/jump/interactive routes have different trust and prompt requirements. |
| Read/write remote file bytes | `sftp`: pinned/authenticated `target`, remote `path` | Windows only. Default stat; read returns hex; write needs `allow_write:true` and an absent path. |
| Make an HTTP request | `http`: `url` | Windows only. No automatic redirects/cookies/auth; transport success and HTTP status differ. |
| Exchange TCP/Telnet/MQTT data | `network`: literal IP `host`, `port` | Windows only. Explicit send permission; no application-delivery claim or TLS fallback. |
| Inspect or use a serial port | `serial`: default `{}` lists ports; other actions need `port` | Windows only. Sending and line configuration are separate opt-ins. |
| Use Bluetooth phone services | `phone`: `action` | Windows only. Device actions require a discovered address; mutating actions preview by default. |
| Filter, project, count or group JSON, JSONL or CSV records | `data_query`: `format` and exactly one `path` or `text` | Native read-only parsing. Field selectors are RFC6901 pointers. Counts describe the examined prefix until `scan_complete=true`. |
| Recursively discover filenames with optional glob filters | `find_files`: absolute ripgrep `program`, absolute `cwd`, absolute `roots` | Uses literal `rg --no-config --files --null`. Ordinary ignore rules apply. Sorted pages require a complete scan; offsets are not snapshots. |
| Read one path's metadata | `stat`: absolute `path`, optional `timeout_ms` | Reports file/directory/link/other, size availability, flags, timestamps and precision. Missing paths retain normalized context. |
| Resolve an executable before launch | `find_program`: bare `name`, optional absolute `candidates` or PATH | Discovery only. Results are absolute, unique and bounded; inspect `scan_complete`, `truncated` and `stop_reason`. |
| Compute a binary SHA-256 | `hash_file`: absolute `path` | Exact bytes, without text decoding. Regular files only; source links/reparse points are rejected. |
| Copy a file to a new destination | `copy_file`: absolute `source`, `destination` | No overwrite; existing destination parent required. Inspect publication, cleanup and metadata flags. |
| Relocate a file within one filesystem | `move_file`: absolute `source`, `destination` | Atomic rename without replacement. No cross-filesystem copy/delete fallback; inspect `source_removed`. |
| Read several known text files | `read_files`: 1–32 `files` items, each with `path` | Ordered fresh disk reads with per-item encoding and line/tail/raw modes. Preserve `partial` and `not_run`; not an atomic multi-file snapshot. |
| Create a directory | `create_directory`: absolute `path` | `parents` and `exist_ok` default false. Inspect `created` and `directories_created`; parents already created remain after a later failure. |
| Create a new text file | `create_file`: absolute `path`, string `content` | Parent must exist; no overwrite. Use `content` for the body. `encoding` defaults to `utf-8`; `auto` is not supported. |
| Capture a Windows desktop, region or window | `screenshot`: `path` and the selected target's fields | Saves a new PNG. Linux returns `not_supported`. `content_verified=false` requires visual inspection. |

Use `list_directory` for immediate children, `read_file` for one decoded text
read, and `search_text` for matches within files. Keep program paths, arguments,
working directories and file paths in their separate typed fields. A missing
PowerShell expression feature is not a reason to bypass a supported native
operation. Git, ripgrep and compilers remain explicit external executables.

Detailed contracts: [data queries](data-query.md), [file metadata](file-stat.md),
[program discovery](program-discovery.md), [file discovery](file-discovery.md),
[regular-file operations](file-operations.md), [batch reads](batch-read.md), and
[Windows screenshots](native-screenshot.md).

## Parameter and result rules

Use the exact input names from `tools/list`. `create_file` takes `content`, even
though reading tools return `text`. Omit unused fields; JSON `null` is not an
omitted string, boolean or read mode. `read_file` and `read_files` default to
`encoding: "auto"`; new-file creation defaults to `utf-8`. Use the enum spelling
`utf-8`, not `utf8`.

For reads choose `start_line`/`line_count`, `tail_lines`, or `raw:true`.
`workspace_read` alone also supports byte `offset`/`limit`; do not mix byte
fields with line/tail/raw/max_bytes fields. Line numbers start at 1 and byte
offsets at 0. Keep the current `memory_sha256` after each applied edit or
conversion; `expected_sha256` must match that current memory revision.

The document workflow is `workspace_open` → read/search/status → edit/convert
→ `workspace_commit` → `workspace_close`, all in the same MCP session.
`dry_run:false` on edit/convert does not save a file. Commit before a compiler
or Git reads disk. Close without `discard:true` after a successful commit.

For SSH execute, use exactly one credential alias or unencrypted key file per
authenticated endpoint, and an independently verified host-key pin. A probe
does not authenticate the target or establish trust by itself. Interactive
routes require the bastion's pin plus caller-supplied ready/shell prompts and
selection template; the selected target omits private_key_path/host_key_sha256.
For `phone`, only adapters/devices/discover/connection_status omit a device
address; discovery requires at least 2000 ms. sms_read/read_code/wait_sms use
the prior sms_list snapshot SHA and identical paging settings.

For `serial`, explicit baud/data/parity/stop fields require `configure:true`;
nonempty transmit bytes also require `allow_send:true`. For `network`,
TCP/Telnet use connect/exchange, MQTT uses connect/publish, and idle_timeout_ms
must not exceed timeout_ms. A successful send does not prove device or remote
application acceptance. Reconcile unknown outcomes before any mutation retry.

For `screenshot`, list_windows accepts only its action and listing limits;
omit capture fields. Region capture needs all x/y/width/height fields. A window
handle is a decimal or `0x` string from list_windows, not a numeric JSON value.
Successful PNG creation still requires visual inspection.

All 34 tool descriptions and every named property, including array objects
and SSH/SFTP endpoint definitions, are checked through the real MCP tool list.
The test also checks that required names exist in their property definitions.
Behavioral tests remain responsible for defaults, conflicts, encoding and
mutation boundaries; description coverage alone does not prove hardware or
remote-service acceptance.

## Minimal argument examples

These are MCP tool **arguments**, without a CLI `operation` envelope. Replace
the example absolute paths and executable paths with actual ones before use.

Create a UTF-8 text file with its required `content` field:

```json
{"path":"D:\\project\\new.txt","content":"中文\r\n","encoding":"utf-8"}
```

`read_file` returns `text`, but `create_file` takes `content`. If a create request
uses `text` instead, the validation error names the missing `content` and gives
the rename instruction. Correct that field; changing an already-valid encoding
does not fix a missing body. These diagnostics are development changes until
the runtime is published, installed and a fresh MCP connection loads it.

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

Read metadata without changing the path:

```json
{"path":"C:/repo/src/main.c","timeout_ms":5000}
```

Resolve one explicit executable candidate before passing it to `execute_program`:

```json
{
  "name": "cdx",
  "candidates": ["C:/tools/cordonix/cdx.exe"],
  "search_path": false
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
enabled_tools = ['tool_info', 'read_file', 'read_files', 'list_directory', 'create_directory', 'screenshot', 'data_query', 'find_files', 'stat', 'find_program', 'hash_file', 'copy_file', 'move_file']
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
2. Verify the actual names and schemas for the current additions (`stat` and
   `find_program`) together with the existing data, discovery and file-operation
   tools in the selection table. Presence and platform action support are
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
