# Cordonix 0.6.0

Cordonix 0.6.0 delivers ordered single-document edit batches, explicit MCP tool
profiles, compact execution results with original-output readback, and two-phase
daily startup updates. The default surface remains 35 tools; `local-code`
advertises 27 using the same retained schemas.

`workspace_edit` accepts either the existing `old`/`new` form or 1..32 `edits`.
Each edit sees the previous temporary result. A stale SHA, wrong match count,
encoding/EOL error, cancellation or shared deadline failure leaves pre-call
memory unchanged. The default preview includes a candidate SHA and a deterministic
changed span capped at 8192 UTF-8 bytes per side with explicit truncation. Applying
a batch changes memory; `workspace_commit` still checks and writes one disk file.
See [the contract and examples](single-document-edit-batches.md).

`--mcp --tool-profile local-code` narrows discovery. `execute_program` can opt
into `result_mode:summary` with a new `capture_log_path`; status, raw-output hashes,
bounded tails and original-log access remain visible. Log publication failure
returns captured full output without replaying the program. The fixed-corpus
harness measures bytes and calls; token usage, provider cost and real agent task
success remain unmeasured. See [usage guidance](mcp-tool-guide.md).

The first launch each local day downloads a verified update into a pending area;
the next stable-launch invocation installs it before starting the new runtime.
Installer fixes preserve the verified archive hash and concurrent log ownership,
bound the Windows apply process, and retain old-version/cleanup receipts. Offline
pending installation and tampered archives have behavioral fixtures. Existing
installations need one launcher migration. Existing MCP connections retain their
running version until reconnection; [installation](install-update.md), hosted CI,
public artifact verification and client adoption remain separate evidence.

## Previous release: 0.5.0

Cordonix 0.5.0 fixes valid directory listings failing when another MCP tool is running, and adds native Windows VNC screenshots. The MCP surface now contains 35 tools. Existing tool argument contracts remain compatible.

## Overlapping MCP requests

A client can submit a batch read, a directory listing and a version query together. In 0.4.1, the first asynchronous call occupies the worker and the others can fail with `-32001: An operation is running`. In 0.5.0, overlapping tool calls wait in a FIFO and execute once in receive order.

- The pending queue is limited to 16 requests, 8 MiB of parsed source/node storage and 30 minutes of waiting per request.
- Queued requests support cancellation before execution. Queue-full, queue-timeout and session-close errors explicitly report `request_started:false`.
- Ping and cancellation remain responsive. Closing a session cancels pending work and cleans up the active operation through the existing ownership model.
- The scheduler retains tool validation, asynchronous dispatch, phone sessions, exact request IDs and existing result/error envelopes. External mutations are never replayed automatically.

Real MCP tests cover a populated directory listing and an asynchronous batch read behind an occupied worker, FIFO ordering, queue bounds, cancellation before file creation and EOF cleanup. See [request scheduling](mcp-request-scheduling.md).

## Native Windows VNC screenshots

`vnc_screenshot` accepts a literal remote IPv4/IPv6 address, optional port (default 5900) and a new PNG output path. Windows supports RFB 3.3/3.7/3.8, None/classic VNC authentication and complete Raw framebuffer capture.

When authentication is required, the client can reuse a current-user Windows credential or show a masked password dialog. Supplied or prompted passwords are saved only after successful authentication; failed authentication does not replace a saved credential. The tool sends a shared framebuffer request without keyboard, pointer or clipboard writes.

PNG publication preserves no-overwrite behavior and exposes image/cleanup outcomes. The implementation reuses existing PNG, proxy and process ownership code without a new third-party dependency. Linux returns `not_supported`. Classic VNC uses its first-eight-Latin-1-character password convention; TLS/VeNCrypt authentication is outside this release. See [the VNC contract](native-vnc.md).

## Upgrade and verification

Use the stable installer described in [Install and update](install-update.md). Existing 0.4.1 processes retain their old scheduler until their client establishes a fresh connection. Installation, a newly started MCP process and adoption by an already-open chat are separate observations.

Windows x64 ZIP and Linux x64 tar.gz packages include these release notes, the scheduling and VNC contracts, checksums, manifests, SPDX inventories and packaging provenance. Detached `.sigstore.json` bundles identify the exact source tag and workflow for each archive; verification is described in [release signing](release-signing.md). This is artifact provenance, not Windows Authenticode or SmartScreen reputation.

Windows/Linux tagged builds must pass their native, real MCP, package, reproducibility and installer gates before publication. Local validation and the final hosted/public/installation results are recorded separately in [HANDOFF.md](../HANDOFF.md).
