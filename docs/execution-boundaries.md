# Execution boundaries

`execute_program` provides a flat argument vector, bounded execution and cleanup of its owned processes. The invoked program still defines how it interprets its arguments. This distinction matters for PowerShell wrappers and browser CLIs.

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
