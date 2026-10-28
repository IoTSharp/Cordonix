# Workspace core verification

Build from a Visual Studio 2026 installation with the existing MSVC toolchain:

```text
eng\build-workspace.cmd
build\workspace\workspace-tests.exe <absolute-owned-scratch-directory>
```

The test runner creates only `<scratch>\workspace-test.txt` and deletes it on normal completion. Use a new task-owned directory. `CDX_PILOT=1` selects the five-check UTF-8 pilot; the full suite has 36 observable checks. It is a native C executable and does not need Python or PowerShell at runtime.

The public API is declared in `include/cdx/document.h`:

- `cdx_document_open`: allocates a document containing original bytes, current bytes, UTF-8 text, BOM/EOL, file identity, timestamps and SHA-256.
- `cdx_document_edit_utf8`: compares the current in-memory SHA, checks the exact anchor count, previews or edits memory without touching disk.
- `cdx_document_commit`: compares the original disk identity/size/time/hash, writes and flushes a same-directory temporary file, rechecks the disk version, replaces with `ReplaceFileW`, then reads back the committed hash.
- `cdx_document_free`: releases the document.

The caller owns each document and must free it before opening another file into the same object. Editing one document concurrently is not supported. `dry_run=1` returns `CDX_STATUS_OK` while `result.status=CDX_STATUS_PREVIEW`; successful edits return `CDX_STATUS_OK` and expose `dirty_in_memory`. This allows the caller to inspect previews without changing its current revision.

Supported encodings are strict UTF-8, UTF-8 BOM, UTF-16LE/BE with BOM, and explicitly selected CP936. AUTO recognizes a BOM, otherwise attempts strict UTF-8 only. It never guesses CP936 from invalid UTF-8. Original bytes must survive decode/re-encode exactly. Unrepresentable edits fail before any memory or disk mutation. Mixed/CR-only EOL edits are rejected; callers preserve LF/CRLF explicitly in replacement text.

The first slice limits a document to 8 MiB and paths to fewer than 480 UTF-16 code units. It is a single-document workspace, not a multi-file transaction. The compare/replace interval is not an atomic filesystem compare-and-swap against an unrelated external writer. Changes completed before the final hash check are rejected; a non-cooperating writer can still race the final replacement. There is no automatic second write or fallback on a failed `ReplaceFileW`.

Validation: MSVC `/TC /std:c11 /W4 /WX /O2` build exits 0. Full suite exits 0 with 36 checks covering UTF-8/BOM/UTF-16LE/BE/CP936, LF/CRLF, invalid and unrepresentable text, no automatic code-page guessing, empty files, exact/missing/ambiguous/overlapping anchors, multiple memory edits before one commit, stale memory SHA, external disk conflict and a known SHA-256 vector.
