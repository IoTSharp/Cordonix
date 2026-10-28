# Windows process core checks

Build from the repository root with `eng\build-process-core.cmd` using the installed Visual Studio 2026 MSVC C compiler. The script compiles C11 with `/TC /std:c11 /W4 /WX /O2` and places all products under `build/process`.

Run the smallest case first:

```text
build\process\cdx-process-tests.exe --pilot
build\process\cdx-process-tests.exe
```

The tests execute a real C fixture. They verify literal Unicode/empty/quoted/backslash arguments, 300 KB stdin, distinct stdout/stderr and trailing newlines, exit-code policy, timeout, output cap, cancellation, validation without spawn, parent-exit and timeout descendant cleanup, and concurrent pipe isolation.

The fixture's sleeping branch lasts at most eight seconds. Normal requests have a three-second limit; tests that deliberately time out use 100 or 250 ms. The test suite has a 60-second wall-clock budget. Descendant checks wait at most one second for the recorded child process to become signaled after Job accounting reports zero active processes; no process is killed by its name or a guessed PID.

The public API returns captured bytes without assuming a text encoding. Callers own decoding and must call `cdx_result_free` once after reading the result. `cancel_event` is a caller-owned manual-reset Windows event; the core only observes it. If no expected exit codes are supplied, only zero is considered successful.

This is a Windows process-core gate. It does not establish MCP protocol compatibility, document encoding, Linux behavior, packaging, or long-running service readiness.
