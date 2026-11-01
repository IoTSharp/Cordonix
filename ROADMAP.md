# Cordonix Roadmap

Release 0.0.5 (2026-11-02) includes the current process-query, native-phone and PowerShell compatibility work. Tagged CI, dual-host publication and this machine's natural hourly update remain separately observed release gates in HANDOFF.md; earlier acceptance below describes v0.0.4.

WinGet follow-up: `IoTSharp.Cordonix` uses the existing Windows portable archive. Source templates and verified manifest generation are implemented; the follow-up workflow prepares community repository PRs after dual publication. Community review, source indexing and the bootstrap installation's existing hourly task remain separate gates. See [WinGet distribution](docs/winget.md).

Latest accepted release: **v0.0.4**. CDX-09B adapters, independent Linux/client and finite-soak gates, natural hourly scheduling, official SPDX, same-environment compiler reproducibility, signed release/readback and actual 0.0.3-to-0.0.4 upgrades have evidence. The [per-item acceptance ledger](docs/successor-acceptance.md) is the authoritative closure map. GAP-03 remains a deliberate transaction limit; GAP-04 discard is tested; physical IO/model tokens, GUI rendering, Authenticode and broader environments are not promoted beyond their evidence. The current checkout also contains unreleased process-query work.


Baseline: 2026-10-30, Asia/Shanghai. IDs below are project planning IDs, not GitHub issues or pull requests.

## Completion standard

An item is complete only when the real product entry point exercises it and its gate has evidence. Source review, builds, native tests, MCP interoperability, performance, recovery, Linux filesystem behavior, packaging and release are separate gates.

Progress: ✅ complete within the stated boundary · 🟡 partial · 🚧 in progress · ⬜ planned. Receipts retain PASS, FAIL, UNKNOWN and NOT_RUN separately.

## Deliveries

| ID | Delivery | Status | Dependencies | Acceptance | Evidence / boundary |
|---|---|---|---|---|---|
| CDX-01 | Project charter, name and public contracts | ✅ complete | none | Name, rules, documentation and license | Cordonix/cdx, eight agent rules, Apache-2.0 and bilingual README |
| CDX-02 | C11 Windows process core | ✅ Windows MVP | CDX-01 | Typed argv, Job ownership, timeout/cancel, bounded output | 15 real native-process checks; broader workloads/soak remain separate |
| CDX-03 | Encoding-safe document core | ✅ Windows MVP | CDX-01 | UTF-8/BOM, UTF-16 BOM, explicit CP936, SHA, exact edits and commits | 36 core checks and explicit lossless memory transcoding |
| CDX-04 | C CLI and deterministic JSON contract | ✅ Windows MVP | CDX-02, CDX-03 | Native binary, UTF-8 JSON, original IDs, no shell parsing | CLI request/version and 46 JSON checks |
| CDX-05 | C MCP stdio adapter | ✅ Windows MVP | CDX-04 | Real stdio, asynchronous cancellation, official SDK and Codex | Local stdio gate; earlier baseline had 14 SDK workflows and fresh Codex open/read/search/close |
| CDX-06 | Memory workspace | ✅ bounded single-file MVP | CDX-03 | Reuse open documents, memory editing/search, explicit commit/conflict | Windows 25 encoding/integration edge checks; external programs read disk |
| CDX-07 | Linux POSIX backend | 🟡 local MVP implemented | CDX-02, CDX-04 | Process ownership and encoding/file parity | WSL Ubuntu native process/document/JSON/MCP integration; independent Linux adoption and metadata parity remain open |
| CDX-08 | Packaging and repeatable releases | 🟡 dual publication complete; signing separate | CDX-05 | Portable packages, provenance, SBOM, install/update guide and signed release | v0.0.1/v0.0.2 published to GitHub/Gitee with identical assets; real upgrade gates PASS; signing remains open |
| CDX-09 | Replace overlapping local agent tools with cdx | 🟡 gradual adoption | CDX-05, CDX-06 | Retire predecessors after real capability coverage | Local execution and encoding-aware edits use cdx first; specialized adapters remain for gaps |

## Current parallel tasks

The process and document agents own disjoint platform sources. The packaging agent owns package scripts. The supervising agent owns shared protocol, integration, documents and final gates; independent review findings return to the owning task before closure.

