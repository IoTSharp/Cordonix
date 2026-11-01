# Native phone access (experimental)

Status: source development, 2026-11-01. This is not in the published v0.0.4
package or the currently connected 17-tool runtime. The source tree has 19
tools, including the earlier `query_processes` addition and the new `phone`.

Earlier live check (2026-11-01): the ANCS path opened a bounded native
`GattSession` and requests `MaintainConnection` before service discovery. On
the currently paired iPhone, Windows still returned `connection_failed` with
`0x8007048F` at `ancs_service_discovery` and reported
`ble_connected_at_lookup=false`. This records a current Windows BLE/ANCS link
failure; it does not claim that notification sharing is disabled. The tray
process remains available to keep the owner-scoped HFP connection alive while
the phone is brought back into range and its Bluetooth page is opened.

The user subsequently authorized bounded link retries. Initial uncached ANCS
service discovery now makes one attempt plus at most five retries when Windows
reports `ERROR_DEVICE_NOT_CONNECTED` / `GattCommunicationStatus_Unreachable`.
Each retry follows a cancellable 500 ms interval and shares the original request
deadline; a shorter deadline can end the request before all retries. Permission,
protocol and missing-service errors stop immediately. Subscription and attribute
failures are not replayed. Results expose `connection_attempts`,
`connection_retries`, `max_connection_retries` (5) and
`connection_retry_exhausted` (true only when the sixth attempt is unreachable).
`auto_retry` records whether a connection retry actually ran. HFP retention
remains separate. The GUI now allows 60 seconds for the complete notification
request and displays the actual attempt/retry counts.

Live retry acceptance (2026-11-01 12:55 Asia/Shanghai): the first discovery
attempt failed and the second succeeded (`connection_attempts=2`,
`connection_retries=1`, `auto_retry=true`, `connection_retry_exhausted=false`).
Both notification/data subscriptions succeeded. Three existing Apple Phone
events were returned before the collection deadline; no new WeChat event was
observed in this run. Sanitized evidence is retained under
`build/phone-followup-20261101/ancs-retry-result.json` without notification bodies.

Previous successful live notification check (2026-10-31): after authorized pairing removal
and user-confirmed fresh pairing, the second iPhone's ANCS service is accessible.
Both notification/data subscriptions succeeded and an existing Apple Phone
notification (`com.apple.mobilephone`, `preexisting=true`) was read with its
attributes. The user confirmed notification and contact sharing are enabled.
The user also received a WeChat test notification. The receiver now drains
multiple events in one subscription, drops the oldest replayed source events
when its bounded queue is full, reserves a result slot for a new event and
reports the discard count. A live GUI run returned four events, including a
new WeChat event (`com.tencent.xin`, `preexisting=false`), after discarding
511 preexisting events. **WeChat delivery is accepted.** Notification bodies
are not included in the acceptance record.

Earlier, ANCS discovery returned `connection_failed` / `0x8007048F` despite LE
paired/connected metadata; the stored service was absent and its interfaces
disabled. A Bluetooth toggle did not resolve that state, while fresh pairing
did restore subscription and content retrieval. This is not evidence that the
user disabled notification sharing. The GUI retained HFP after re-pairing.
Prior MAP/PBAP checks returned 10 SMS metadata rows and 20 recent-call rows.
Contact transfer succeeded but the current page returned
`preview_status=unsupported_format`; readable contact preview for that page is
still not accepted.

The original immediate `ancs_unavailable` was a separate implementation defect:
classic and LE addresses differed. The backend now resolves the LE node by the
Windows device container using native `cfgmgr32`, without matching a phone by
name or using a .NET bridge. Errors include the failing `stage`, `hresult` and
nullable LE metadata observed at lookup. ANCS commands now carry the required
attribute lengths, assemble fragmented responses, validate the command/UID and
UTF-8, preserve callback bytes under a lock, and honor one cancellation/deadline.
The six packet fixtures and eight Windows queue/collection fixtures are
supplemented by the live WeChat event above; they do not claim notification
history synchronization or a permanent subscription.

The previous Windows `eng/test.cmd` batch follow-up passed (65/65 phone,
23/23 session/notification queue, 106/106 MCP); the optional rg adapter scope was NOT_RUN and Git
passed. MSVC `/analyze /W4 /WX` passed on the changed notification/protocol/UI
sources. Windows AddressSanitizer passed the 65 phone and 23 session/queue fixtures. The live
WeChat event above is the hardware delivery gate; older Linux results below were not rerun for
this follow-up.

