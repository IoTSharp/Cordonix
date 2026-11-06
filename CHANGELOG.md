# Cordonix Changelog

## Unreleased

- Added five evidence-selected Windows/Linux tools: native `data_query` for JSON/JSONL/CSV projection, filtering, counting and grouping; external-rg `find_files` with complete-scan sorting and bounded offset pages; streaming `hash_file`, no-replace `copy_file` and same-filesystem `move_file`. Development now has 32 tools. Original numeric tokens, strict decoding, partial results, cancellation and output budgets remain explicit. Linux copy publication requires hard links; atomic no-replace move requires filesystem support for `renameat2`. No new core dependency is introduced. Contracts, platform limits and validation are recorded in the capability-gap batch document.

- Implemented the CDX-11E minimum MCP result mapping on Windows/POSIX stdio: malformed or unknown `tools/call` requests return JSON-RPC `-32602`, and deterministic domain failures add outer `isError: true` while preserving the inner status JSON. Partial, preview, no-match, unsupported-platform and unknown-outcome results remain text results. Added real stdio coverage for the envelope and error mapping; modern client compatibility remains open.
- Added Windows native `screenshot` capture and bounded window discovery to CLI/MCP, bringing the development surface to 27 tools. Desktop/region/window/foreground targets use explicit visible-pixel or `PrintWindow` semantics, Unicode new-file PNG output, bounded owned-child execution, cancellation and independent write/cleanup results. Uses system GDI/WIC without PowerShell or .NET; Linux returns `not_supported`. Added native pixel/PNG/process fixtures, real MCP checks and packaged documentation/example; installed-client adoption remains separate.

- Added optional HTTP `representation=both|text|base64`; the existing default, decoding status and outcome fields remain. Text mode omits duplicate Base64 only after strict decoding succeeds; binary/invalid text retains raw bytes without replay.
- Added Windows/Linux `read_files` with 1–32 validated inputs, common deadline/cancellation, independent returned-text and escaped-output budgets, preserved per-item results and explicit `not_run`. Reuses strict single-file readers and both asynchronous MCP dispatchers; opens validated original paths to preserve POSIX symlink/`..` traversal. Development now has 26 tools.
- Added independent three-round CLI/MCP acceptance, literal/encoding/hash comparisons, failure and maximum-budget counterexamples, real deadline/cancellation, POSIX path traversal and raw-response archives. Byte/call measurements remain separate from unmeasured tokens, provider cost, peak RSS and physical IO.
- Packaged the missing linked user/project documentation and batch example on both platforms; package gates now check relative Markdown targets. Audited official MCP specifications while retaining the existing legacy handshake; error mapping and modern-client compatibility remain open.
- Documented the authorized three-stage implementation program, evidence-based selection rules, parallel ownership and separate closure gates. Background jobs and design/office extensions remain planned.

- Added native Windows/POSIX `create_directory` to the C API, CLI and cancellable MCP worker. Explicit `parents`/`exist_ok` options replace ensure-directory plus absolute-path readback; results retain leaf creation and total created counts on failure. Existing files are preserved, recursive creation has a 256-directory preflight bound, and deadline/cancellation states remain explicit.
- Normalized Windows parent candidates before probing their directory entries, preserving drive roots. This fixes the file-ancestor classification exposed by the first Windows Server 2022 CI run; native fixture failures now include the status, native error and created count.
- Added native filesystem and real MCP regression coverage, including Unicode/spaces, existing directories, missing parents, file collisions, lexical normalization, invalid inputs, pre-cancellation and fixture cleanup. The development surface has 25 tools; publication and installed-client adoption are deferred to the next release.

## 0.1.0 - 2026-11-04

- Published signed Windows/Linux archives on GitHub and Gitee from clean source `6bf1e14`. All tagged gates pass; 12 uploaded assets match across hosts, four independent signature checks and four tamper rejections pass, and downloaded binaries expose 24 MCP tools. The public Windows executable passes 108 protocol checks. WinGet update PR #470229 is submitted; community acceptance remains separate.

- Integrated the v0.0.5 phone, PowerShell and process-query contract with the new communication tools and call summaries: 24 tools in total. Kept the separate native C process-query library and its checks, and retained exact original command-line/creation-time protocol regressions without changing the released MCP fields.

