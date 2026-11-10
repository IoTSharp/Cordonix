# VNC screenshots

`vnc_screenshot` captures one complete remote framebuffer as a local PNG. Give
the remote IP and a new output path. The TCP port defaults to **5900**.

```json
{"host":"192.0.2.10","path":"D:\\captures\\server.png"}
```

Without a supplied password, the client selects unauthenticated access when
the server offers it. If the server requires classic VNC authentication, it
reads the current user's saved credential. When none is available, a masked
Windows password dialog asks the user. `prompt_for_password:false` instead
returns `credential_required`, so an agent can ask the user before another
call. No empty password is sent automatically to an authenticated server.

The optional `password` field accepts a password the user already supplied.
Prefer the native dialog so secrets do not enter chat history, command lines
or request files. The tool never echoes the password in its result, summary
or error message. A provided password takes precedence over a saved one.
Wrong passwords return `authentication_failed`; the client does not retry,
delete or replace the old saved password on authentication failure.

## Current-user credentials

After successful VNC authentication, a supplied or prompted password is saved
as a Windows Generic credential with a UTF-16 secret blob. The default target
is `Cordonix/VNC/<canonical-IP>:<port>`, for example
`Cordonix/VNC/192.0.2.10:5900`. An optional `credential` selects another alias.
IPv6 addresses are normalized before forming the default target.

`CRED_PERSIST_LOCAL_MACHINE` retains the entry for **this Windows user** across
logon sessions on the same machine; it does not share the entry with other
users. It can be inspected or removed through Windows Credential Manager.
Passwords are not saved to a Cordonix configuration file. The dialog runs in
an owned native child; its secret goes through a private pipe to the parent,
which clears that buffer after reading it. The child never writes credentials.

Screenshot and credential outcomes are separate:

| Field | Meaning |
| --- | --- |
| `status`, `stage` | Final outcome and the phase that produced it. |
| `written_to_disk`, `path` | Only true and a non-null path confirm PNG publication. |
| `width`, `height`, `bytes` | Remote framebuffer dimensions and published PNG size. |
| `security_type` | RFB security type: 1 None, 2 classic VNC; 0 before selection. |
| `credential` | Alias used or proposed, never the password. |
| `credential_status` | `not_used`, `reused`, `saved`, `invalid`, or `save_failed`. |
| `credential_error` | Windows credential error, separate from image success. |
| `cleanup_complete`, `elapsed_ms` | Local cleanup and elapsed time. |

For example, successful authentication can save a password even if a later
framebuffer read times out. Conversely, a screenshot can succeed while
credential saving fails; `credential_status:save_failed` must be reported.

## Protocol and limits

The Windows implementation uses Winsock, CNG, GDI/WIC and Credential UI. It
does not launch an external VNC viewer or add a runtime dependency. It supports
RFB 3.3, 3.7 and 3.8 with None or classic VNC authentication, requests 32-bit
true-color Raw pixels, and converts them to PNG. Raw is required by the RFB
protocol. Classic VNC uses the first eight Latin-1 password characters; longer
passwords retain this protocol behavior. Non-Latin-1 passwords are rejected
explicitly. TLS/VeNCrypt, RealVNC account authentication and other security
types return `not_supported`. Linux currently returns `not_supported`.

Connections use the shared flag to preserve existing viewers. No keyboard,
pointer or clipboard write messages are sent. Bell and bounded server clipboard
messages are ignored. A PNG is published only after every requested pixel has
arrived; overlapping rectangles cannot make an incomplete frame look complete.

The maximum framebuffer is 16,777,216 pixels, PNG output is below 16 MiB, wire
traffic is bounded to 128 MiB, and collection permits at most 256 server
messages / 4,096 rectangles. The default total deadline is 60 seconds, maximum
300 seconds, including password entry. Cancel or timeout terminates the owned
dialog child and closes the socket. WIC encoding and local file-system calls
are synchronous; cancellation/deadlines are checked before PNG publication.
PNG output uses the existing same-directory temporary-file and no-overwrite
publication path. Existing destination files are rejected before connecting.

`proxy` uses the existing connector: `auto` by default, `direct` to bypass,
or an explicit loopback HTTP/SOCKS5 proxy. Proxy fallback only follows a
transport connection failure; authentication is never retried. Classic VNC
does not encrypt the framebuffer; use the existing trusted operations network
or an independently established secure tunnel when needed.

## Validation

Run `python tests/acceptance/vnc_screenshot.py --executable build/bin/cdx.exe --output build/vnc-tests.json`.
The stdlib fixture uses real loopback RFB
connections through MCP, an independent DES response vector, decoded PNG
pixel checks, disposable Windows credentials and timeout/cancellation checks.
All sockets, processes, fixture files and test credentials are reclaimed.

On an interactive Windows desktop, add `--ui` to exercise the actual masked
dialog: accept a synthetic password, dismiss, cancel through MCP and time out.
Only windows owned by the fixture's child process are addressed. CI does not
require an interactive desktop. `--pilot` runs the smallest no-password case.
With unused loopback port 5900, `--default-port` also verifies an actual
connection with the port field omitted.

Native tests, MCP interoperability, real remote-server acceptance and adoption
by an installed client are distinct. Current evidence belongs in HANDOFF.md.

Protocol reference: [RFC 6143](https://www.rfc-editor.org/rfc/rfc6143).