The latest 2026-11-01 Windows retry gate passed 66/66 phone checks, 32/32 session/notification
checks, 106/106 MCP checks, 15/15 Git adapter checks, 8/8 PowerShell checks and 2/2
process-query checks. The optional native ripgrep adapter scope remains `NOT_RUN`.

The nine added retry checks cover initial/transient/sixth-attempt success, retry
exhaustion, non-connection failure, deadlines and cancellation. MSVC
`/analyze /W4 /WX` passed the ANCS and GUI sources; Windows AddressSanitizer
passed all 66 phone and 32 session/notification checks.

The user selected a pure C11 implementation. Windows uses Bluetooth device
APIs and Winsock RFCOMM directly, with the system `bthprops` and `ws2_32`
libraries; pairing calls the Windows Runtime C ABI through `runtimeobject`,
and the native GUI uses `user32`, `gdi32`, `comctl32`, `uxtheme` and `dwmapi`. There is no .NET bridge,
HTTP service, shell, Phone Link automation or third-party runtime dependency.
The user confirms pairing and phone permissions. The connection belongs to
the GUI or MCP process that opened it.

## Pairing and connection visibility

On Windows, run the locally built executable with `--phone-ui` to open the
native Chinese pairing window. An optional second argument is the phone's
exact Bluetooth name. The resizable window displays the computer adapter name,
discovered devices, pairing state, retained connection state, access checks and
readable synchronized pages. Detailed diagnostics are behind **查看诊断**.
Adapter/device addresses remain available through the typed API.

1. Open the phone's Bluetooth settings and choose **搜索手机** in Cordonix.
2. Select the intended device, then choose **配对**. Compare the actual code
   in the topmost Cordonix dialog with the phone, and explicitly confirm both.
   The default answer is No; the program never approves a code automatically.
3. After authenticated pairing is read back from Windows, choose
   **连接并保持**. The window shows the phone name and remaining one-hour lease.
4. **断开** closes this owner's connection. **删除配对** confirms the selected
   phone, closes this owner's connection to it, removes the Windows pairing
   and independently reads it back. If the phone still remembers the computer,
   use its Bluetooth settings to forget that computer before pairing again.

`devices` still reads cached entries. `discover` performs a bounded radio
inquiry, including unknown devices. `adapters` exposes the local computer
Bluetooth name; it is the name to check on the phone. Pairing uses `DeviceInformationCustomPairing.PairAsync` through native C ABI
interfaces and an explicit `ConfirmPinMatch`/`DisplayPin` handler. The handler
holds a deferral while the human checks the code, calls `Accept` only after
Yes, and lets Windows complete authentication. Legacy PIN/OOB flows remain
outside this dialog. Discovery/socket binding can select an adapter;
the Windows device-pairing backend selects its association endpoint.

Windows discovery, pairing and removal APIs are synchronous. They run in a
short-lived instance of the same C executable, owned by the existing Job
process boundary with cancellation, deadline and cleanup. No helper service
is installed. Cancelling or timing out a pairing/removal can leave an unknown
mutation outcome; inspect state before another attempt.

A GUI or persistent Windows MCP session retains one HFP control socket.
HFP requests for that phone reuse its negotiated indicator mapping.
SMS/MAP and contacts/PBAP continue using separate bounded connections.
The default lease is one hour, with a maximum of eight hours; an explicit
`connect` to the same phone renews it. Health probes run when the owner is idle,
about every two seconds, and take at most 1.5 seconds. An active phone request
uses its own cancellation/deadline, capped by the lease. Disconnect, expiry,
transport failure or owner exit closes the socket. There is no automatic
reconnect. The GUI closes after eight hours.

Connections are local to their owner: the GUI and each MCP process have
separate sessions and do not share a connection over IPC. One-shot CLI
`connect`/`disconnect` returns `session_required` when actually executed;
use the GUI or an MCP session for retention. `connection_status` reports that
owner's state, not every Bluetooth connection on the computer.

The October 31 hardware run found the adapter and a second iPhone, and
independently confirmed Windows pairing removal. The user confirmed the code
dialog appears in front. The earlier direct authentication callback failed
with response error 31 and final error 1244 after approval, while Windows
Settings pairing succeeded. Replacing that path with native Windows device
pairing produced `Paired` (0), successful human confirmation and independent
Windows readback. The user confirmed fresh pairing succeeded. HFP connection
then succeeded, the GUI displayed the remaining lease, and the user confirmed
the computer appeared connected on the iPhone. Pair diagnostics retain
method, user decision, response result and timing without the code.

