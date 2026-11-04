# Native TCP, Telnet and MQTT

The Windows `network` tool implements bounded socket communication in C11
using system Winsock. It does not start an external client or install a
library. Literal IPv4/IPv6 addresses and explicit ports are required. Linux
currently returns `not_supported`.

| Protocol | Actions | Initial scope |
|---|---|---|
| `tcp` | `connect`, `exchange` | Binary data without application-protocol interpretation |
| `telnet` | `connect`, `exchange` | TCP plus IAC escaping and refusal of unsupported option negotiation |
| `mqtt` | `connect`, `publish` | MQTT 3.1.1 clean-session CONNECT and one QoS 0 PUBLISH |

`connect` is the default action. `exchange` can receive without sending.
Sending a nonempty TCP/Telnet payload or publishing MQTT requires explicit
`allow_send=true`. `payload_hex` is even-length hexadecimal and retains every
byte, including NUL, IAC, CRLF and UTF-8 data. Results always return `data_hex`;
the tool does not guess a device's character encoding or command language.

```json
{
  "protocol": "telnet",
  "action": "exchange",
  "host": "192.0.2.10",
  "port": 23,
  "payload_hex": "",
  "read_limit_bytes": 4096,
  "timeout_ms": 5000,
  "idle_timeout_ms": 200
}
```

This example receives an initial terminal response. There is no automatic
login, command, line ending, echo setting, baud probe or credential fallback.
Telnet DO/WILL requests receive WONT/DONT. DONT/WONT are consumed without
creating an option-negotiation loop. Escaped IAC becomes one data byte;
subnegotiation is skipped separately from application output, with a 4096-byte
bound. Truncated controls return `protocol_error`.

TCP/Telnet `exchange` completes on EOF or after the configured idle interval
following received data. `completion=idle` means an observation interval,
not proof that an application message is complete. No data before the overall
deadline remains `timed_out`. Response bytes beyond `read_limit_bytes` return
`output_limit` while retaining the allowed prefix. The default limit is 4096,
maximum 65536. Both outbound and inbound data are bounded to 64 KiB.

## Initial transport and local proxies

`proxy` defaults to `auto`; an empty string has the same meaning. The native
transport first attempts the requested target directly. Only when that initial
connection fails does it probe loopback ports 7890 and 1080 and evaluate their
HTTP CONNECT or SOCKS5 handshake. This reuses the shared C proxy engine; no
external proxy client, shell or dependency is installed. Candidate discovery
and tunnel negotiation use the same absolute deadline and cancellation handle
as the requested operation. Probing an open port alone does not establish that
it speaks the expected proxy protocol.

Use `proxy="direct"` to suppress fallback, or an explicit
`http://127.0.0.1:PORT` / `socks5://127.0.0.1:PORT` URI to use that loopback
tunnel without a direct target attempt. Production MCP requests cannot replace
the fixed automatic candidate ports. Explicit proxy routing retains the same
literal-IP target requirement as the direct network interface. Proxy
authentication and nonlocal proxy URIs are outside this initial scope.

The helper runs once, before the first TCP payload, Telnet negotiation or
MQTT CONNECT. A connected socket's later timeout, protocol error, broker
refusal or uncertain write never triggers route fallback or protocol replay.
`tls=true` still returns `not_supported` before any direct connection or proxy
probe. A proxy tunnel does not add TLS to these protocols.

The result's `proxy` trace reports `status`, `selected_proxy`,
`direct_attempted`, `direct_error`, `native_error`, `probed_ports` with their
observed open states, `tunnel_attempts` and local probe cleanup. A successful
direct connection has `selected_proxy="direct"` and no proxy probes. A
validation/platform/TLS rejection before the helper runs has trace
`status="not_run"`. The top-level `cleanup_complete` also includes closing
the selected connected socket.

MQTT creates a clean session with a generated client ID unless `client_id`
is explicitly supplied. It checks the actual 3.1.1 CONNACK, preserving a broker
refusal code. `publish` requires a nonempty UTF-8 topic without wildcards.
`retain` defaults false and is set only when requested. QoS 0 does not have a
publish acknowledgement: `completion=qos0_sent_not_acknowledged` and
`remote_outcome=unknown` remain true after a successful send. A short-lived
connection sends DISCONNECT before local cleanup.

This initial MQTT implementation does not provide authentication,
subscriptions, QoS 1/2, retained-session recovery, keepalive loops or a
persistent broker connection. `username`/`password` fields are rejected.
TLS for TCP/Telnet/MQTT is not implemented; `tls=true` returns
`not_supported` before opening a socket. There is no silent plaintext
fallback. HTTPS has its separate HTTP client and validation boundary.

Every socket operation shares a deadline, cancellation handle and finite
step budget, with 10 ms readiness waits. Default overall timeout is 10 seconds,
maximum 60 seconds. All local sockets close before return. The result keeps
`connected`, `application_write_attempted`, `payload_bytes_sent`,
`wire_bytes_sent`, `mqtt_connack`, `completion` and `cleanup_complete` separate.
An attempted application write has `remote_outcome=unknown`; these generic
protocols cannot establish whether the remote business operation executed.
`payload_bytes_sent` counts a fully sent application payload; partial wire
transfers remain visible through `wire_bytes_sent`, without pretending to know
how many application bytes the peer consumed. No mutation is automatically
retried after an error, timeout or cancellation.

The actual loopback acceptance suite uses Python standard-library sockets
as independent TCP, Telnet and MQTT peers. It runs against an integrated cdx
MCP server without a new test package:

```text
python tests/protocol/test_network.py build/bin/cdx.exe --pilot
python tests/protocol/test_network.py build/bin/cdx.exe
```

The native harness gate additionally supports `--harness` for independent
module testing. It verifies real wire messages, binary preservation, fragmented
negotiation, broker refusal, timeouts, cancellation and cleanup. These fixtures
do not establish acceptance by a deployed broker or physical device.

Proxy coverage additionally uses real independent loopback HTTP CONNECT and
SOCKS5 peers. It checks exact binary relay, Telnet framing, one MQTT CONNECT /
PUBLISH, route traces, explicit direct mode and lack of replay after a connected
transport fails. Automatic candidate injection belongs only to the shared
proxy engine's native fixture; it is not exposed in the network MCP schema.
