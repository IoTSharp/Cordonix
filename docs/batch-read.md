# Bounded file batches

`read_files` is an unreleased CLI/MCP operation on Windows and Linux. It combines 1–32 disk reads into one request, using the existing strict decoder and line/tail/raw reader. It does not read workspace memory. The [example](../examples/read-files.json) uses Windows paths; replace them with absolute paths on the target machine.

## Request and result

Each `files` item accepts `path`, `encoding` (default `auto`), and the same mutually exclusive modes as `read_file`: line range, `tail_lines`, or `raw=true`. Per-item `max_bytes` defaults to 32768 and is bounded by 262144. All paths, encodings, modes and limits are checked before the first document load. Code-shaped paths remain invalid. Validation does not collapse filesystem traversal: a validated original path is opened, preserving missing ancestors and POSIX symlink/`..` behavior.

| Batch field | Default | Range / meaning |
|---|---|---|
| `timeout_ms` | 10000 | 1–60000; one deadline for the whole batch |
| `max_total_bytes` | 1048576 | 1–4194304; sum of retained UTF-8 text bytes |
| `max_output_bytes` | 4194304 | 1024–4194304; JSON-quoted tool-result content, excluding the outer MCP envelope |

The result includes `status`, `count`, `completed`, `failed`, `not_run`, `text_bytes`, both selected byte budgets, and ordered `results`. Each item contains `index`, the supplied `path`, `source="disk"`, `status`, and the original single-read result under `result`. `completed` counts attempted reads, including failures. `failed` counts attempted items whose status is not `ok`; `not_run` counts items that were not attempted. A read that does not fit a batch budget counts as attempted but returns an `output_limit` item instead of its text.

Missing files and decoding errors retain individual results and allow later files to run; the batch returns `partial`. A budget exhaustion, cancellation or deadline stops the batch with the corresponding `output_limit`, `cancelled` or `timed_out` status. Earlier results remain intact, and all remaining items retain `not_run`. A wholly invalid request returns `validation_error` before reading, including when the selected output budget cannot hold the minimum metadata for every item.

Raw mode requires the complete decoded file to fit its per-item limit. Line/tail modes retain the existing range, truncation, encoding, EOL and decoded-document hash fields. A successful batch is a sequence of fresh disk snapshots, not an atomic multi-file snapshot.

## Decision and bounds

The observed task is repeated reads of several source files, each requiring a separate tool round trip. Keeping individual reads remains useful when only one result is needed. A sequential batch is the smallest extension: it reuses the reader and cancellation worker without adding threads, another decoder, caching or a dependency. Parallel IO and recursive discovery are separate proposals.

The algorithm first validates all items and reserves every `not_run` record. It then opens one document, selects text with the common reader, serializes a complete item and replaces the reserved record only if both budgets allow it. The output calculation includes the second escaping layer when MCP embeds result JSON inside text. A 1024-byte escaped summary reserve covers bounded counters and status. The shared JSON capacity remains 8 MiB; the 4 MiB quoted-result cap leaves room for the input-bounded request ID, envelope and summary metadata.

`max_total_bytes` is **not a disk-IO budget**. The existing document loader may load up to 8 MiB per file before selecting the requested lines; the batch retains one loaded document at a time. Work is proportional to loaded/decoded bytes plus serialized output, bounded by 32 files and the shared deadline. Synchronous OS file reads and decoding are not forcibly interruptible; cancellation is observed before/after these steps and during range scans.

Retained output, item serialization, decoding and BOM/round-trip copies all contribute to peak memory. In the existing POSIX decoder, a conservative source-derived 8 MiB-file round-trip bound is approximately 144 MiB, plus retained batch JSON below 8 MiB and two item scratch buffers each bounded by 2 MiB. This is an allocation bound, not measured RSS, and is not multiplied by 32. Peak RSS, physical IO, model tokens and provider costs remain unmeasured.

## Acceptance

The ordinary/failure/maximum corpus and raw MCP archive are documented in `tests/acceptance/three_stage_baseline.md` in the source repository. Tests compare independent single reads, literal original fixtures and all per-item states. They cover CP936, BOM, mixed EOL, Unicode, quote/backslash/newline-heavy 32-file output, each byte budget separately, preflight without disk reads, real cancellation/deadline, subsequent session use and POSIX traversal parity.

Small-file batching can increase returned bytes because it adds per-item metadata; the measured benefit is fewer tool calls. Partial maximum-output cases are correctness tests and cannot count as lossless savings. See [the current handoff](../HANDOFF.md) for final-tree evidence and separate installed-client, release and measurement boundaries.
