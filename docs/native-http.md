# Native HTTP and HTTPS

The `http` tool uses the Windows WinHTTP C API in process. It requires no
curl executable, downloaded runtime, Python, PowerShell or third-party TLS
library. Windows supplies DNS, HTTP framing, proxy CONNECT and TLS trust.
The initial backend is Windows only; Linux returns `not_supported`.

## Request contract

`url` is an absolute `http://` or `https://` URL of at most 8192 UTF-8 bytes.
Embedded user/password credentials, fragments and line breaks are rejected.
`method` is GET, HEAD, POST, PUT, PATCH, DELETE or OPTIONS and defaults to GET.
`body` is explicit UTF-8 request data, at most 64 KiB; GET/HEAD require an empty
body. `headers` is an array of at most 32 `{name,value}` objects, preserving
duplicate names. Their total text is bounded to 16 KiB. Header line injection
and caller overrides of Host, Content-Length, Transfer-Encoding or Connection
are rejected; the typed request and WinHTTP control framing.

`representation` is optional and defaults to `both`, preserving the original
response fields. `text` keeps the decoded `body` and omits `body_base64` after
successful decoding. `base64` keeps the exact `body_base64` and omits decoded
text. A text request that encounters a strict decoding error still includes
`body_base64` so the captured bytes remain recoverable without replaying the
network request. The values are an explicit enum: `both`, `text` or `base64`.

`proxy` defaults to `auto`; an omitted or empty value also selects `auto`.
The client first tries direct. Only a network connection failure before
WinHTTP observes `SENDING_REQUEST`, `REQUEST_SENT` or a connected/TLS phase
permits evaluating local listeners on 7890 and 1080. A TCP listener is not
accepted until an HTTP CONNECT or SOCKS5 handshake succeeds. `direct` disables
evaluation. Explicit `host:port` or `http://host:port` retains the per-request
HTTP proxy route; `socks5://127.0.0.1:PORT` uses only that local SOCKS tunnel.
The choice applies to this call only, without changing system settings or
environment variables. Proxy credentials in the URL are rejected.
No shell, system browser, cookies, automatic authentication or automatic
redirect following is involved. A 3xx response is returned for the caller to
assess. HTTP 403/404, authentication refusals, TLS errors and any send-phase
evidence never cause route replay. Direct connection gets at most three
seconds and at most half the remaining deadline, preserving time for fallback
within the original timeout. The final handle-close callback must confirm
the same no-send phase before fallback; `request_started` alone is not proof.
Cordonix never retries a mutation after an uncertain result. This first
version does not implement all curl features,
multipart files, streaming downloads to disk, credential-store authentication,
cookie jars or authenticated SOCKS proxies.

WinHTTP cannot consume a SOCKS socket directly. A C bridge therefore listens
on an ephemeral loopback port for one client in this call, consuming an
already connected tunnel from the shared proxy helper. It accepts only the
known target authority. HTTP absolute request targets become origin form;
HTTPS CONNECT gets a 200 reply and then forwards unchanged encrypted bytes.
The original URL/hostname remains in WinHTTP, preserving SNI and certificate
checks. The bridge has a 32 KiB initial-header bound, a 4 MiB relay bound and
the original deadline/cancellation. Its sockets and thread close at call end.

An observed Windows limit is explicit: WinHTTP bypasses proxies for loopback
URL destinations. Auto therefore keeps localhost and literal loopback targets
local/direct; an explicit proxy for such a URL returns `not_supported` before
opening a tunnel. This concerns the target URL; the local proxy endpoint is
still allowed at 127.0.0.1. No machine policy or trust setting is changed.

HTTPS retains normal system certificate and hostname verification. There is
no insecure/ignore-certificate option. Trust failures are `tls_error`, with
the WinHTTP error code. The client does not install certificates or change
the user's root store.

## Response and limits

`status` describes transport completion, cancellation, timeout, validation,
TLS or output limits. `status_code` is an independent HTTP observation; for
example HTTP 404 can have `status=completed` and `http_success=false`.
`response_headers` retains the raw response header block, bounded to 64 KiB.
The response body limit defaults to 64 KiB and can be 1–256 KiB.

`body_base64` preserves the captured response bytes when the selected
representation includes it. `body` is strict UTF-8 text by default, with
explicit CP936/GBK decoding available through `output_encoding`. An invalid
text body is null and `body_encoding_status` is `encoding_error`; the
transport status and Base64 remain available even in `text` mode. Every
mode runs the same strict decoder and retains `body_encoding_status=ok` or
`encoding_error`. The selection changes only which body fields are emitted;
transport, HTTP, encoding, completeness, proxy and cleanup observations keep
their existing meanings. Select `both` or `base64` when exact captured bytes
are needed after a successful decode. Text mode retains no hidden cache or
later-read handle.
Content decompression is not enabled, so compressed bytes remain compressed.

The overall `timeout_ms` defaults to 30000 and is limited to five minutes.
WinHTTP runs asynchronously; the waiting C thread checks cancellation and a
shared deadline every 10 ms, with at most 30002 waits per operation. Response
reads have a byte/iteration bound as well as the same deadline. Pending input
and read buffers remain owned until WinHTTP's final handle-close callback.
The callback reference and caller reference are released separately to avoid
freeing state while the asynchronous transport still owns it.

