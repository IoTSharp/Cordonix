# MCP request scheduling

One MCP connection executes one tool call at a time. Calls received while a worker is running enter a FIFO instead of receiving the former `-32001: An operation is running` response. This covers overlapping `execute_program`, file reads, Git/search adapters and memory workspace operations. The queue order is the order the server receives requests, which can differ from the order a concurrent client creates promises.

The queue has three limits: 16 pending requests, 8 MiB of parsed JSON source/node storage, and 30 minutes of waiting per request. The active request has its existing execution/output limits in addition to the pending queue. A tool's `timeout_ms` still measures execution after dispatch; it does not include time spent in the queue. Clients can cancel an unwanted queued request without waiting for the active command.

`ping`, `tools/list` and `notifications/cancelled` are processed while requests wait. Cancellation matches the original request ID and preserves the distinction between string and numeric IDs. A queued request cancelled before dispatch receives JSON-RPC `-32800` and is removed without calling the tool. Active cancellation uses the existing tool cancellation result and process cleanup. Closing the connection cancels pending requests and the active operation; pending writes are not started during shutdown.

Errors for unstarted requests carry an `error.data` object:

| Field | Meaning |
| --- | --- |
| `reason` | `queue_full`, `queue_timeout`, `cancelled`, or `session_closed` |
| `request_started` | Always `false` for these admission/queue errors |
| `queued_requests` | Number still waiting after the decision |
| `waited_ms` | Time this request spent waiting |
| `active_request_id` | Exact ID of an active request, when one exists |
| `active_tool` | Tool currently running, when one exists |
| `active_elapsed_ms` | Elapsed time since that operation started |

Queue overflow and expiry use JSON-RPC `-32001`; cancellation and connection shutdown use `-32800`. A rejection does not start a process, edit a document or create a file. Once an external operation has started, its existing timeout/unknown-result rules still apply. No automatic mutation retry or replay is introduced.

The dispatch loop joins a completed worker before sending its final response. Progress notifications retain their existing tokens and may precede completion. The queue runs tools sequentially, including workspace changes and accounting, so it introduces no concurrent access to document slots or global counters. CLI `--request` retains its one-shot behavior.

Real stdio regression tests send overlapping requests to the native executable and check ordered results, exact file bytes, queued-write cancellation, active cleanup, queue overflow, responsive ping and EOF. Queue storage/deadline boundary checks exercise the shared implementation without waiting 30 minutes. Platform gates and installed-client adoption are recorded separately in HANDOFF.md.

## Directory-listing regression (2026-11-10)

The reported client submitted `tool_info`, `read_files` and `list_directory` together. Installed 0.4.1 returned `-32001: An operation is running; wait or cancel that request` for the version and directory calls while the batch reader occupied the worker. The directory existed and the same listing succeeded when called sequentially. The failure occurred before filesystem enumeration.

The earlier queue implementation in commit `145af3c` was on a separate branch and was not an ancestor of the investigated main checkout. This integration retains the current tool validation, complete asynchronous-tool selection, phone session and result/error mapping. Real MCP tests now include queued directory enumeration and queued batch reads alongside the existing cancellation, memory and overflow cases.

This scheduler is included in 0.5.0. Existing installed 0.4.1 connections retain the immediate-busy behavior. Until the updated runtime is installed and the client reconnects, await each Cordonix call sequentially within that connection. `Promise.all` and `Promise.allSettled` both start overlapping calls. A client-side timeout after dispatch still requires reconciliation before retrying a mutation.