| Task | Scope | Status | Acceptance / evidence |
|---|---|---|---|
| CDX-07A | POSIX process backend and native fixtures | ✅ local gate passed | 16 real process checks; cleanup is limited to the created group and incomplete observations remain explicit |
| CDX-07B | POSIX documents and shared edit core | ✅ local gate passed | 42 file checks, ASan/UBSan/leak detection, four SHA-256 known vectors |
| CDX-07C | Linux CLI/MCP, builds and final integration | ✅ local gate passed | Current tree: Linux 276/276 (full Git/rg) and Windows 284/284; real stdio cancellation/EOF/output blocking/SIGTERM and readable titles |
| CDX-07D | Independent Linux adoption and filesystem metadata | 🟡 independent client and bounded soak PASS | 44 checks across ext4/tmpfs/9p; 60 rounds over 600 seconds; crash-before-commit discards memory. ACL/xattr NOT_SUPPORTED; native Linux Codex installed, model authentication missing; other distributions NOT_RUN |
| CDX-08A | Windows/Linux portable packages | ✅ local gate passed | Identical Windows ZIP from two runs plus Linux tar.gz, SHA-256 metadata, install/update docs and runtime readback |
| CDX-08B | Tagged dual-platform releases | ✅ v0.0.2 full workflow PASS | Windows/Linux gates, GitHub release, original Gitee tag mirror and Gitee release all PASS; ten identical assets downloaded from each source for both versions |
| CDX-08C | Release signing | 🟡 external identity missing | No configured Git signing key or Windows code-signing certificate was found. Independent signature verification NOT_RUN; checksums, schema and embedded provenance do not close this gate |
| CDX-08D | Installers and hourly user updates | ✅ implementation and isolated Windows scheduler PASS | Windows 12 and Linux 14 installer checks; published upgrade gates; seven real Windows register/run/disable checks. Natural hourly triggering and Linux user scheduling remain separate |
| CDX-09A | Local execution and encoding-aware edit migration | ✅ local policy updated | Retired PowerShell execution skill; known GBK opens explicitly as CP936 before editing |
| CDX-09B | Remaining Native Tools adapters | ✅ source and local gates | Creation, child environments, parsed rg/Git implemented; installed-client adoption and published assets remain separate |
| CDX-09B1 | Native new-file creation | ✅ local gate passed, unreleased | Strict encoding, no overwrite, bounded cancellation, 20 native checks per platform plus eight shared MCP checks; Windows /analyze and Linux ASan/UBSan/leaks PASS |
| CDX-09C | Native line-range reading | ✅ local gate passed | One-shot read_file and memory workspace_read line mode; 16 shared real MCP checks; unchanged-SHA comparisons for TableStore.cs Skip 950 / First 245 and encoding_posix.c First 100 |
| CDX-09D | Native tail and complete-text reading | ✅ local gate passed | 14 shared MCP checks; real Tail 55 and Raw examples match PowerShell with unchanged source SHAs; over-budget complete reads fail explicitly |
| CDX-09E | Native directory listing and path resolution | ✅ local gate passed | 12 shared MCP checks; hidden/system entries, limits, normalization and explicit missing paths; existing-directory comparison matches PowerShell |
| CDX-09F | Native batch encoding verification | ✅ local gate passed | 14 shared MCP checks; seven distinct CP936 fixtures match .NET strict verification without writes; scope/count preflight, BOM mismatch, timeout/cancel/EOF and NOT_RUN evidence |
| CDX-09G | Execution boundary diagnostics and caller guidance | ✅ local gate passed, unreleased | PowerShell array binding reproduced; cleanup observations exposed; Windows 96/96 and Linux 100/100 real MCP checks. Persistent sessions remain outside this API; installed-client adoption is separate |
| CDX-09H | Native process observation | 🟡 local source gate, unreleased | Windows C11 `query_processes` is wired into CLI/MCP with bounded filters, metadata and explicit partial states; focused native and MCP checks are 2/2 on each platform. Linux returns `not_supported`; release and installed-client adoption are separate |

## Native phone extension (2026-10-31)

The user selected pure C11 implementation without the existing .NET bridge. [Contract and evidence](docs/phone-native.md).

| ID | Scope | Status | Evidence / remaining gate |
|---|---|---|---|
| CDX-10A | Native Windows Bluetooth, MAP, PBAP and HFP operations | 🟡 partial hardware acceptance | Existing pairing reused; authorized SMS and short outgoing call received per user, active/idle readback and hangup PASS; later PBAP contact and MAP metadata pages PASS; answering and sensitive reads remain open |
| CDX-10B | SMS/incoming-call observation | 🟡 bounded polling implemented | Timeout/cancellation/change fixtures; persistent MNS push, simultaneous subscriptions and live incoming-event tests remain open |
| CDX-10C | Contact-directed actions and verification codes | 🟡 partial hardware acceptance | One complete vCard with a telephone field returned; explicit-number SMS received per user; contact-selected actions and real body/code extraction remain open |
| CDX-10D | Call audio and deployment | ⬜ unsupported / planned | No SCO/PCM audio, no native Linux Bluetooth, no package/release/installed-client acceptance |
| CDX-10E | Discovery, pairing/removal and connection visibility | ✅ local hardware acceptance, unreleased | Native GUI, adapter name, discovery, topmost confirmation and verified Windows removal PASS. Native Windows device-pairing C ABI paired the second iPhone; HFP retention, GUI lease and phone-side connected display PASS. 15/15 real socket lifecycle checks; prolonged hardware soak and multi-adapter pairing selection remain separate |

