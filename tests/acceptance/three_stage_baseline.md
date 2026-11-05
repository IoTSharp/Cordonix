# CDX-11C independent acceptance

`three_stage_baseline.py` uses only the Python standard library and an explicitly
supplied Cordonix binary. It starts its own MCP connection and loopback HTTP
fixture. It does not build, install, replace, publish or modify that binary.

Run against a frozen current-tree binary, with a new output path:

```text
python tests/acceptance/three_stage_baseline.py --executable ABSOLUTE_CDX_PATH --output ABSOLUTE_BUILD_RECEIPT_PATH
```

The default is three bounded rounds under a 240-second deadline. `--rounds 1`
is a pilot and does not replace the three-round comparison. An output path must
be new and below the checkout's ignored `build/` directory. Each run preserves
raw requests, cancellation notifications, raw MCP response frames, inner tool
JSON, stderr, fixture definitions/hashes, source hashes and binary hashes beside
the receipt. Generated fixture files are removed after byte-integrity checks.
The fixed corpus includes UTF-8, explicit GBK/CP936, UTF-8 BOM, UTF-16 BOM,
mixed line endings, line/tail/raw modes, a missing file, invalid UTF-8 and an
oversized complete read. Raw source hashes and selected decoded text hashes are
separate measurements.

The contract is explicit: `read_files(files, timeout_ms, max_total_bytes,
max_output_bytes)` and HTTP `representation=both|text|base64`. No heuristic tool
matching or argument guessing is performed. The maximum corpus contains 32
distinct 256 KiB files of quotes, backslashes and newlines. A valid output-limit
result must retain all 32 ordered item states, keep successful text identical to
the corresponding separate reads, and mark unexecuted items `not_run`.
Every batch checks ordered index/path/disk-source fields, nested status equality,
actual completed/failed/not-run counts and the UTF-8 byte length of all retained
text. A separate low `max_total_bytes` case leaves ample output budget, retains
the first literal fixture, rejects the next whole item and leaves the third
`not_run`. A code-shaped path after a valid entry must fail preflight without
increasing the document-load counter. POSIX additionally compares single and
batch reads for `missing/../file` and `link/../file`; the latter must return the
literal text in the symlink target's parent. POSIX fixtures use an exclusively
owned temporary directory under `/tmp`, require an ext4 mount, and record the
mount metadata in the receipt. Fixture definitions, hashes and raw responses
remain beside the receipt under `build/`, after the temporary tree is removed.

Cancellation sends a real `notifications/cancelled` message immediately after
the batch request. A real 8 MiB file provides bounded decoding work; no test
delay or hook is added to the product. Cancellation and the independent 1 ms
deadline case must retain 32 item states, the correct stop reason, a `not_run`
suffix and a usable subsequent `tool_info` call. The same batch structure checks
apply, and any completed text must equal the literal selected line `a\n`. A
currently executing item may itself return the matching stop status, between
the successful prefix and the `not_run` suffix.

Windows HTTP runs compare default, explicit both, text and Base64 against
identical fixture bytes for ordinary, HTTP 404, maximum-size and invalid UTF-8
bodies. The fixture counts actual network requests to detect replay. Transport
completion, HTTP success, decoding, outcome and cleanup remain independent.
Default and explicit `both` independently validate the decoded text and decoded
Base64 against the fixture; representation field presence and absence are exact.
Invalid UTF-8 must retain its exact raw bytes and a null text body when text is
requested. Acceptance requires normal MCP exit code zero, no forced termination,
joined reader threads and a stopped HTTP fixture.
Linux reports the Windows-only HTTP acceptance as NOT_RUN.

Metrics count calls, original body/selected text bytes, inner JSON bytes, full
raw MCP response UTF-8 bytes including the newline, elapsed time, item success
rates and failures. Expected error visibility is checked separately from task
success; HTTP body recovery, HTTP success and body decoding are separate fields.
Separate/batch execution order alternates across rounds. A maximum-budget batch retrieves only a subset of the requested text;
its smaller output must not be described as a lossless saving over all 32
separate reads. No tokenizer/provider/price data is available to this harness;
tokens, usage and cost stay NOT_MEASURED. OS cache is not flushed and no IO speed
claim or statistical significance is inferred from three rounds.

A missing required capability or failed behavioral predicate fails acceptance.
`--observation-only` explicitly records OBSERVATION, with feature acceptance
NOT_RUN, for historical binary identification. The earlier local 19-tool binary
(`7b7c74a`, SHA-256 `66b579df0f9a0eb665e9c66ac41bdd4533b5205c27b987380d5b82438a0c7fc7`)
is stale and cannot serve as the `92aba4f` source baseline. Its initial harness
incorrectly labeled the run PASS while skipping both required capabilities;
the retained receipts now label that run OBSERVATION and preserve the reason.

