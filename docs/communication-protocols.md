# Communication protocol scope

The first communication layer is a native C Windows implementation, exposed
through typed MCP/CLI operations. It uses Windows system libraries and the
already-approved static libssh2 dependency. It does not install protocol
clients, run shell command strings or start background protocol daemons.
Linux currently returns explicit `not_supported` for these new operations.

All supported network clients use [default automatic local proxy evaluation](local-proxy.md)
after direct connection failure. Probes are limited to localhost 7890/1080,
and an actual HTTP CONNECT/SOCKS5 handshake is required. Serial COM access
has no IP route and remains local. This changes neither system proxy settings
nor external programs launched through `execute_program`.

## Decision by protocol

| Protocol | Decision | Implementation and first boundary | Further work requires evidence |
|---|---|---|---|
| HTTP / HTTPS | Built in now | WinHTTP asynchronous API; typed methods, headers, UTF-8 request body, proxy, binary response, strict TLS validation, timeout/cancel | Streaming, multipart, client certificates and broader authentication |
| SFTP | Built in now | Existing static libssh2; direct/jump stat, bounded binary read, exclusive create | Large-file streaming, resume, atomic remote publication and directory operations |
| Serial communications | Built in now | Windows COM APIs; passive enumeration/diagnosis, explicit active probe and byte exchange | Physical-device acceptance and device-specific protocols |
| TCP / Telnet | Built in as bounded diagnostic exchange | Winsock; exact bytes and Telnet IAC negotiation | Persistent interactive terminal, authentication scripting and device-specific command sets |
| MQTT | Built in as a stated initial subset | MQTT 3.1.1 clean CONNECT and explicit QoS0 PUBLISH | Authentication, TLS, subscriptions, QoS1/2 and reconnect/persistent sessions |
| FTP / FTPS | Defer a new built-in implementation | No native FTP tool is added in this batch | Confirm actual legacy FTP servers and needed listing/upload/download/passive/TLS behavior before choosing a backend |
| Other protocols | Evaluate when a concrete workflow needs them | Avoid automatically importing every protocol supported by curl | Supported platforms, frequency, installation cost, binary cost and testable acceptance |

The current Windows machine has system curl 8.22.0 with Schannel. Its reported
protocol list includes HTTP(S), FTP(S), MQTT(S) and Telnet, and excludes SFTP.
That observation describes this machine, not every supported Windows image.
An installed executable is not a callable libcurl development library.

For the Windows-first implementation, WinHTTP avoids a new static HTTP/TLS
dependency, COM APIs avoid a serial helper, and existing libssh2 supplies
SFTP. A statically linked libcurl could be a useful later cross-platform
HTTP/FTP backend; it would need a pinned source/version/license, selected
protocols, crypto backend, size measurement and its own approval and tests.
It is not introduced merely because its command-line frontend is familiar.

## Tool contracts

- `http`: see [native HTTP](native-http.md). HTTP status and transport success
  are separate; binary responses retain Base64.
- `network`: see [TCP/Telnet/MQTT](native-network.md). MQTT QoS0 means sent,
  not acknowledged by the broker or accepted by an application. `tls=true`
  returns NOT_SUPPORTED without falling back to plaintext.
- `serial`: see [serial communication](native-serial.md). Listing and default
  diagnosis do not open a physical port. Active open/configure/send is explicit.
- `sftp`: see [native SFTP](native-sftp.md), including unknown partial writes.

Every call owns bounded local resources and returns an explicit result.
Closing a socket or COM handle does not roll back a remote/device mutation.
No mutation is automatically retried after timeout, cancellation or uncertain
completion. Real hardware and deployed services are separate from loopback
and operating-system fixture evidence. Tool availability in a local build
does not establish adoption by an already-connected Codex client.

## Verification

`eng/test.cmd` remains the full Windows gate; `eng/test-linux.sh` preserves
the existing Linux regressions and explicit unsupported-platform behavior.
Focused fixtures use real loopback HTTP/SSH/TCP/Telnet/MQTT and native COM
enumeration plus a task-owned virtual DOS mapping. The COM mapping is not a
physical UART or proof of electrical line behavior. No actual device or
external broker/server mutation is part of this development batch.
