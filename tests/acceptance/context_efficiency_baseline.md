# CDX-14C context-efficiency baseline

This is an offline acceptance and measurement harness around a fresh real Cordonix
stdio connection. It uses the existing Python standard-library acceptance stack;
Python and any optional tokenizer remain outside the native C11 product.

After the normal platform build and process-fixture build, run the smallest case:

```text
python tests/acceptance/context_efficiency_tests.py
python tests/acceptance/context_efficiency_baseline.py --executable build/bin/cdx.exe --output build/context-efficiency-pilot.json --pilot --deadline-seconds 30
python tests/acceptance/context_efficiency_baseline.py --executable build/bin/cdx.exe --output build/context-efficiency-baseline.json --repetitions 3 --deadline-seconds 120
```

Linux uses its built executable and `--process-fixture` path. Receipt targets must
be new files below this checkout's `build/`. An archive directory beside the receipt
keeps exact request/response frames, stderr, and caller-selected captured-output
logs; these are deliberate receipts, not temporary files to delete after success.
Each owned MCP process records PID, parent, argv, start time and cleanup state.
Windows uses the existing acceptance Job Object owner; POSIX uses a new owned
process group. Ctrl+C closes the current session. Timeouts never replay a command.

The default is three repetitions; the hard limit is five. The whole run has a
120-second default and 600-second maximum deadline, at most 64 requests per session,
256 bounded frames and 32 intervening notifications per response. All corpus loops
are finite; the fixed corpus has five cases and rejects more than 16. The pilot
uses one repetition and one execution case before the complete run.

For each repetition the harness opens separate default, explicit `full`, and
`local-code` sessions. It measures the exact UTF-8 wire-frame bytes and SHA-256 for
`tools/list`, also records the canonical compact UTF-8 `tools` array bytes separately
from the complete wire frame, compares default/full schema hashes, and requires local-code membership
to be a proper subset. The execution session compares `result_mode=full|summary`
against the same existing native process fixture and fixed small JSON corpus:
ordinary streams, output limit, invalid UTF-8, timeout and cancellation. Summary
calls explicitly supply a new `capture_log_path` and 128-byte tails. The receipt
independently reads each raw capture log to check length and captured-byte hash.

Every sample remains in the receipt, including failures. The harness reports task
calls, retries (always zero because it never retries), observed success/failed/unknown
outcomes, median and P95 wire bytes and elapsed time. Percentiles use linear
interpolation at `(N-1)*p`; tiny samples do not establish statistical significance.
`outcome=success` requires the observed status to equal that corpus case's expected
status. Expected failure-boundary outcomes can therefore be successful acceptance
cases; the original `status`, `process_status`, `observed_outcome` and explicit
`acceptance_success` remain visible. A sent call whose transport fails is retained
as `outcome=unknown`, with response byte count/hash null, no fabricated zero-byte
result and no replay. That failed session cannot send another request. Completed
and unknown samples are aggregated even when the run exits with failure. Setup and
optional tokenizer RPC calls are counted separately from measured task calls;
`transport_unknown_rpc_calls` also includes initialization/control-call failures.

Tokenizer and provider usage default to `NOT_MEASURED`; no bytes/4 conversion is
used. An already available approved tokenizer can be explicitly selected with
`--tokenizer-config path.json`, containing `name`, exact `version`, and an `argv`
array whose first element is an absolute native executable. It receives the exact
UTF-8 MCP response on stdin and must return JSON `{"tokens":123}` on stdout. The
harness invokes it through Cordonix `execute_program` with bounded output/deadlines
and owned cleanup, outside the measured task-call totals. At most two separate
tokenizer sessions handle at most 60 samples each (120 total) under the same whole
run deadline. Five repetitions of this fixed five-case corpus produce 65 samples,
so they fit in two sessions without replaying any measured task. The tokenizer
aggregate is `MEASURED` only after every sample returned a valid exact integer and
all auxiliary sessions cleaned up; on a partial failure it stays `NOT_MEASURED`,
preserves `measured_samples`, and leaves aggregate `tokens` null. No tokenizer is installed
or autodetected, and provider/model cost stays `NOT_MEASURED`. No aggregate token
or cost reduction may be claimed without the exact tokenizer/provider evidence.

The harness measures current built artifacts only. It does not establish installed
Codex adoption, remote CI, packaging, publication, live model task quality, or the
completion of later CDX-14D/E work.
