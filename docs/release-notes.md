# Cordonix 0.5.0

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
