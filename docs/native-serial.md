# Native serial communication

The `serial` MCP/CLI operation uses the Windows COM, registry and DOS-device
APIs from C11. It requires no Python, shell, vendor SDK, terminal executable
or newly installed library. Linux explicitly returns `not_supported` in this
initial backend. Only literal `COM1` through `COM65535` names are accepted;
filesystem paths and command-shaped values are rejected.

## Choose an operation

| Action | Behavior |
|---|---|
| `list` | Enumerate the serial-device registry, with a result cap; never open a port |
| `diagnose` | Read the DOS-device mapping and available default configuration; never open a port by default |
| `diagnose`, `open_probe=true` | Open the explicit port exclusively and read its active DCB/modem status |
| `exchange` | Open the explicit port, optionally configure line format, write explicitly authorized bytes and collect bounded received bytes |

Passive diagnostics report active settings and the busy state as unknown.
Opening a serial device can itself affect hardware lines through its driver;
`open_probe=true` is an explicit opt-in to that boundary. Active diagnosis
does not call `SetCommState`, `ClearCommError`, `EscapeCommFunction`, transmit,
toggle DTR/RTS or issue BREAK. A sharing violation is `busy`; Windows access
denied is `access_denied_or_busy` because that error alone cannot distinguish
an exclusive opener from insufficient permission. A missing mapping or port
is `not_found` with availability `unavailable`.

## Exchange bytes

```json
{
  "operation": "serial",
  "arguments": {
    "action": "exchange",
    "port": "COM7",
    "allow_send": true,
    "tx_hex": "010300000002c40b",
    "configure": true,
    "baud_rate": 9600,
    "data_bits": 8,
    "parity": "none",
    "stop_bits": 1,
    "read_max_bytes": 256,
    "read_idle_ms": 100,
    "timeout_ms": 2000
  }
}
```

This is a shape example; it does not authorize sending those bytes to a real
device. `tx_hex` is contiguous hexadecimal with an even number of digits, up
to 64 KiB. A nonempty payload requires `allow_send=true`. There is no automatic
AT command, baud scanning, protocol detection, BREAK, reset or retransmission.

Line-format fields require `configure=true`. That call changes only baud,
data bits, parity, stop bits and their required binary/parity flags, preserving
the DTR/RTS and flow-control modes read from the device. Otherwise the current
settings are used. `active_settings` reports those modes so the caller can
interpret flow-related behavior. Configuring a port is an explicit device
operation; the tool does not claim to restore or persist a system default.
Windows permits 1.5 stop bits only with five data bits and two stop bits only
with more than five data bits; invalid combinations fail before opening.

`read_max_bytes` is a capture cap, not a promised response length. Zero skips
the receive phase. The default cap is 1024 bytes; the maximum is 64 KiB.
The receive phase stops at that cap, the idle limit, cancellation or the total
deadline. No received data before the idle limit returns `no_data`. A shorter
capture that ended normally keeps `status=completed`, `stop_reason=idle` and
its exact `rx_hex`. The exchange acknowledges communication error flags with
`ClearCommError` while inspecting the input queue and reports their accumulated
bit mask in `communication_errors`.

## Result and resource boundaries

`bytes_written` and `bytes_read` count confirmed transferred bytes. `send_outcome`
is `known` only when the driver completed the whole requested write, `unknown`
after an incomplete attempted write, or `not_run` without a write attempt.
`device_outcome` remains `unknown` after any transmission: a COM write does not
prove that the device accepted or applied an application command. Never retry
a potentially mutating payload merely to recover a missing response.

Timeouts and cancellation retain confirmed partial bytes. Operations use
overlapped I/O and bounded waits, with a total timeout of 1–60000 ms, a receive
idle limit of 1–5000 ms, finite poll counts and cancellation checks. Cancellation
cleanup has an additional one-second bound. All successfully completed I/O
events and port handles close before return. If a serial driver does not
acknowledge cancellation, `cleanup_complete=false` explicitly reports that
case; the pending I/O storage stays alive until process exit, as Windows may
still write to it. This is not a guarantee about malfunctioning third-party
driver behavior.

Timeout/cancellation checks surround synchronous Windows metadata, open and
configuration calls; an individual driver call is not forcibly preempted.
The bounded polling guarantee applies to the tool's own loops and overlapped
transfers, not to a guarantee that every third-party driver call returns.

## Validation

The focused native C suite exercises the actual registry/DOS-device APIs,
schema and request parser, cancellation before open, missing ports and a
task-owned regular-file DOS mapping for passive diagnosis, exclusive-use and
non-serial I/O errors. It transmits no physical serial data and removes its
exact test mapping and file. Physical UART/USB serial traffic, real line-format
changes, active modem lines and driver cancellation under hardware load remain
`NOT_RUN` until a specific device or authorized virtual COM fixture is supplied.