## Permissions, synchronization and Windows presentation

Pairing and HFP connection are separate from SMS/MAP and contact/history/PBAP
access. Select the paired phone and choose **检查全部权限** to perform five
sequential read operations: `call_status`, `sms_list`, `contacts`, `call_history`.
The first four use bounded read operations; `app_notifications` opens a separate
ANCS BLE/GATT notification window with a 60-second deadline. The five-step batch
is cancellable and is never repeated automatically. Results belong to the selected device and are cleared
on a device change. They distinguish successful access, explicit permission
denial, pairing required, timeout, cancellation, unsupported and other failure.
A connection/protocol failure is not evidence of a disabled permission switch.
These checks cannot read or change the actual iPhone permission toggles.

**授权说明** explains the phone-side Bluetooth detail page, notification sharing,
contact synchronization and recent-call/contact-group selections when present.
The user must enable the phone's options and accept its access prompts. Options
vary with iOS and the offered service; an absent switch is not assumed granted.

Choose a capability then **同步所选**. The GUI displays at most 20 rows per page
and offers explicit previous/next-page operations; it keeps only the current
page in the window. No database, durable synchronization or complete historical
collection is claimed. Contact previews show the first number and explicitly
report multiple numbers; they are not an automatic send/dial target selector.
Successful empty results remain empty, not a claim of complete synchronization.
After synchronizing an SMS page, **等待短信** waits at most 30 seconds for a change
to that exact page/hash and can be cancelled. A timeout does not revoke the last
successful access result. The taskbar can flash for a list change or observed
incoming call; a list change is not necessarily a newly received SMS. Incoming
call observation runs while the connection owner is idle; active operations can
delay it. `app_notifications` subscribes to the iPhone's Apple Notification Center Service
(ANCS) characteristics for events observed during the subscription, including WeChat
when iOS exposes them. It does not synchronize notification history, create a
permanent subscription, or provide telephone audio.

A request keeps one subscription until `max_entries` complete events (default
20, maximum 50) or the original deadline. Existing notifications count toward
this bound; reaching it does not imply a complete notification-center snapshot
or a cursor for later requests. `event_id`, `flags` and `preexisting` distinguish
arrival/modification/removal and notifications already in the phone's notification
center. Removed events do not request attributes. New subscriptions can replay
existing notifications; there is no durable deduplication or history cursor.
If the phone replays more source events than the bounded collector can retain,
oldest preexisting source events are discarded so a later new event can still
be returned. The result exposes `source_events_dropped` and
`preexisting_events_discarded`; data-attribute queue overflow remains an
explicit error.

`wait_status=event` means at least one complete event was read;
`wait_status=timed_out` with an empty array means subscription succeeded but no
event arrived. Successful `collection_status` is `max_entries` or `timed_out`,
and `notification_count` matches the returned array. A connection/attribute
failure remains an error; if it follows completed events, `partial=true` and
`notifications` preserves those events. The GUI shows partial content alongside
the failure, labels all four event kinds, and flashes only for new arrivals.
The GUI renders the batch when the bounded request finishes, not as a live stream.

The C11 GUI embeds common-controls v6, uses the system message-font family,
scales per monitor, relayouts on resize and observes Windows theme changes.
Windows UISettings supplies the application theme; the title bar, client area,
text and list controls follow light/dark mode. Native buttons retain their
keyboard/automation behavior, with dark-mode custom painting. High contrast
uses system colors and native rendering. No undocumented dark-mode exports,
webview, .NET runtime or third-party UI framework is introduced.

## Capability and evidence