- Made `proxy=auto` the default for built-in HTTP/HTTPS, SSH/SFTP and TCP/Telnet/MQTT connections. Direct network connection failures trigger bounded local 7890/1080 probes and actual HTTP CONNECT/SOCKS5 negotiation; explicit direct/proxy selection remains available.
- Added native per-call HTTP bridging for SOCKS5/CONNECT tunnels while preserving original TLS hostname/system trust. WinHTTP loopback destination URLs remain direct in auto and reject explicit proxying as unsupported. Serial access stays local.
- Added shared route traces, absolute-deadline/cancellation handling and pre-send HTTP phase checks. HTTP statuses, target trust/authentication failures and uncertain dispatched operations do not trigger replay or machine proxy changes.

- Added native `http`, `network`, `serial` and `sftp` MCP/CLI tools. HTTP(S) uses WinHTTP; TCP/Telnet and MQTT CONNECT/QoS0 use Winsock; serial uses Windows COM APIs; SFTP reuses the approved static libssh2. No additional runtime package is required.
- Added real HTTP/proxy/TLS, TCP/Telnet/MQTT, native serial API and SFTP fixtures with independent mutation/outcome/cleanup states. New backends return `not_supported` on Linux; physical serial acceptance is separate.
- Fixed demonstrated SFTP interrupted-open result classification and handle cleanup. Lost mutation replies remain UNKNOWN; per-call libssh2 allocation accounting verifies local cleanup, including cancellation after open.
- Documented protocol decisions, focused limits and deferred FTP/FTPS, and packaged typed request examples.

- Added deterministic C11 per-call summaries in MCP result `_meta["cordonix/summary"]` and opt-in standard progress messages. Existing program/argv/path/range fields describe program purpose and operation targets without extra model calls, input fields or summary text in model-visible content. Client rendering remains a separate gate.
- Added a native C `ssh` MCP/CLI client using statically linked libssh2 1.11.1 and Windows CNG: direct IP, direct-tcpip SSH jump and configurable PTY bastion target selection, with no external SSH/product helper process.
- Added Windows Generic Credential and supported in-memory private-key authentication, independent host-key pins, optional interactive root elevation/UID guard, bounded cancellation/output and explicit remote UNKNOWN outcomes. Linux returns `not_supported`.
- Added a real loopback SSH protocol acceptance fixture, request examples, native SSH guidance and packaged BSD-3-Clause notices. Release, installed-client adoption and deployed-bastion acceptance remain separate.
- Fixed a reproduced MCP worker race where a sequential request arriving after the prior response could still receive busy. The dispatch loop now joins the completed worker before emitting its final response, preserving progress notifications and cancellation.

- Clarified the existing native `Get-Content -LiteralPath | Select-Object -Skip M -First N` equivalent in MCP read-tool descriptions, parameter guidance and bilingual examples: `start_line=M+1`, `line_count=N`.
- Clarified Windows executable/script routing and child-versus-client deadlines in the MCP execution schema and bilingual guidance. Expanded the PowerShell gate to all discovered application paths (at most eight), including both Store aliases, with real script-failure and timeout-cleanup checks. The existing 0.0.5 path fix remains unchanged.

- Added WinGet manifest templates for `IoTSharp.Cordonix`, generation from verified published Windows archives, and a follow-up workflow that submits community repository update PRs when `WINGET_TOKEN` is configured. Microsoft validation, review and source indexing remain separate from GitHub/Gitee release publication.

## 0.0.5 - 2026-11-02

- Published signed Windows/Linux archives on GitHub/Gitee after all tagged gates passed. This machine's existing hourly task naturally upgraded 0.0.4 to 0.0.5 from Gitee at 03:27:03 Asia/Shanghai, with exit 0 and matching release SHA-256. A fresh MCP connection through the unchanged stable launcher returns all 19 tools and the clean release revision; existing connections keep their earlier processes.