`candidate-v2.json` and `candidate-v3.json` retain failed harness-development
runs. `candidate-v4.json` is INTERIM (oracle gaps): its recorded 24-case PASS
predates the independent cleanup, dual-body, per-item-count and low-text-budget
checks above and is not final acceptance. Its raw receipt and frames remain
unchanged, with this classification also recorded in
`candidate-v4.assessment.json`. In that interim measurement ordinary reads
returned the same 91 selected UTF-8 bytes: six separate calls used 2,985–2,993
MCP response bytes, while one batch used 3,462–3,463 bytes. Call count decreased;
wire bytes increased. The maximum batch retained only three of 32 file bodies,
so its smaller output is a partial-result limit, not a lossless saving.

`candidate-v5.json` retains a 24/25 result from an overly strict interruption
oracle: it rejected the current file's valid `timed_out` result followed by 31
`not_run` items. The corrected oracle permits at most one matching stop result
between successful and unexecuted items, while still checking all item fields,
counts and retained text. This failed run is preserved rather than overwritten.

## Final measured acceptance, 2026-11-06

The frozen harness SHA-256 is
`329bd929895224d3d04204d3f8f199419987be18c355b1dc7ce0b653ca6e2cf9`.
Both final runs used three rounds and retained the source/binary hashes unchanged:

| Receipt under `build/three-stage-20261105/cdx11c/` | Result | Binary SHA-256 | Measured harness time |
| --- | --- | --- | --- |
| `candidate-v6.json` | Windows PASS, 25/25 cases, 725 checks | `209c62aa4e10d5bdca558af8734700d4e7c94d29e64d384a485241466256f953` | 3.180 s |
| `candidate-v6-linux.json` | Linux PASS, 15/15 cases, 292 checks | `e1eecac4bf57bf757757d70ce596d79ca168f514009994879340a87bc4b4edff` | 7.019 s |

Both MCP processes exited normally with code zero, without forced termination.
The fixture hashes matched their original bytes and temporary fixtures were
removed. Windows made exactly 48 HTTP requests and stopped the HTTP fixture.
Linux recorded `/tmp/cdx11c-0kxeslao/fixtures` on the `/` ext4 mount; both native
path-resolution cases passed. Windows-only HTTP was explicitly NOT_RUN there.
The connected older Cordonix server rejected `wsl.exe` before launch
(`validation_error`, PID 0), so the Linux harness used the existing execution
tool to invoke Ubuntu Python with fixed arguments.

Per-round Windows results below count full MCP response bytes including newline:

| Workload | Separate calls / batch calls | Separate / batch selected text bytes | Separate MCP bytes | Batch MCP bytes |
| --- | --- | --- | --- | --- |
| Ordinary | 6 / 1 | 91 / 91 | 2,985–2,993 | 3,462–3,463 |
| Expected failures | 5 / 1 | 53 / 53 | 1,894–1,899 | 2,378–2,379 |
| Maximum output budget | 32 / 1 | 8,388,608 / 786,432 | 41,958,112–41,958,144 | 3,943,275–3,943,276 |

Ordinary reads preserved all six results while increasing wire bytes. The
failure workload preserved two successes and three expected failures. Each
maximum batch retained three complete bodies, one `output_limit` result and
28 `not_run` results. The independent 31-byte total-text budget retained the
first 30-byte body, followed by `output_limit` and `not_run`. Cancellation
returned 32 `not_run` items; deadline returned one `timed_out` and 31 `not_run`.
Linux ordinary calls likewise decreased from six to one while wire bytes grew
from 2,979–2,987 to 3,048–3,049.

HTTP explicit `both` versus `text` preserved the exact fixture body in every
round, with independently checked text/Base64 representations:

| HTTP fixture | Body bytes | Both inner JSON / MCP bytes | Text inner JSON / MCP bytes |
| --- | --- | --- | --- |
| Ordinary UTF-8 | 54,000 | 152,834 / 169,036 | 80,817 / 97,015 |
| HTTP 404 | 16 | 881 / 1,084 | 840 / 1,039 |
| Maximum escaped body | 262,144 | 1,224,176–1,224,177 / 1,661,285–1,661,286 | 874,631–874,632 / 1,311,736–1,311,737 |
| Invalid UTF-8 | 11 | 858 / 1,058 | 858–859 / 1,058–1,059 |

Invalid UTF-8 retains raw Base64 in text mode, so that case has no meaningful
size reduction. These measurements establish response sizes and call counts;
model tokens, provider usage, monetary cost, physical IO and peak memory remain
NOT_MEASURED.