| Action | Native implementation | Current device evidence |
|---|---|---|
| `devices` | Enumerate cached paired/remembered/connected devices | PASS: paired iPhone found |
| `adapters` / `discover` | Adapter identity and bounded discovery | PASS: local adapter and second phone found |
| `pair` | Native Windows device pairing, topmost explicit confirmation and independent readback | PASS: second iPhone freshly paired; user confirmed |
| `unpair` | Remove Windows pairing and independently read back | PASS: selected phone removal verified |
| `connect` / `disconnect` / `connection_status` | Owner-scoped HFP connection, lease and health checks | Real socket lifecycle fixtures PASS; second iPhone retained connection PASS with GUI state and user-confirmed phone display |
| `call_status` | HFP service handshake, negotiated indicator mapping, current call state | PASS: initial idle, active after dialing, idle after hangup |
| `sms_list` | MAP OBEX inbox metadata page, strict UTF-8 and SHA-256 | PASS: latest page contained five metadata entries; no message body read |
| `contacts` | PBAP `telecom/pb.vcf`, requested vCard 3.0 and bounded page | PASS: second-phone read returned contact data; the page remains bounded and is not a complete address-book claim |
| `call_history` | PBAP `telecom/cch.vcf`, combined recent calls with explicit offset/limit | PASS: second-phone read returned recent-call data; the page remains bounded and is not a complete history claim |
| `app_notifications` | Independent ANCS BLE/GATT notification source/control/data characteristics; one bounded batch per subscription | Fresh pairing restored subscription and content retrieval for existing Apple Phone and new WeChat notifications; source-queue overflow is bounded with oldest replayed events discarded |
| `sms_read` / `read_code` | Recheck the inbox snapshot, GET one SMS, parse its bMessage envelope; optionally extract a unique code | Protocol fixtures PASS; real private SMS/OTP NOT_RUN |
| `sms_send` | One UTF-8 bMessage PushMessage, device retry disabled | PASS: one authorized send accepted; recipient receipt confirmed by user, no carrier delivery receipt |
| `dial` / `answer` / `hangup` | HFP commands after checking current state | Dial/hangup PASS: one authorized short call, active/idle readback and user receipt confirmation; answer NOT_RUN |
| `wait_sms` | Poll one inbox page for a changed SHA-256 | Cancellation/change fixtures PASS; live incoming SMS NOT_RUN |
| `wait_call` | Poll negotiated HFP indicators for an incoming call | Cancellation/timeout/incoming fixtures PASS; live incoming call NOT_RUN |
| Audio | No SCO/eSCO or PCM backend | NOT_SUPPORTED, always `audio_available=false` |
| Linux device access | Explicit `not_supported` | Native Linux gate exercises this boundary |

An HFP `ATA` acknowledgement is acceptance of a control command. It does not
establish telephone audio on the computer. Existing audio limitations from
the earlier phone project remain unresolved.

## Request contract

CLI requests use `{"operation":"phone","arguments":{...}}`. MCP uses the
`phone` tool with the same arguments. All values are literal typed fields.

| Field | Contract |
|---|---|
| `action` | One of the 21 actions above |
| `device_address` | Required except for `devices`, `adapters`, `discover`, `connection_status`; exactly 12 hexadecimal digits from enumeration/discovery |
| `radio_address` | Optional local adapter address: exactly 12 hexadecimal digits from `adapters`; discovery/socket binding use it, Windows device pairing selects its association endpoint |
| `connection_ttl_ms` | 1000–28800000, default 3600000; connection lease |
| `recipient` | Required for `dial`/`sms_send`; optional leading `+`, then 3–20 ASCII digits |
| `body` | Required for `sms_send`; nonempty strict UTF-8, at most 1920 bytes; the final encoded message must fit one negotiated OBEX packet |
| `message_handle` | Required for `sms_read`/`read_code`; exactly 16 hexadecimal digits from the selected inbox page |
| `expected_sha256` | Required for `sms_read`/`read_code`/`wait_sms`; SHA-256 returned by `sms_list` |
| `max_entries` | 1–50, default 20; requested device/listing page size or notification-event limit |
| `offset` | 0–65535, default 0; phone listing offset |
| `timeout_ms` | 1–60000, default 15000; one deadline covering connection and all exchanges; discovery requires at least 2000 |
| `dry_run` | Defaults true for send/dial/answer/hangup/pair/unpair/connect/disconnect; explicit false submits the requested action once |

`sms_read`, `read_code` and `wait_sms` must use the same device, `max_entries`
and `offset` as their source `sms_list` request. A body read opens one MAP
connection, obtains the page again, compares its hash, verifies the requested
handle, and then reads the body on that same connection. A changed page returns
`snapshot_changed` before reading the body. This detects a stale page; it does
not establish persistent message-handle identity or a transaction against
concurrent phone changes.