- Added user-requested bounded ANCS link retries: initial uncached service discovery retries an unreachable connection at most five times after the first attempt, with a cancellable 500 ms interval and the original request deadline. Results report attempt/retry counts and exhaustion; the GUI displays them and permits 60 seconds for a notification request.
- Live retry acceptance on 2026-11-01 succeeded on attempt 2 after one retry: both ANCS subscriptions opened and three existing Apple Phone notifications were read. No new WeChat event was observed during that request.
- Added a bounded native Windows `GattSession` keep-alive before ANCS service discovery. The 2026-11-01 live check still reports `connection_failed`/`0x8007048F` at `ancs_service_discovery` for the current paired iPhone, so the result remains a BLE link diagnostic rather than a permission verdict; no automatic reconnect was added.
- Fixed ANCS collection stopping after the first existing notification: one subscription now receives up to `max_entries` events under the original deadline, with `collection_status`, `notification_count` and explicit partial results on failure. The native GUI preserves partial content, distinguishes new/updated/existing/removed events and flashes only for new arrivals. Eight Windows FIFO/collection regression checks cover batching, bounds, timeout, cancellation and partial failures.
- Fixed ANCS source-queue overflow when a phone replays many existing notifications during subscription. The bounded source queue now drops the oldest preexisting events, keeps data-attribute overflow explicit and reserves one result slot for a new event; the result reports `source_events_dropped` and `preexisting_events_discarded`. A live GUI run captured a new WeChat event (`com.tencent.xin`, `preexisting=false`) after discarding 511 existing events.
- Fixed ANCS lookup when a phone's classic and LE addresses differ by resolving Windows device-container membership. Corrected ANCS attribute request lengths, fragmented UTF-8 response parsing, callback buffering and cancellation/deadline handling. GUI diagnostics include the failing stage. Fresh user-confirmed pairing resolved the earlier `0x8007048F` service failure; subscription and content retrieval passed for existing Apple Phone and new WeChat notifications.

- Added `call_history` PBAP combined-call pages and bounded display `items`/`preview_status` alongside original hashed vCard/MAP data. Reworked the native phone window with embedded common-controls v6, system fonts, per-monitor DPI, light/dark and high-contrast handling, per-feature access checks, iPhone permission guidance, paged previews and cancellable SMS waiting. Pairing/connection is never reported as blanket authorization; added a separate ANCS BLE/GATT `app_notifications` action for current-window application notifications such as WeChat; notification history, permanent subscriptions and audio remain outside the contract.
- Added experimental `phone` CLI/MCP operations in pure C11: Windows native RFCOMM, MAP SMS and bounded inbox-change waits, PBAP contact pages, HFP call control/incoming-call waits and snapshot-checked SMS/verification-code reads. No .NET bridge or new third-party runtime is required. Mutations preview by default; uncertain submission outcomes remain `unknown` without automatic retry.
- Real iPhone tests reused an existing Windows pairing: one authorized SMS and short outgoing call were confirmed received by the user, with active/idle state readback around hangup. A later PBAP page returned one contact with a telephone field and MAP returned five metadata entries. Incoming events, answering, private message/code reads and audio are not claimed as accepted. Linux device operations return `not_supported`; phone support remains experimental.
- Added native Windows adapter discovery, `pair`/`unpair` and a Chinese `--phone-ui` window. Human code confirmation is topmost and defaults to No; pairing deletion asks for the selected phone and independently reads back Windows state. Discovery/removal and foreground confirmation passed hardware checks. Fixed post-confirmation authentication failure by using the Windows device-pairing C ABI; the second iPhone now freshly pairs and retains an HFP connection, with the connected display confirmed on both sides. Response-stage diagnostics omit pairing codes.
- Added owner-scoped HFP `connect`/`disconnect`/`connection_status`, a bounded connection lease, health checks and cleanup on expiry/disconnect/exit. GUI and MCP sessions own separate sockets; MAP/PBAP remain per-operation. Real socket lifecycle checks cover cancellation/deadlines, remote close and ownership cleanup; native GUI smoke joins the Windows final gate. No automatic reconnect, installed helper service or computer audio is introduced.
- Included phone protocol fixtures in both native gates and added three real MCP preview/validation checks. Corrected the existing process-query test to accept a documented byte-capped result only when `truncated=true`; path, command-line and creation-time fields remain best-effort and may be `null` for inaccessible processes.

