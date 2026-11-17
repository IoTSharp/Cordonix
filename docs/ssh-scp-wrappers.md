# SSH/SCP compatibility wrappers

The repository root contains `ssh-wrapper.cmd` and `scp-wrapper.cmd` for
deployment scripts that need an `ssh.exe`- or `scp.exe`-shaped entry point.
Each wrapper launches the Windows OpenSSH client directly, adds the same key
and host-key options used by the LaneApp helper, forwards the caller's
arguments unchanged, and returns the OpenSSH exit code.

These files are an external compatibility layer. They are not the native
Cordonix `ssh` or `sftp` operations and do not add OpenSSH to the Cordonix
runtime or package. The native operations remain the typed JSON/MCP interface
documented in [native SSH](native-ssh.md) and [native SFTP](native-sftp.md).

## Defaults and overrides

The wrappers use these defaults:

| Setting | Default |
| --- | --- |
| OpenSSH executable | `%SystemRoot%\System32\OpenSSH\ssh.exe` or `scp.exe` |
| Private key | `%USERPROFILE%\.ssh\xjetc_toll_remote_ed25519` |
| Host-key policy | `StrictHostKeyChecking=accept-new` |
| Connection timeout | `10` seconds |
| Prompt policy | `BatchMode=yes` |

Set `CDX_SSH_EXE` or `CDX_SCP_EXE` to an explicit client path when OpenSSH is
installed elsewhere. `scp-wrapper.cmd` accepts `CDX_SSH_EXE` as a fallback for
callers that set one shared client path. Set `CDX_SSH_KEY` to the unquoted path
of the private key when the default key is not the one authorised for the
target. The key file is never read by the wrapper and no key material belongs
in this repository.

Examples from a PowerShell 7 session:

```powershell
$env:CDX_SSH_KEY = 'C:\Users\mysti\.ssh\xjetc_toll_remote_ed25519'
.\ssh-wrapper.cmd -p 22 user@192.0.2.10 'hostname'
.\scp-wrapper.cmd '.\build\payload.tar.gz' user@192.0.2.10:/tmp/payload.tar.gz
```

The wrapper checks that the selected executable and key path exist before
launching. It returns `9009` when the client is missing, `2` when the key is
missing, and otherwise preserves the OpenSSH exit code. It does not add
proxy settings, `known_hosts` relocation, retries or remote command quoting.
The connection timeout and `BatchMode` defaults described above are passed to
OpenSSH; they do not bound a remote command or active file transfer. Callers
must quote paths and remote commands according to normal `cmd.exe`/OpenSSH
rules.

`StrictHostKeyChecking=accept-new` accepts a previously unseen host key and
rejects a changed key. `ConnectTimeout=10` bounds the initial network
connection; it does not bound a remote command or an active file transfer.
`BatchMode=yes` rejects interactive authentication instead of waiting for a
prompt. These fixed automation defaults are deliberately placed before the
forwarded arguments; use the underlying OpenSSH client directly when a
different timeout or interactive authentication is required.
For stronger identity guarantees, use the native Cordonix SSH operation with
an independently verified SHA-256 host-key pin.