Listings return the original UTF-8 MAP XML or vCard text in `data`, their
declared `format`, a `sha256`, and the requested paging fields. The result is
a page, not a complete contact or message database. Listings additionally return
`preview_status` and bounded `items`: contact/call rows have `name`, `number`
(first number), `number_count`, `datetime` and `direction`; SMS rows expose
available `handle`, `subject`, `datetime`, `sender_name`, `sender_addressing`
and `type`. Supported vCard previews unfold lines and escapes; supported MAP
previews decode named/numeric XML entities. Unsupported display formats return
`preview_status=unsupported_format` with an empty `items` array and preserve
original data and hash. These previews do not replace the original vCard or MAP
contract. The client interprets
contact names, number labels and vCard escapes, then supplies one exact number
to the send/dial operation. No name-to-number guessing or automatic contact
selection is performed.

SMS body parsing accepts one UTF-8 SMS_GSM/SMS_CDMA bMessage from the inbox.
It checks the envelope and declared byte length, and unescapes MAP message
delimiters. MMS, native PDUs and unsupported envelope variants fail explicitly.
OTP extraction supports 4–8 ASCII digits after a supported code label on the
same line, within 64 bytes. Supported labels include 验证码, 校验码, 驗證碼,
`verification code`, `Verification code`, `OTP` and `one-time password`.
No candidate returns `no_match`; different candidates return `ambiguous`.
It returns the code alone and does not fill forms or forward it. This is not
a universal SMS-language parser. Request a sensitive read only when the user
has asked to read that message or retrieve that code.

## Examples

Open the native GUI from the repository after building:

```text
build\\bin\\cdx.exe --phone-ui
```

Inspect adapters and discover nearby devices:

```json
{"operation":"phone","arguments":{"action":"adapters"}}
{"operation":"phone","arguments":{"action":"discover","timeout_ms":15000}}
```

Preview removing a selected pairing (synthetic address):

```json
{"operation":"phone","arguments":{"action":"unpair","device_address":"001122334455","dry_run":true}}
```

The GUI asks before removal. A user-authorized CLI/MCP removal must explicitly
set `dry_run=false`. The phone's own remembered-computer entry may still need
to be removed on the phone.

Find a previously paired phone:

```json
{"operation":"phone","arguments":{"action":"devices","max_entries":10}}
```

Replace the synthetic address below with an address returned by `devices`:

```json
{"operation":"phone","arguments":{"action":"sms_list","device_address":"001122334455","max_entries":20,"offset":0}}
```

Use the returned hash and an actual handle from that page for `sms_read` or
`read_code`. Do not substitute a handle from a different phone or page.

A contact page (use `call_history` for the combined recent-call page):

```json
{"operation":"phone","arguments":{"action":"contacts","device_address":"001122334455","max_entries":20,"offset":0}}
```

Preview a text to an explicitly selected contact number:

```json
{"operation":"phone","arguments":{"action":"sms_send","device_address":"001122334455","recipient":"+15555550123","body":"你好，我稍后联系你。","dry_run":true}}
```

Wait for current-window iPhone application notifications:

```json
{"operation":"phone","arguments":{"action":"app_notifications","device_address":"001122334455","timeout_ms":10000,"dry_run":false}}
```

This uses ANCS and is separate from `sms_list`; it returns one event observed
during the bounded subscription, possibly marked `preexisting` by the phone.

The preview performs no Bluetooth operation. For a user-authorized real send,
submit the actual device, recipient and message with `dry_run=false`. Dial,
answer and hangup follow the same preview convention. Developing or testing
the integration is not an instruction to send a message or place a call.

## Waiting and ownership

The Windows transport uses nonblocking RFCOMM with at most 50 ms between
cancellation/deadline checks. SDP resolution is requested through Windows by
service UUID. MAP/PBAP and one-shot HFP requests own and close their connections;
a session can retain one HFP connection as described above. Cached device
enumeration uses synchronous Windows APIs without a radio inquiry; cancellation
and deadline checks surround those calls, not an OS guarantee of preemption.

OBEX advertises 4096-byte packets, accepts at most 128 GET responses and 32 KiB
of aggregate text. HFP command responses have bounded line and byte counts.
Phone operations use the existing single MCP worker and therefore support
`notifications/cancelled` and session shutdown. Another operation receives
busy while a phone wait is active.