- Removed the Windows `pwsh.exe` MSI-path restriction so saved PowerShell 7 scripts can use Microsoft Store or portable installations. Existing `-File`, absolute script-path and forbidden-shell/command checks remain; real CLI/MCP PowerShell regression checks join `eng\test.cmd`.
- Added native read-only Windows `query_processes` to the C API, CLI and asynchronous MCP worker, preserving original command lines and exact creation times with explicit partial/timeout/cancellation/output-limit states. Linux returns `not_supported` without an interpreter fallback.
- Added focused native and MCP checks for the new operation: 2/2 native checks and 2/2 MCP checks on each platform. Windows and Linux full gates keep the POSIX `not_supported` boundary explicit; release packaging and installed-client adoption remain open.
- Made the Windows test harness locate Visual Studio Professional installations, preserve Git/system tools after `VsDevCmd`, fall back to the available PowerShell 7 executable and report a missing ripgrep installation as `NOT_RUN` while still running the Git adapter checks.

## 0.0.4 - 2026-10-30

- Added GitHub Actions OIDC artifact provenance for both release archives through the official, commit-pinned attestation action. Sigstore bundles accompany the immutable archive assets on GitHub and Gitee.
- Independent verification binds the artifact digest, repository, workflow, source commit and release tag. This is artifact provenance signing, not Windows Authenticode.

## 0.0.3 - 2026-10-30

- Added per-child environment overrides, including deletion and empty values, without mutating the parent. Real process checks cover Unicode, platform name matching and simultaneous child isolation.
- Added parsed external ripgrep JSON and Git porcelain v2 NUL adapters, with raw byte preservation, rename paths and distinct no-match/failure/limit states.
- Added independent Linux client acceptance across ext4/tmpfs/9p, a 60-round/600-second finite soak, and explicit crash-before-commit disk readback.
- Added actual Windows/Linux natural scheduler acceptance scripts and failure/cleanup regressions; first natural invocations have passed while hourly recurrence is recorded separately.
- Added embedded build revision/dirty readback, clean-release provenance checks, official SPDX 2.3 schema validation and same-environment compiler reproducibility gates.
- Added a fixed 128 KiB fresh-versus-memory read benchmark. OS I/O counters include pipes; physical disk I/O and model token counts remain unmeasured.

- Clarified flat argv versus PowerShell named array binding, call-scoped cleanup for persistent browser launchers, and output decoding versus process status in MCP tool descriptions and bilingual usage guidance.
- Exposed existing active-process observations before and after cleanup through `execute_program`; retained process ownership and exit-status behavior. Added real MCP coverage for successful-root descendant cleanup and independent encoding/process results.

- Added native `create_file` to CLI/MCP and the public C API: strict declared encoding, same-directory temporary writes, no replacement of existing paths, cancellation and explicit write/cleanup results.
- Added 20 native create-file checks on each platform and eight shared real MCP checks; the original create-file batch passed 225 Windows and 211 Linux checks.
- Added a separate real ext4 MCP gate and CI step for invalid UTF-8 directory entries, owner/group/mode and external permission changes. ACL/xattr non-preservation remains explicit.
- Verified real Windows Task Scheduler registration, one manually triggered hourly-update action, completion and removal in an isolated public-v0.0.2 installation (seven checks). Natural periodic firing and Linux scheduler registration remain separate.

## 0.0.2 - 2026-10-29

- Published Windows x64 ZIP and Linux x64 tar.gz on GitHub and Gitee from `c41ab22` through a fully passing tagged workflow.
- Added verified bootstrap installation, hourly user update support and a copyable AI setup prompt; both platforms passed real 0.0.1-to-0.0.2 package upgrade checks.
- Fixed original annotated-tag mirroring, Linux checksum paths/SPDX metadata and the reserved Windows encoding fixture filename.

## 0.0.1 - 2026-10-29

Initial native runtime release, published on both platforms from `586a240`. The detailed implementation and follow-up additions are listed below; the install/update and mirror fixes are included in 0.0.2.

### Added

- Windows and Linux bootstrap installers with exact release-asset selection, SHA-256 and native runtime verification, bounded GitHub/Gitee fallback, preserved previous versions and stable MCP launch paths.
- Optional hourly user updates through PowerShell 7 / Task Scheduler on Windows and systemd user service/timer on Linux; isolated HTTP, rollback and scheduler-definition gates.
- A recovery workflow to mirror an existing GitHub release to Gitee without rebuilding or overwriting published assets.

