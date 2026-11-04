# Execution boundaries

`execute_program` provides a flat argument vector, bounded execution and cleanup of its owned processes. The invoked program still defines how it interprets its arguments. This distinction matters for PowerShell wrappers and browser CLIs.

## Windows PowerShell executable paths

Supply the absolute path to the installed PowerShell 7 `pwsh.exe`. MSI, Microsoft Store execution aliases/package paths and portable installations do not need to match `C:\Program Files\PowerShell\7\pwsh.exe`. Cordonix uses the supplied executable; it does not discover PowerShell or verify its distribution/version for the caller.

Saved scripts use `-NoProfile -File <absolute-script-path>`. The options accepted before `-File` remain `-NoProfile`, `-NoLogo` and `-NonInteractive`; the script must exist. Command strings, encoded commands and Windows PowerShell 5.1 remain rejected before spawn. Arguments after the script path belong to that script and are passed literally.

Version 0.0.4 restricted `pwsh.exe` to the MSI path. The same Store alias and saved script return `validation_error`, `win32_error=5`, `pid=0` on the retained 0.0.4 binary and succeed on 0.0.5. This error is the old policy check, not evidence that Windows denied access to the package. Check `tool_info.version` and `build_revision` on the connected server: replacing the installed version does not replace an existing MCP process. Reconnect the client before judging whether the update fixed that connection.

`eng\test-pwsh-windows.ps1` exercises the real PowerShell host and every distinct `Get-Command pwsh.exe -All` application path, bounded to eight hosts. On the investigated Store installation this covers the package executable, package-specific alias and general WindowsApps alias. It checks Unicode/space-containing script paths and literal arguments through CLI and MCP, then script failure and timeout cleanup through MCP, plus rejection before spawn. These checks run from `eng\test.cmd` and retain fixtures and receipts under `build/`. They do not assert compatibility with every Microsoft Store application.

## Child deadlines and MCP client deadlines

`execute_program.timeout_ms` bounds the child task (default 30000 ms, maximum 1800000 ms). The MCP client's tool-call deadline is separate. A larger child timeout does not increase the client's wait. Leave time for bounded cleanup and delivery of the response before the client's deadline.

In the reported REA/Ghidra session, a request with `timeout_ms=600000` failed at the transport after 300008 ms. The recorded client configuration set `tool_timeout_sec=300`; the child deadline had not elapsed. The session subsequently checked that its original analysis processes had exited before moving the work to a terminal. This is evidence about that client configuration, not a universal five-minute Cordonix limit.

For a finite analysis that may exceed the client deadline, choose a terminal session that can remain attached and be polled, or deliberately configure a sufficient client deadline before starting. A terminal may run `cdx --request <absolute-request-json>` to retain Cordonix's bounded process ownership for jobs within the 30-minute core limit. Keep a persistent MCP server with its lifecycle owner. Do not launch a detached analysis beneath `execute_program`: descendants are still owned by that invocation.

If the client times out without a Cordonix result, treat completion and side effects as unknown. Inspect the original process identity and output artifacts before any retry. A returned `timed_out` with cleanup evidence and a transport timeout with no result are different observations.

## Diagnose the result before changing the invocation

| Observation | Meaning and next step |
|---|---|
| `validation_error`, `pid=0` | Nothing was launched. Check the connected version, absolute paths, actual tool schema and supported arguments. Win32 error 5 can be an explicit policy rejection. |
| `spawn_error` | Startup failed. Inspect `win32_error`, `pid` and `cleanup_complete`; startup may have created a suspended process before a later setup failure. |
| `failed`, known exit code | The program ran and reported failure. Read its captured output; changing the executable path is not a general repair. |
| `timed_out` result | The child deadline expired. Check cleanup and artifacts; work may already have changed files. |
| Client/transport timeout without a result | Outcome is unknown; inspect the original invocation before retrying. |
| `encoding_error` | Inspect the separate `process_status` and retained bytes; decoding failed after execution. |

On Windows, `.cmd`/`.bat` files and command strings are outside this API. Use the underlying native executable and literal argv when the tool provides one; otherwise use the existing terminal/build entry point. PowerShell 5.1, `-Command`, `-EncodedCommand` and unsupported options before `-File` remain explicit rejections. Do not turn a policy rejection into an elevation request or silently route the same command through another interpreter.

## PowerShell array parameters

This request shape does not construct a PowerShell array for a named script parameter:

