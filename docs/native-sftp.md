# Native SFTP

The `sftp` MCP/CLI tool reuses the approved static libssh2/Windows CNG client,
its credential aliases and host-key pins. It adds no runtime dependency.
The SSH first hop also shares default automatic local-proxy fallback; see
[local proxy routing](local-proxy.md). Failed authentication, server rejection
or an uncertain file write does not trigger another connection or mutation.
Choose `direct` or `jump` transport using the same typed `target`/`bastion`
objects as `ssh`. A PTY menu does not carry an SFTP subsystem, so the
interactive route is rejected. SFTP uses the authenticated user's filesystem
rights; it cannot perform `su` elevation.

`action=stat` returns size and permissions. `action=read` returns a bounded
range; metadata that was not queried or not provided by the server is null.
Binary reads return
exact `data_hex`, with optional `offset` and `read_bytes` (at most
256 KiB). It does not create a local file. `action=write` accepts at most
64 KiB of `data_hex` and requires `allow_write=true`. This initial writer uses
exclusive creation and never replaces an existing path. It reports the
acknowledged `transferred_bytes`; it does not claim atomic remote publication,
fsync, resume, directory recursion or transfer of an arbitrarily large file.

The operation shares SSH's deadline, cancellation, independent host-key pins
and local session cleanup. An interrupted write can leave a partial remote
file; `remote_outcome=unknown` must be reconciled before another action. A
server status rejection is reported as `sftp_error` with its numeric status.
There is no automatic retry or external SFTP client. Linux currently returns
`not_supported`.

The loopback fixture in `tests/protocol/test_sftp.py` exercises the actual SSH
SFTP subsystem, binary/Unicode bytes, bounded reads, exclusive writes,
existing-file protection and server errors. Test dependency Paramiko was
already installed and is not shipped with the runtime. Real production
servers, filesystem metadata variations and very large transfers remain
separate acceptance work.