- Initial Cordonix product charter, short form `cdx`, roadmap identifiers and eight agent rules.
- Native C11 CLI JSON request mode and persistent MCP stdio mode.
- Windows process ownership, literal argv, bounded output, cancellation and whole-Job cleanup.
- Session documents with exact SHA/count edits, repeated memory reads/search and explicit single-file commits.
- Strict UTF-8/BOM, UTF-16LE/BE BOM and explicit CP936/GBK preservation and lossless transcoding.
- Linux backend with literal UTF-8 argv, process-group ownership, nonblocking pipes, timeout/cancellation and bounded cleanup observation.
- Linux regular-file document commits using same-directory temporary files, fsync, rename, identity/SHA checks and readback; owner/group/mode preservation.
- Linux C11 build and native process/document/JSON/stdio gates, including output backpressure and SIGTERM cases.
- Human-readable Chinese MCP tool titles that distinguish memory edits from disk commits, while retaining tool identifiers.
- Deterministic Windows portable ZIP, SHA-256 manifest and sidecar, packaging-checkout provenance, SPDX 2.3 file inventory, and extraction/CLI validation.
- Versioned builds through the `CDX_VERSION` compile contract, with tagged `vMAJOR.MINOR.PATCH` Windows/Linux release packaging and checksum assets.
- GitHub `IoTSharp/Cordonix` release workflow with optional Gitee source mirroring and dual-platform asset publication through a protected `GITEE_TOKEN` secret.
- Installation and update guidance with a copyable AI prompt, exact asset selection, SHA-256 verification, bounded GitHub-to-Gitee fallback and rollback-aware Codex configuration switching.
- Apache-2.0 license, contributor/security guidance and bilingual README.

- One-shot `read_file` with explicit encoding and 1-based line ranges, also available through CLI `--request`.
- Line-range mode for `workspace_read`, with whole-line byte limits, EOF/next-line metadata and disk/memory source distinction.

- Tail (`tail_lines`) and complete decoded-text (`raw`) modes for disk and memory reads, with explicit output-limit errors instead of partial complete-read results.
- Portable ZIP includes the first-lines, line-range, tail and raw request examples referenced by the README.

- Native `list_directory` and `resolve_path` tools for bounded, nonrecursive hidden-aware directory queries and normalization of existing absolute filesystem paths, with explicit missing/permission/encoding states.

- Native `verify_encoding` batches with explicit encoding, expected path count, lexical root preflight, per-file metadata and conservative failure/NOT_RUN reporting.
- Pure path normalization and containment helpers shared by filesystem queries and batch preflight, without filesystem access during validation.

### Changed

- Shared document edit, conversion, encoding/EOL metadata and cleanup logic between Windows and Linux; kept file and encoding APIs in platform sources.
- Portable public path and cancellation types preserve the Windows C types; Linux execution results use `posix_error`.
- Shared native JSON and MCP behavior tests run on both platforms.
- GitHub Actions now defines Windows portable-package and Linux native gates.
- Tagged release CI now builds and verifies Windows x64 ZIP and Linux x64 tar.gz assets, publishes the GitHub release, mirrors the tag to Gitee and uploads the same assets when the Gitee repository and secret are configured.
- Canonical repository links use `IoTSharp/Cordonix`; the former `maikebing/Cordonix` URL remains only as a redirect target.
- MCP encoding verification reuses the active worker for notification/EOF cancellation; busy messages describe an active operation.
- Roadmap progress uses emoji markers with separate PASS/FAIL/UNKNOWN/NOT_RUN evidence.

### Fixed

- Gitee source mirroring preserves the original annotated tag object instead of the lightweight event tag produced by Actions checkout.
- Linux package checksums resolve relative filenames from the package directory; SPDX verification codes, package relationships and checkout provenance now carry actual content evidence.
- Windows encoding fixtures avoid the reserved `nul.txt` device name, so the hosted Windows gate exercises a real NUL-containing file.

- Linux UTF-8 validation rejects historic iconv sequences above U+10FFFF.
- Linux execution preserves the calling process when a child closes stdin, accepts root cwd, and keeps cleanup bounded after the root exits or closes its streams.
- Linux MCP stops dispatching buffered requests after output failure or interruption, cancels active work on EOF, and restores inherited stdout flags.
- Windows packaging resolves one Git executable when multiple installations are available and emits UTF-8 subprocess diagnostics.
- Encoding verification rejects a BOM-selected decoder that conflicts with the declared encoding instead of reporting a false declared-encoding pass.