`request_started` marks the attempt to dispatch; `response_complete` confirms
that response body EOF was read. `remote_outcome` is `not_run` before dispatch,
`known` after a complete response, and `unknown` after an interrupted or
limited response. A response status code does not establish business-level
commit/rollback. `cleanup_complete` reports local handle cleanup, with an
additional two-second bound for the final callback and a separate two-second
bridge-thread cleanup bound when used. An uncertain remote
mutation must be reconciled before retrying.

`request_send_started` records WinHTTP send-phase evidence. The nested `proxy`
trace contains `status`, `selected_proxy`, `direct_attempted`, `direct_error`,
`native_error`, probed ports/open flags, `tunnel_attempts` and cleanup status.
`proxy_bridge_used` reports the bridge route; `proxy_bridge_error` retains
bridge diagnostics separately from the WinHTTP error.

## Example

```json
{
  "url": "https://example.com/api/items",
  "method": "POST",
  "headers": [{"name": "Content-Type", "value": "application/json"}],
  "body": "{\"name\":\"example\"}",
  "proxy": "http://127.0.0.1:7890",
  "timeout_ms": 10000,
  "response_limit_bytes": 16384
}
```

Pass this object to MCP tool `http`, or wrap it as
`{"operation":"http","arguments":...}` for CLI `--request`.

## Validation boundary

### CDX-11A decision record (2026-11-06)

The 2026-11-05 assessment observed one response with 52,594 captured body
bytes and 70,128 Base64 characters. The protocol source also unconditionally
emitted both representations. That demonstrates duplicate representation;
it does not establish token, cost, latency or whole-task savings.

Keeping the original output requires no maintenance but retains that
duplication. The selected minimum extension is a three-value projection
enum at the protocol boundary; it uses the existing request, transport,
decoder and byte buffer. A general field selector or retained-result store
would require separate retention, lifetime and recovery contracts without a
proven need in this slice, so those options remain deferred.

Omitting the field keeps the original `both` serialization. `text` omits
Base64 only on successful decoding; `base64` omits the decoded text field.
Invalid values fail validation before dispatch. All modes perform the same
decode and retain encoding status; invalid UTF-8/CP936 and a limit cutting a
multibyte sequence still return the exact captured Base64 in that call.
There is one transport execution; representation never triggers a retry.

Work is linear in captured body bytes. Base64 itself is exactly
`4 * ceil(n / 3)` characters; text-mode omission removes that payload and
its JSON field overhead. The existing 256 KiB body and 64 KiB header caps
are unchanged. Peak memory still includes the captured bytes, strict-decoded
text and JSON/MCP serialization; this slice does not claim memory or IO
acceleration. Actual internal JSON and complete MCP bytes are measured by
the independent three-stage acceptance fixture, separately from tokens.

Acceptance compares a default call and explicit projections against the
same local peer bytes, including literal POST counted once per request,
CP936, binary decoding failure, 404, output limits, a truncated multibyte
sequence, timeout and real MCP cancellation followed by a usable session.
The projection cases run only through the protocol MCP binary; the native
C API shim has no representation parameter and retains its existing suite.
A changed body, outcome/cleanup field, additional dispatch, or lost fallback
bytes falsifies this choice. Build, protocol, analysis and independent
measurement results are recorded by the integration owner before closure.

The 2026-11-06 three-round acceptance preserved a fixed 54,000-byte UTF-8
fixture while reducing full MCP response bytes from 169,036 (`both`) to
97,015 (`text`). Invalid UTF-8 kept its Base64 fallback with no meaningful
size reduction. Normal and ASan clients each passed all 71 protocol checks;
the independent corpus passed 25 Windows cases/725 predicates on each build.
These are measured wire bytes and local correctness checks, not tokenizer,
provider-cost or general latency claims. Final raw evidence is recorded in
[HANDOFF](../HANDOFF.md).

`tests/protocol/test_http.py` exercises actual loopback HTTP/proxy peers,
exact POST bytes, duplicate headers, binary/CP936 responses, HTTP errors,
redirect suppression, output limits, timeout, cancellation, input rejection
and rejection of an untrusted HTTPS certificate. Native C API fixture tests
also supply isolated candidate ports to prove automatic HTTP and SOCKS
fallback without binding or changing the user's 7890/1080 services. Candidate
overrides belong only to that C API fixture; MCP uses 7890/1080. The fixture
proves no fallback after HTTP 403, POST timeout or a drop after POST bytes.
Its optional
`--public-https` performs a bounded read-only HTTPS request to example.com
through the explicitly configured local proxy.

The fixture uses Python's standard library and the existing cryptography
package only to generate its temporary test certificate. These are testing
requirements, not runtime dependencies. The test owns and closes its local
servers, processes, threads and temporary certificate files. Run the normal
MCP entry point after integration:

```text
python tests/protocol/test_http.py build/bin/cdx.exe --pilot
python tests/protocol/test_http.py build/bin/cdx.exe
```

Runtime installation, fresh Codex-client adoption, corporate proxy auth and
deployment-specific certificate/network acceptance are separate gates.
