# Discovering an executable with `find_program`

The `find_program` tool resolves a native executable for a later typed launch. Discovery never executes the result and never invokes a shell.

Required input:

- `name`: a bare literal executable name without path separators, backticks, command substitution (`$(` or `${`), newlines or NULs. Wildcards are not expanded.

Optional input:

- `candidates`: up to 64 absolute literal paths checked first.
- `path`: an explicit PATH list (`;` on Windows, `:` on POSIX). Every non-empty directory must be absolute. Empty components are skipped rather than interpreted as the working directory.
- `search_path`: defaults to true. Set false when only explicit candidates should be checked.
- `max_results`: 1..64, default 16.
- `max_output_bytes`: 1024..262144, default 65536.
- `timeout_ms`: 1..60000, default 5000.

Windows accepts Win32/Win64 `.exe` files validated with `GetBinaryTypeW` and appends `.exe` when absent. Legacy DOS/16-bit formats are excluded. POSIX accepts regular files with execute permission and retains the supplied absolute launch spelling, including symlinks and dot segments. This preserves interpreter virtual-environment identity and actual filesystem traversal: `missing/../program` remains missing. Results are absolute, bounded and ordered by first match; repeated result strings are removed and `selected` is the first result. Distinct aliases, hard links or Windows case variants are not deduplicated by file identity. An absent inherited PATH is an empty search list; explicit candidates are still checked.

Always inspect `scan_complete`, `truncated` and `stop_reason` before launching a result. A complete miss returns `status:"not_found"`, `selected:null`, `scan_complete:true` and `truncated:false`. A timeout, cancellation, output limit or result limit keeps the incomplete state explicit. Result and output limits return `status:"output_limit"` with `stop_reason:"max_results"` or `"max_output_bytes"`. The output budget covers the serialized operation JSON, excluding the MCP envelope. Relative PATH components return `validation_error`; PATHEXT and wildcard expansion are unsupported. Discovery does not interpret command strings.

Example using one explicit candidate:

```json
{
  "name": "cdx",
  "candidates": ["C:/tools/cordonix/cdx.exe"],
  "search_path": false
}
```

Pass the returned absolute path as the separate `program` field to `execute_program`; keep its arguments, working directory and environment overrides in their own fields.
