# Single-document edit batches

`workspace_edit` can apply 1–32 ordered literal replacements to one open document
using its current memory SHA-256. The complete batch either changes memory or
leaves the original memory revision intact. `workspace_commit` remains the
explicit disk-write operation.

Use ordinary `apply_patch` for small, maintained UTF-8 source edits. The workspace
route is useful when several replacements need one revision check, or when the
file's explicit CP936, BOM, EOL and disk-conflict guarantees matter.

## Preview, apply and commit

Open the document in the current MCP session with `workspace_open`, then use the
returned `document_id` and `memory_sha256`:

```json
{
  "document_id": 1,
  "expected_sha256": "<current memory_sha256>",
  "edits": [
    { "old": "alpha", "new": "gamma", "expected_count": 1 },
    { "old": "gamma", "new": "delta", "expected_count": 1 }
  ],
  "dry_run": true,
  "timeout_ms": 15000
}
```

Each edit sees the preceding edit's candidate text. In this example the second
replacement finds `gamma` created by the first. Matches within one edit are
literal, left-to-right and non-overlapping; replacement text is not searched
again by that same edit. There is no fuzzy matching or position-based overlap
resolution.

`dry_run` defaults to `true`. A successful preview returns `status: "preview"`,
the candidate byte counts and a bounded difference, while memory SHA, dirty state
and disk remain unchanged. Review the difference and apply the same batch with
`dry_run: false` against the still-current SHA. A successful application returns
`status: "ok"`, the resulting memory SHA and `written_to_disk: false`.

Call `workspace_commit` with the document ID to write the applied revision.
Commit verifies the original disk identity and SHA before replacement. A changed
file returns `file_changed`, preserves the external writer's bytes and leaves
the candidate dirty in memory. Close the document after committing, or explicitly
discard dirty memory with `workspace_close` and `discard: true`.

## Parameters and limits

| Parameter | Contract |
| --- | --- |
| `document_id` | ID returned by `workspace_open` in this same MCP session. |
| `expected_sha256` | Current memory SHA, checked against the starting revision. |
| `edits` | Array of 1–32 replacement objects, applied in array order. |
| `edits[].old` | Nonempty exact UTF-8 anchor, not a regular expression. |
| `edits[].new` | Exact replacement text; an empty string deletes matches. |
| `edits[].expected_count` | Required exact occurrences, default 1, range 1–10000. |
| `dry_run` | Boolean, default `true`; `false` changes memory only. |
| `timeout_ms` | Whole-batch deadline, default 15000 ms, range 1–60000 ms. |

The existing single-edit form, with top-level `old`, `new` and optional
`expected_count`, keeps its existing behavior. Supply either this legacy form
or `edits`; mixing the forms, including top-level `expected_count` with `edits`,
is a validation error. Batch mode does not add another MCP tool or alter the
35-tool `full` / 27-tool `local-code` profiles.

The existing 8 MiB document limit and strict encoding rules apply. CP936
replacements must be representable without loss. UTF-8 BOM and existing CRLF/LF
style are preserved; existing mixed or CR-only documents and replacements that
introduce incompatible EOL return `mixed_eol`. A later encoding or EOL failure
rolls back earlier candidate replacements in the same batch.

## Result and difference

A batch result reports `edit_count`, `completed_edits`, zero-based
`failed_edit_index` (or `null`), and bounded `edits` records with `index`,
`status`, `expected_count` and `actual_count`. Per-edit success describes candidate
processing; a failing batch never partially applies those earlier candidates to
the live document. Later unprocessed edits are reported as not run.

`bytes_before` and `bytes_after` count the document's encoded bytes, including a
BOM. `memory_sha256` describes the actual current memory revision, so a preview
or failed batch retains the original SHA. A successful batch's `preview_sha256`
describes the complete candidate encoded bytes, including their BOM; it equals
the resulting memory SHA after application. `dirty_in_memory` and
`written_to_disk` retain their separate meanings.

`diff` describes one changed UTF-8 byte span after common prefix and suffix:

| Field | Meaning |
| --- | --- |
| `prefix_bytes` | Unchanged UTF-8 bytes before the changed span. |
| `removed_bytes` | Full number of old UTF-8 bytes in that span. |
| `added_bytes` | Full number of new UTF-8 bytes in that span. |
| `before` / `after` | Leading text of the respective span, each at most 8192 UTF-8 bytes. |
| `truncated` | `true` when either span exceeds its preview budget. |

Span boundaries and preview endings respect complete UTF-8 characters. The
difference is a deterministic bounded preview, not a minimal line diff. Text
between distant edits can be included in the span. A truncated preview does not
change whether the batch can be applied; inspect full candidate memory after
application using bounded `workspace_read` pages before committing when needed.

An ambiguous match, missing anchor, stale SHA, encoding error, cancellation or
timeout preserves the starting live memory and disk. A timeout is not permission
to repeat a disk mutation. This feature provides one-document memory
transactions; it does not claim multi-file atomicity or persistent sessions
across separate CLI invocations.

## Verification

Run the real stdio acceptance against the just-built executable:

```text
python tests/protocol/edit_batch_stdio.py --executable build/bin/cdx.exe --output build/edit-batch-windows
python3 tests/protocol/edit_batch_stdio.py --executable build/linux/bin/cdx --output build/edit-batch-linux
```

The harness has a 128-call / 100-check bound, a maximum 120-second wall-clock
budget, signal cancellation and owned-process cleanup. `--pilot` runs the small
schema and legacy compatibility check first. Outputs must be new directories
under `build/`; raw RPC frames and receipts remain there, while task-created
document fixtures are removed. Receipts separate source behavior from installed
runtime adoption, packaging and public release.

For a small reproducible traffic comparison, add `--measure-only` with a new
output directory. This mode performs six equivalent legacy edits and one
six-edit batch, checks identical final text and memory SHA, and counts the exact
request and response UTF-8 bytes for the edit RPCs. It has a twenty-RPC bound and
keeps the original frames. Initialization, opening, reading and closing are
retained in the receipt but excluded from the edit-only comparison. A fixed
six-edit task does not establish token, billing or general workload savings.
