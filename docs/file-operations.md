# Native regular-file operations

Use these tools for binary checksums, copying one file to a new path, or relocating
one file without replacement. Use `read_file` for decoded text and `create_file`
for new text content. Use `copy_file` when the source must remain, and `move_file`
when it should be removed by a successful rename.

`hash_file`, `copy_file` and `move_file` operate on absolute paths with literal fields. They reject directories and source links/reparse points. Existing path syntax checks apply, including code-shaped and newline paths. Validation uses normalized paths, while native operations retain the original traversal semantics.

| Tool | Required fields | Behavior |
| --- | --- | --- |
| `hash_file` | `path` | SHA256 over the exact binary stream; no text decoding. |
| `copy_file` | `source`, `destination` | Stream to a same-directory temporary file, flush, then publish atomically without replacement. |
| `move_file` | `source`, `destination` | Same-filesystem atomic rename without replacement. No copy/delete fallback. |

The destination is a new **file path**, not a directory from which a filename is
inferred. Its parent directory must already exist. An existing destination,
including the source itself, is a collision and is never overwritten. Minimal
calls, with actual absolute paths and an existing destination parent, are:

```json
{"path":"C:/artifacts/app.zip"}
```

for `hash_file`,

```json
{"source":"C:/artifacts/app.zip","destination":"C:/archive/app.zip"}
```

for `copy_file`, and

```json
{"source":"C:/artifacts/app.zip","destination":"C:/artifacts/app-ready.zip"}
```

for `move_file`. POSIX calls use absolute native paths in the same fields.

All tools accept `timeout_ms` (default 10000, 1..60000). Hash and copy accept `max_bytes` (default 64 MiB, 1 byte..1 GiB). They use a fixed 64 KiB buffer and check cancellation/deadline between chunks. Synchronous filesystem calls are not forcibly interrupted, so a slow filesystem may exceed the deadline before control returns. Move checks before its single rename and reports its known result; it does not report a timeout after a successful mutation.
Do not send `max_bytes` to `move_file`; it does not stream the file and rejects
that parameter.

Copy preserves exact default-stream bytes. Windows requests source creation/access/write timestamps; the new file inherits destination-directory permissions. Linux requests ordinary permission bits (`0777`) and access/write timestamps, with ownership belonging to the caller. After applying metadata, both platforms read it back from the prepared file: successful publication reports `metadata_checked=true` and `metadata_preserved=true` only when those documented fields match exactly. A filesystem may round timestamps or synthesize permissions even when its setter succeeds; copying bytes can succeed with `metadata_preserved=false`. Hash/move and copies that do not publish report `metadata_checked=false`, which does not assert either preservation or loss. ACLs, ownership, alternate streams, xattrs, special mode bits and sparse allocation are not cloned. Move retains native file metadata because it renames the file, without separately checking it. No crash-durability or transactional batch guarantee is claimed.

Windows holds the source open without write/delete sharing while hashing/copying. Linux checks the open file's size and modification time before and after streaming; `file_changed` discards its digest and prevents copy publication. This is not an atomic snapshot of a concurrently modified file.

Linux copy publishes the flushed temporary file using `link(temporary, destination)`, which creates a new destination atomically without replacement, then removes the temporary name. Filesystems must support hardlinks. A failure to remove that name reports `written_to_disk=true`, `cleanup_complete=false` and an I/O error; it never removes the published destination. This is atomic destination publication, not a claim that both directory-entry changes form a transaction.

Linux move uses `renameat2(RENAME_NOREPLACE)`. Unsupported kernels/filesystems return `not_supported`; cross-filesystem moves return `cross_device`. For example, WSL drvfs can reject this rename flag although ordinary NTFS hardlinks allow copying. There is no replacing-rename or non-atomic move fallback. Tests on such a filesystem explicitly verify the unsupported boundary and preserve source/destination; an ext4 fixture run is required to exercise successful native moves.

The observed WSL drvfs fixture also accepts `futimens` but drops subsecond precision: expected `1793929016.150033100`, actual `1793929016.000000000` across five copies. Its mode setters report success while exposing synthetic `0777` permissions. The tool reports metadata limitations separately from byte-copy success. Tests compare flags with actual native metadata before reading content; the ext4 fixture additionally requires exact preservation as the positive case.

Results distinguish `already_exists`, `too_large`, `not_regular_file`, `file_changed`, `cancelled`, `timed_out`, native I/O errors and successful publication. `written_to_disk` identifies the final destination publication, `source_removed` identifies a successful move, and `cleanup_complete` identifies temporary-file cleanup. A partial stream's byte count remains visible on failure, while its SHA256 is returned only after a complete successful operation. Completed native mutations are reported as known successes; callers must inspect any transport-level unknown outcome before retrying.

Inspect the result flags as well as `status`:

| Observation | Meaning and next step |
| --- | --- |
| `status:ok`, `sha256` present | A complete hash/copy stream has a usable digest. Hashing does not write a destination. |
| `written_to_disk:true` | The destination was published. A copy can still report a cleanup error; do not repeat publication blindly. |
| `source_removed:true` | The move completed. A second move from the old path is not an idempotent retry. |
| `cleanup_complete:false` | Temporary-name cleanup was incomplete; an already published destination is retained. |
| `metadata_checked:true`, `metadata_preserved:false` | Copy bytes can be correct while the filesystem cannot preserve the documented metadata exactly. |
| `already_exists` | The destination was retained. No replacing fallback is performed. |
| `file_changed` | The stream's digest is discarded and a copy is not published. Retry after the source settles. |
| `cross_device` or `not_supported` | The requested atomic move/publication boundary is unavailable. No copy/delete or replacing fallback is performed. |

After a transport timeout or lost response, first inspect the source/destination
and, for a copy, compare complete hashes when needed. A transport failure does not
prove the native mutation failed. Native `timed_out`/`cancelled` results before
publication leave the destination untouched; use the returned publication and
cleanup flags to determine the known effects.

The native test executable takes one existing absolute fixture directory. It exercises known SHA256 vectors (empty, `abc`, one million `a` bytes and independent crypto vectors at lengths 55/56/63/64/65/65537), binary/Unicode/apostrophe files, copy readback, metadata, collision preservation including identical source/destination, move, cancellation, size limits, temporary cleanup and JSON protocol results. POSIX tests additionally verify symlink rejection and missing-parent traversal.
