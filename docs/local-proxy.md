# Local proxy selection

The native C proxy transport supports direct TCP, HTTP CONNECT and SOCKS5
without external programs, additional libraries or system proxy changes.
It is shared by network clients so protocol layers can obtain one connected
socket before dispatching business data.

The request's `proxy` value is empty/`auto` by default. It can also be
`direct`, `http://127.0.0.1:<port>` or `socks5://127.0.0.1:<port>`.
Explicit proxy URIs choose their named protocol only. Authentication in proxy
URIs, remote proxy addresses, paths and ambiguous port syntax are rejected.

Automatic selection first tries the explicit destination IP directly. Only
after a connection failure does it probe local `127.0.0.1:7890` and
`127.0.0.1:1080`. Both endpoints are observed before selection. An open port
is a candidate, not proof of a proxy. The first candidate prefers HTTP CONNECT;
the second prefers SOCKS5. An unrecognized handshake can try the other protocol
at that same endpoint. A recognized proxy refusal retains that failure and
does not reinterpret the same service as another protocol. There are at most
two probes and four full tunnel attempts, all within the original deadline.

HTTP CONNECT validates the status line and consumes only response headers,
leaving tunnel data untouched. A 2xx response establishes the tunnel; refusal
codes such as 407 remain visible. SOCKS5 negotiates no authentication, validates
the reply and consumes the bound-address structure. Destination IPv4, IPv6
and ASCII hostnames are supported. Hostnames use SOCKS5 remote DNS; the helper
does not perform unbounded local name resolution. CONNECT brackets IPv6
authorities correctly.

The C API in `include/cdx/proxy.h` accepts an absolute GetTickCount64 deadline
and cancellation handle. It returns a nonblocking Winsock socket, selected
route, native/direct errors, observed candidate ports, open flags and tunnel
attempt count. `UINTPTR_MAX` means there is no transferred socket. The caller
owns Winsock initialization, the successful socket and eventual cleanup.
Failed probes and tunnel attempts close their own sockets before returning.
The helper has no process-global cached route.

`skip_direct=true` is for a caller such as HTTP that has already attempted
direct connectivity and established that no business request was sent. It
starts proxy evaluation without repeating direct access and permits an ASCII
hostname for the tunnel. The protocol layer must preserve its own direct
attempt observations and prevent replay after a possibly dispatched mutation.
The proxy helper itself sends only CONNECT/SOCKS negotiation, never business
payloads, and never retries a dispatched application operation.

The direct attempt gets a bounded portion of the original budget, up to one
second. Local probes get at most 200 ms each; each tunnel negotiation gets a
bounded remaining-budget portion, up to three seconds. Socket waits use 10 ms
readiness intervals, a 60000-step cap and cancellation checks. `timed_out` and
`cancelled` remain separate from `proxy_unavailable`, `proxy_error` and
`connection_error`. There is no machine-wide environment or OS proxy mutation.
Linux currently returns `not_supported` for this helper.

## Client integration

SSH/SFTP use this transport for the first outbound hop: target for direct
connections, bastion for jump/menu connections. It never bypasses a selected
bastion, retries failed target authentication/host keys, or replays a file write.
TCP/Telnet/MQTT choose a socket before protocol dispatch and never select a
second route after CONNECT/PUBLISH or application bytes have been sent.

HTTP first uses native WinHTTP, including its DNS support. A fallback requires
a network connect failure before any connected, secure-failure or request-send
phase evidence; final callback snapshots are checked again after closure.
The original deadline applies across both attempts. HTTP status codes such as
403/404 and target TLS/authentication failures are not connectivity failures.

For a native CONNECT/SOCKS tunnel, HTTP creates a single-call loopback bridge
to feed WinHTTP while retaining the original URL, TLS SNI and system certificate
verification. The bridge is bounded, accepts one client for the known target,
and closes its sockets/thread before a successful cleanup report. It does not
install a service or change machine proxy, environment or trust settings.

WinHTTP bypasses proxying for loopback target URLs. Auto keeps localhost,
127/8 and ::1 target URLs direct; explicit proxying of these targets returns
`not_supported` before a misleading tunnel is created. This restriction concerns
the destination URL, not the local 127.0.0.1 proxy endpoint. Existing explicit
named HTTP proxy configuration remains supported for non-loopback targets.
See [HTTP details](native-http.md). Local serial COM access has no IP route.

Optional native-library candidate ports (maximum two) support isolated tests
with ephemeral loopback listeners. They are not MCP configuration: default
MCP auto endpoints remain 7890 and 1080.

`tests/protocol/test_proxy.py` exercises the actual native transport through a
test DLL, with independent Python standard-library HTTP/SOCKS peers. Tests
cover direct/no-probe behavior, both probes, swapped endpoint protocols,
unrelated services, refused/malformed handshakes, remote DNS/IPv6, deadline,
cancellation and byte-preserving post-handshake traffic. All fixtures are
local and bounded; public proxy or deployed-host acceptance is separate.
