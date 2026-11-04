# Native SSH client

The `ssh` MCP tool and CLI operation use C11 and statically linked libssh2
1.11.1 with Windows CNG. They open sockets and SSH channels in the Cordonix
process. They do not start OpenSSH, PowerShell, .NET, a skill script or a
vendor client. The upstream BSD-3-Clause license is included in packages.
The initial implementation is Windows only; Linux returns `not_supported`.

## Choose a route

| `route` | Connection | Host identity |
|---|---|---|
| `direct` | TCP to `target`, followed by SSH handshake/authentication | Target SSH key pinned before authentication |
| `jump` | SSH to `bastion`, SSH direct-tcpip channel to `target`, second SSH handshake/authentication inside that channel | Bastion and target keys pinned independently |
| `interactive` | SSH to `bastion`, PTY shell, explicit target selection and optional `su - root` | Bastion key pinned; selected asset trust is managed by the bastion |

Endpoints accept literal IPv4 or IPv6 addresses, ports 1–65535, a user and an
authentication source. DNS lookup is outside this initial bounded interface.
There is one bastion per call. All sessions and local sockets close before
return; there is no persistent shell, local listener, key installation or
password fallback after a failed authentication.

`proxy` defaults to `auto`. The first outbound TCP connection (target for
direct SSH, bastion for jump/interactive SSH) uses [local proxy fallback](local-proxy.md)
after a direct connect failure. A connected SSH handshake, host-key or
authentication failure does not start another route. Proxying the first hop
does not change the selected bastion or bypass its audited asset selection.

`action=probe` observes a SHA-256 host-key digest as 64 hexadecimal characters.
Verify that digest through an independent trusted source before using it as
`host_key_sha256` with `action=execute`. A probe is an observation, not trust
establishment. A jump probe needs the bastion's pinned key and authentication
to reach the target. An interactive probe observes only the bastion key.

## Supply authentication

Use `credential` to name a Windows **Generic Credential**. Its `UserName` is
the SSH user, unless the endpoint supplies `user`. Its secret blob must be
UTF-16LE, optionally ending with one NUL, as used by Windows Credential
Manager. The native client reads the entry through `CredReadW`, converts it
strictly and clears secret memory after use. Missing aliases fail explicitly.
The MCP request rejects plaintext `password` fields. No credential is written
or migrated from another product's vault by this operation.

Alternatively supply `user` and an absolute `private_key_path`. The key is
read with UTF-16 Windows file APIs, kept in memory and limited to 64 KiB.
Unencrypted RSA PEM authentication is covered by the native protocol tests.
WinCNG supports RSA and configured ECDSA algorithms; this backend does not
support Ed25519 or promise every encrypted-key format. Password and key-file
authentication are separate choices. SSH agent and MFA challenge flows are
not implemented in this initial version.

## Select an asset through an interactive bastion

Supply literal `ready_prompt`, `password_prompt` and `shell_prompt` values
matching the actual terminal. The single-line `selection` template must
contain `{host}` and can also contain `{port}` and `{user}`. For example,
`connect {user}@{host}:{port}` selects the requested IP rather than a changing
menu index. `selection_prefix` can enter the menu's command mode first.
Prompt matching removes terminal control sequences for login only.

Optional `su_credential`, `su_password_prompt` and `root_prompt` perform the
separate `su - root` exchange. Set `require_root=true` to check `id -u` equals
zero before the user script; failure exits 77. Prompt appearance does not
establish UID. Each password prompt receives at most one response. Unknown
target-host-key confirmation is returned to the caller without answering
`yes`. Supply the prompts and selection syntax for the specific bastion;
there is no built-in product name, asset index, IP mapping or route discovery.

Scripts use LF Bash text, up to 64 KiB. Interactive lines have a 2048-byte
limit for the remote terminal. A random heredoc and completion token isolate
the script and its exit status from terminal echo. The remote host needs
`bash`, and interactive execution also uses `stty` and shell `printf`.
PTY output combines stdout and stderr; ordinary/jump exec keeps them separate.
Interactive targets use the bastion's asset login; a local target private-key
file or target SSH-key pin is rejected rather than silently ignored. Pin the
bastion key and configure its target trust through that system's own controls.

## Interpret the result

`status` distinguishes connection, credential, authentication and host-key
failures from `timed_out`, `cancelled`, `output_limit` and `encoding_error`.
`transport_status` retains the SSH/process result independently of decoding.
`exit_code`, `remote_command_started`, `remote_outcome` and the two
`*_ssh_verified` flags describe separate observations. In particular,
`target_ssh_verified=false` for an interactive asset even after successful
execution, because the SSH peer of this client is the bastion.

`remote_outcome=not_run` means command dispatch was not attempted. `unknown`
means dispatch began but no final result was confirmed. Never retry a
potential mutation solely because of timeout, cancellation or output limits.
`cleanup_complete` concerns local SSH sessions/sockets; it does not prove
termination or rollback of remote processes. UTF-8 is strict by default;
explicit CP936/GBK decoding is supported. Decoding failures retain Base64
bytes and the independent remote result.

Default timeout is 30 seconds, maximum five minutes. Default combined output
budget is 64 KiB, maximum 256 KiB. Nonblocking protocol loops check a shared
deadline, cancellation and a 60000-step limit, with 10 ms backoff. Local
cleanup has an additional two-second bound. Authentication transcripts and
secret material are not included in results.

## Build and validate

`eng/build.cmd` statically compiles the pinned source in `third_party/libssh2`
using `eng/build-ssh.cmd`. No download occurs during builds. The source
archive hash and regeneration command are recorded beside the dependency.
`eng/test.cmd` is the normal C11 regression gate, including the MCP schema
and input-validation checks. Linux's existing gate tests the explicit platform
boundary without linking a different crypto backend.

The separate loopback protocol gate uses an already-installed Python Paramiko
test server and Git Bash to execute real received scripts. This test fixture
is not part of the runtime or a new automatically installed CI dependency:

```text
python tests/protocol/test_ssh.py build/bin/cdx.exe --pilot
python tests/protocol/test_ssh.py build/bin/cdx.exe
```

It exercises real SSH handshakes, key/password authentication, nested SSH,
generic PTY selection/elevation, script bytes, output/encoding, host-key
rejection, timeout and MCP cancellation. Temporary keys, Generic Credentials,
sockets, threads and fixture processes are cleaned up. These local checks
do not establish compatibility with a particular deployed bastion or adoption
by an already-running Codex client.

See the CLI request examples: [probe](../examples/ssh-probe.json),
[jump](../examples/ssh-jump.json), [interactive](../examples/ssh-interactive.json).
The documentation addresses and credential aliases are placeholders.