```json
{
  "program": "C:\\Program Files\\PowerShell\\7\\pwsh.exe",
  "args": ["-NoProfile", "-File", "C:\\work\\Target.ps1",
           "-Arguments", "-NoProfile", "-File", "C:\\work\\Child.ps1"],
  "cwd": "C:\\work",
  "timeout_ms": 10000
}
```

If `Target.ps1` declares a named `[string[]]$Arguments`, the second `-File` can be parsed as a parameter of `Target.ps1`. A reproduced error was `找不到与参数名称 'File' 匹配的参数。` (no parameter named `File`). The same literal argv failed through both Cordonix and .NET `ProcessStartInfo.ArgumentList`; the failure occurred in PowerShell binding before the target body ran.

For a bounded task whose existing wrapper requires an array, save the invocation as a script and construct the array in PowerShell:

```powershell
#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$values = @('-NoProfile', '-File', 'C:\work\Child.ps1', 'two words', '$(literal)')
& 'C:\work\Target.ps1' -Arguments $values
```

Then supply only the entry script and its scalar arguments:

```json
{
  "program": "C:\\Program Files\\PowerShell\\7\\pwsh.exe",
  "args": ["-NoProfile", "-File", "C:\\work\\Invoke-Task.ps1"],
  "cwd": "C:\\work",
  "timeout_ms": 10000,
  "output_limit_bytes": 8192,
  "output_encoding": "utf-8"
}
```

Supply all other parameters required by the actual target script. A target that already supports a JSON file or stdin can use that data interface instead. Do not invent a `-ArgumentsJson` parameter for an unchanged script. Direct flat-argument scripts remain usable according to their own parameter contracts. Do not join argv, add shell quoting, or replace the saved script with an inline `-Command`.

## A successful command can still have descendants cleaned up

Windows execution assigns the suspended root to a Job Object before resuming it. When the root finishes, Cordonix terminates remaining Job processes. Linux applies the equivalent bounded cleanup to its owned process group, with the documented limits for processes that leave that group.

A browser CLI can start a browser or session daemon, print success and exit. Its exit code 0 is compatible with Cordonix subsequently cleaning up the remaining browser processes. A following snapshot command can therefore report that the browser is no longer open. Increasing `timeout_ms` does not extend the lifetime after the root exits; adding another wrapper below Cordonix does not change ownership.

| Workload | Route |
|---|---|
| Compiler, Git, ripgrep, finite script | `execute_program`, with bounded output/time |
| Browser login, persistent Playwright session | Existing browser/domain lifecycle owner, including commands that may start a session implicitly |
| Persistent service or daemon | Existing service manager with its own start/status/stop contract |

Execution results include these additive diagnostic fields:

| Field | Meaning |
|---|---|
| `active_processes_before_cleanup` | Active processes observed in the owned Job/group before cleanup; can include the root and is not a descendant-only count |
| `active_processes_after_cleanup` | Last observed active count after bounded cleanup |
| `cleanup_complete` | Whether the existing cleanup procedure completed and verified its boundary |

These are observations, not a list of killed PIDs or a promise that every observed process was terminated by Cordonix. Read them with the process status and cleanup flag. Zero counts on a request that never launched or on an incomplete observation do not independently prove successful cleanup. `completed` remains the exit-status result, not browser readiness or persistence.

## Decode output without repeating execution

`output_encoding` selects the decoder for both captured streams. It does not set the child's console code page or transcode what the child emits. A PowerShell binding error can happen before the script reaches an encoding setup statement.

A real failure returned `status=encoding_error`, `process_status=failed`, `exit_code=1` and `stderr_base64`. Strict CP936 decoding of the retained bytes recovered the parameter-binding error. No repeat execution was needed to obtain it.

1. Inspect `process_status` and `exit_code` separately from decoding status.
2. Decode retained `stdout_base64`/`stderr_base64` locally using a known encoding and strict fallbacks.
3. Use `output_encoding=cp936` or `gbk` only when that output encoding is known. Do not silently guess or discard undecodable bytes.
4. Retry a command only after establishing whether its side effects occurred. A decoding failure is not permission to repeat a publication or upload.

## Validation and adoption

The shared real-MCP test launches the existing process-tree fixture, observes a successful root with active descendants, then verifies the reported cleanup result. It also checks that invalid UTF-8 preserves the original Base64 bytes and the independent successful process status. Windows native tests already check descendant termination and literal argv, including Unicode, quotes, empty strings and `$()`.

Tool descriptions and additional result fields take effect when the changed executable is built and loaded by the client. Editing this source does not update an already-running MCP server or its cached tool schema. Local validation, release publication and installed-client adoption remain separate gates.
