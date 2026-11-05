# Native Windows screenshots

`screenshot` captures PNG files through Windows GDI and Windows Imaging Component in C11. It needs no PowerShell, `Add-Type`, .NET desktop runtime or third-party image package. Linux returns `not_supported`.

This capability is local development work. It adds the twenty-seventh development MCP tool; an existing installed 0.1.0 connection still exposes its original 24 tools. A build and a fresh connection to that build are separate from release publication and installed-client adoption.

## Find a window

```json
{"operation":"screenshot","arguments":{"action":"list_windows","max_entries":50}}
```

The bounded list contains visible top-level windows, their string `window_handle`, PID, Unicode title, minimized state and physical-pixel rectangle. `truncated=true` means the list is incomplete; increase `max_entries` up to 100. Enumeration also has a deadline, cancellation and an 8192-window scan bound. It does not focus, move, minimize or restore windows.

Handles identify the current window, not a durable application identity. Refresh the list after restarting an application. Supply a decimal or `0x` hexadecimal **string** so pointer precision does not depend on a client's JSON number representation.

## Capture

```json
{"operation":"screenshot","arguments":{"target":"window","window_handle":"123456","path":"C:\\Temp\\window.png","timeout_ms":10000}}
```

Replace the example handle and path. The parent directory must exist, the path must be absolute and end in `.png`, and the destination must be new. Unicode and spaces are supported; existing filesystem rules reject line breaks and code-shaped path values. Existing files are never overwritten. The [active-window example](../examples/screenshot.json) avoids retaining a stale handle; edit its output path before running it with `cdx --request`.

| Target | Required fields | Default method |
| --- | --- | --- |
| `desktop` | `path` | `screen` |
| `region` | `path`, physical-pixel `x`, `y`, `width`, `height` | `screen` |
| `window` | `path`, string `window_handle` | `print_window` |
| `active_window` | `path`; foreground handle is sampled before spawning the capture child | `print_window` |

`screen` uses `BitBlt` to record visible desktop pixels. Other windows can cover a requested window rectangle. Rectangles extending outside the virtual desktop are rejected as `invalid_region`; there is no implicit crop. Desktop capture uses the entire virtual desktop, including monitors with negative coordinates.

`print_window` is only available for window targets. It uses `PrintWindow` with `PW_RENDERFULLCONTENT` (2), requesting the target's own rendering rather than the pixels of an overlapping window. A particular application may return blank or incomplete content. `content_verified=false` is always returned: successful PNG creation does not certify application content. Inspect the saved image with the client's image viewer. The tool never silently switches methods. Minimized targets return `window_minimized`; invalid or closed handles return `window_not_found`.

There is no cursor overlay, UI automation, foreground activation or persistent capture service. Each request captures one frame.

## Bounds and outcomes

The default deadline is 10000 ms, with a maximum of 60000 ms. Dimensions are limited to 16777216 pixels; encoded PNG data is limited to 16 MiB minus 64 bytes reserved for transport overhead. Capturing and encoding run in an owned native child of the same executable, under the existing Job Object/process runner. This also bounds a synchronous `PrintWindow` that waits on an unresponsive application. The child returns image bytes, and never writes the destination.

Only after the child completes does the parent write a same-directory temporary file, flush it and publish it without replacing existing paths. Cancellation and deadline checkpoints run before publication. `written_to_disk=true` confirms publication; `path` is null on an unsuccessful capture. `bytes`, physical dimensions, sampled handle, method, `native_error`, `elapsed_ms` and `cleanup_complete` remain separate. An encoding failure does not become success merely because a desired path was supplied. No capture is retried automatically.

`timeout_ms` also covers bounded checkpoints around local file publication; individual synchronous filesystem calls are not forcibly interrupted. Cancellation arriving after a completed publication does not undo the image. PNG success is independent of visual correctness.

## Regression gates

`eng\test-screenshot.cmd` builds native window/process fixtures and exercises real capture, independent WIC decoding, target pixels behind an overlapping window, Unicode paths, collision preservation, stale handles, invalid paths/regions, cancellation, timeouts and owned-child cleanup. Its blocked-child fixture deliberately waits before capture so process cancellation is deterministic; this does not claim that every application blocks inside `PrintWindow`.

`eng\test.cmd` includes that native gate and the real MCP stdio checks for discovery, capture, collisions and asynchronous cancellation. `eng/test-linux.sh` checks the explicit Linux boundary. Packaging includes this guide and the typed example. Release and installation are separate gates.

## The reported PowerShell error

The November 6, 2026 report from the WinForms demo chat contains `CS1069`: C# compilation could not resolve `System.Drawing.Bitmap` from `System.Drawing.Common`. Loading a DLL with a preceding `Add-Type -Path` did not supply the compilation references for the following `Add-Type -TypeDefinition` call. That route needs explicit compatible `-ReferencedAssemblies`, and terminating error handling before reporting an output file. The error happened before `PrintWindow` was called. Cordonix's native screenshot entry point removes this script/runtime dependency.