| CDX-10F | Phone access onboarding and native Windows UI | 🟡 ANCS subscription/content PASS; WeChat delivery PASS | Fresh user-confirmed pairing restored ANCS; an existing Apple Phone notification and a new WeChat event (`com.tencent.xin`, `preexisting=false`) were read successfully. The bounded receiver discards the oldest replayed events when the source queue is full, reserves a result slot for a new event and preserves completed events on error. GUI labels event types and listening completion. Retained HFP, SMS metadata and recent calls readable; latest contact page preview unsupported. Native system theme/DPI and separate access checks remain; iOS switches are never inferred or changed by the PC |

The 2026-11-01 ANCS follow-up adds user-authorized initial link retries: one attempt plus at most five retries for unreachable service discovery, with a shared deadline, cancellation and a 500 ms interval. Results and the GUI show actual attempt/retry counts and exhaustion; the GUI notification request allows 60 seconds.
The live check succeeded on attempt 2 after one retry, opened both ANCS subscriptions and read three existing Apple Phone events. Windows retry/session checks pass 32/32 (nine new checks), phone 66/66 and MCP 106/106; no new WeChat event arrived in this run.

## Local Codex session audit

PowerShell path compatibility: the Windows process core now accepts an explicitly supplied absolute `pwsh.exe` path for saved scripts, including Microsoft Store and portable installations, while preserving command-string/encoded-command rejection. `eng\test.cmd` passes with 40/40 process checks, 101/101 existing MCP checks and 8/8 new real PowerShell CLI/MCP checks using this machine's Store package and execution-alias paths. MSVC `/analyze` and an AddressSanitizer process run (40/40) pass. The optional ripgrep adapter scope is `NOT_RUN` when rg is absent from the gate's environment. Release publication and installed-client adoption remain separate.

The October 31, 2026 audit covered 109 local Codex sessions. It found 11,568 recorded external command calls, 983 failure records, 192 explicit `.ps1` file changes and 179 explicit `.py` file changes. The repeated categories were process/browser/task observation, JSON state readback, Git/ripgrep checks and bounded file batches. The evidence supports native process observation as the first new C capability; it does not justify removing browser DOM automation, vendor APIs, compilers, Git, ripgrep or scheduler APIs that remain external boundaries.

## Next item

CDX-09B2 (child environments) passes Windows 35 / Linux 39 process checks and 11 shared MCP checks. CDX-09B3 (external adapters) passes Windows 26 / Linux Git 17 native checks; Linux full rg passes 29 checks locally and in required CI. Official Python SDK acceptance passes 12 workflows. Compiler reproducibility and SPDX schema have their own gates; they do not establish a signing identity.

CDX-09B source gates are complete. Finish exact-commit remote CI, new-version release/readback and installed Codex adoption. The process-query source gate is local and unreleased; its Windows command-line observation uses undocumented PEB layout details and is a best-effort diagnostic, not a security boundary. Both isolated schedulers have naturally completed hourly recurrence and task cleanup. The Linux observation records a host suspend that delays monotonic time. Signing identity, Linux Codex model authentication, UI title rendering and other distributions retain separate gates. See [the complete acceptance ledger](docs/successor-acceptance.md). Published v0.0.1/v0.0.2 tags and assets are immutable.

## Capability gaps

| ID | Reproduction / scale | Expected and actual | Owner | Blocked scope | Closing evidence |
|---|---|---|---|---|---|
| GAP-01 | C core packaging | Reusable header/library and native executable | maintainers | Windows MVP | ✅ native build and process checks |
| GAP-02 | Memory edits and external compiler reads | Explicit commit materializes memory before external execution | maintainers | Windows MVP | ✅ MCP edit/convert/commit/readback |
| GAP-03 | External writer during final replacement | Hash/identity checks prior changes; no filesystem compare-and-swap | maintainers | stronger transaction guarantees | 🟡 documented limitation; no speculative locking |
| GAP-04 | Crash before memory commit | Uncommitted session data is discarded | maintainers | crash recovery | ✅ declared discard behavior verified by real SIGKILL and unchanged disk bytes; recovery NOT_SUPPORTED |
| GAP-05 | Performance/token/physical-I/O claims | Measure fixed workloads first | maintainers | public performance claims | ✅ fixed 128 KiB, 3 x 100 fresh/memory measurements and equal response SHA; physical IO/token NOT_MEASURED, no general savings claim |
| GAP-06 | File creation and execution environment overrides | Native creation and isolated child overrides | maintainers | retirement of remaining adapters | ✅ local source gates; release and installed adoption remain separate |
| GAP-07 | Readable tool titles in Codex | tools/list includes title; UI selection belongs to the client | maintainers/client | dynamic per-call labels | 🟡 protocol title readback passes; fresh Codex rendering and dynamic labels UNKNOWN |
| GAP-08 | Repeated process/task observation scripts | A bounded native snapshot with explicit inaccessible/timeout/partial states | maintainers | Browser DOM, vendor APIs, scheduler registration and other external interfaces | 🟡 Windows source and MCP checks pass locally; POSIX is explicit `not_supported`; unreleased |
