# File metadata with `stat`

The `stat` tool reads metadata for one absolute path without changing the filesystem. Both successful and missing-path responses retain path context. POSIX returns the supplied absolute spelling, including dot segments, so the returned path identifies the entry actually queried through intermediate symbolic links. Windows returns its normalized path.

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

POSIX uses `lstat`, so a symbolic link is reported as a link and its final target is not followed. Windows classifies all reparse-point entries as `link` and omits their timestamps. Representable Windows timestamps before 1970 use negative Unix nanoseconds. Intermediate path components retain platform traversal behavior. A missing entry returns `status:"not_found"` with `path` when validation succeeded. Timeout and cancellation remain distinct from an I/O failure. Synchronous filesystem calls can block; the deadline is checked between calls and MCP uses its owned cancellable worker.

Example:

```json
{"path":"C:/repo/src/main.c","timeout_ms":5000}
```

The operation is read-only. It does not recurse, copy, move or delete entries.

POSIX hidden-name metadata ignores trailing separators. A terminal `.` or `..`
uses the actual traversed directory name; the returned path still retains the
original spelling. If that additional name resolution fails, `hidden_known`
is false rather than inventing a flag. Ordinary final symlink names remain
independent of their target names.
