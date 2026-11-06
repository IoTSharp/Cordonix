# File metadata with `stat`

The `stat` tool reads metadata for one absolute path without changing the filesystem. It preserves the normalized path in both successful and missing-path responses.

Arguments:

- `path`: required absolute UTF-8 path. Relative paths, NULs, newlines and shell-shaped text are rejected.
- `timeout_ms`: optional integer from 1 to 60000; the default is 5000.

A successful response reports:

- `type`: `file`, `directory`, `link` or `other`.
- `size_bytes` and `size_known`; only regular files expose a size.
- `mode_bits` and `mode_known` on POSIX.
- `hidden`, `hidden_known`, `system` and `system_known`.
- `creation_time_ns`, `access_time_ns`, `modification_time_ns`: signed Unix epoch nanoseconds, with null for unavailable or unrepresentable values. JSON uses nullability for these fields; the C API has separate known flags. Preserve integer precision when parsing them.
- `time_precision_ns`: native timestamp representation unit, 1 on POSIX and 100 on Windows. This does not measure the underlying filesystem's effective resolution or clock accuracy.

POSIX uses `lstat`, so a symbolic link is reported as a link and its final target is not followed. Windows classifies all reparse-point entries as `link` and omits their timestamps. Intermediate path components retain platform traversal behavior. A missing entry returns `status:"not_found"` with the normalized `path` when normalization succeeded. Timeout and cancellation remain distinct from an I/O failure. Synchronous filesystem calls can block; the deadline is checked between calls and MCP uses its owned cancellable worker.

Example:

```json
{"path":"C:/repo/src/main.c","timeout_ms":5000}
```

The operation is read-only. It does not recurse, copy, move or delete entries.