`wait_sms` polls the selected inbox page about once per second until its hash
changes or the operation is interrupted. It returns `event=inbox_changed`;
deletion or a changed page can also trigger it. It does not prove a new SMS
arrived, see messages outside the selected page, or register MAP MNS push.
`wait_call` returns `event=incoming_call` when its HFP status query observes a
ringing call. There is no durable event queue or permanent subscription; an
event between requests or between polls can be missed. Simultaneous permanent
SMS/call notifications need a separately scoped lifecycle implementation.

## Outcomes and acceptance

The preceding permission/UI gate on 2026-10-31 passed Windows `eng/test.cmd` (59/59
phone protocol checks, 15/15 session checks, 106/106 MCP checks and manifest/
control/window smoke), Linux `eng/test-linux.sh` (56/56 phone and 109/109 MCP),
MSVC `/analyze /W4 /WX`, and Linux ASan/UBSan/leak checks. Light and dark native
window rendering were inspected; the dark rendering fixture forces the palette
only in a generated local test executable without changing Windows settings.
Live high-contrast switching and movement between monitors still require a
user/display environment check.

The preceding second-phone read-only check retained the pairing and successfully returned
call state, SMS metadata, contacts and combined recent calls. The independent
ANCS check returned `ancs_unavailable`. Later stage diagnostics identified that
early result as a failed LE-device lookup using the classic address, not evidence
of an absent service or denied permission. No new SMS or call was sent, and the original
one-SMS/one-call authorization was not repeated.


Mutations default to `preview`, with no phone I/O. `accepted` records the
phone's acknowledgement and never implies delivery, connected audio, or a
verified final call state. `submitted=true` means submission was attempted;
failure after that point can be `unknown`. An explicit remote refusal remains
`rejected` or `permission_denied`. No operation automatically retries a
mutation. After `unknown`, inspect the phone before any new attempt.

Protocol fixtures exercise real C packet construction/parsing using fragmented
recorded streams and synthetic numbers/text. They are not Bluetooth or carrier
acceptance. The existing `eng/test.cmd` and `eng/test-linux.sh` include them;
`eng/test-phone.cmd` runs the focused Windows gate. Current source validation
also includes MSVC `/analyze` and Linux ASan/UBSan/leak detection. Native MCP
stdio tests exercise previews and validation without operating a real phone.

Local onboarding gates on 2026-10-31: `eng/test.cmd` PASS (48/48 phone
protocol checks, 15/15 real socket session checks, 104/104 MCP checks and GUI
startup/control/close smoke). WSL Ubuntu `eng/test-linux.sh` PASS (47/47 phone
checks, 108/108 MCP checks). MSVC analysis and Linux ASan/UBSan/leak detection
passed, including the subsequent pairing diagnostics.
The session fixture uses real loopback sockets to exercise the actual retained
transport: explicit disconnect, lease expiry, remote close, cancellation,
caller deadline and owner cleanup. It does not establish Bluetooth hardware
acceptance.

The optional ripgrep adapter portion is `NOT_RUN` in these test environments;
Git gates pass. The connected older Cordonix runtime was used for file/native
tools but does not contain this phone API. Logs are ignored under
`build/phone/onboarding-{windows,linux,analysis,sanitizer}.log`.

Additional hardware acceptance on 2026-10-31 used an independent native
`0.1.0-dev` executable snapshot. One explicitly authorized SMS was accepted,
and one explicitly authorized short call was dialed and hung up. Readback
observed `active` during the call and `idle` afterward. The user confirmed
receipt of both the SMS and the call. The SMS API itself still reports no
carrier delivery receipt. A later contact page contained one complete vCard
and a telephone field; this does not verify the full address book or a
contact-selected send. The first post-call metadata read failed to connect;
a later read-only request succeeded with five entries. No mutation was retried.
Sanitized local evidence is ignored under
`build/phone-hardware-20261031-01/acceptance-sanitized.json`; it supplements the
earlier `build/phone/acceptance.json` and contains no phone number, Bluetooth
address, contact content or SMS metadata.

Remaining gates: prolonged hardware soak and multi-adapter pairing selection;
contact-selected actions; authorized body/code extraction; live incoming
SMS/call detection and cancellation; answering and state readback; telephone
audio; packaged release and fresh installed-client adoption.

Protocol behavior was compared with the user's existing MIT-licensed
`IoTSharp/phone-mcp-bridge` sources, including its distinction between call
control and audio and its findings about MAP folder flags and handle lifetime.
This implementation is C11 and does not invoke that project.
