# Cordonix Roadmap

Latest accepted release: **v0.0.4**. CDX-09B adapters, independent Linux/client and finite-soak gates, natural hourly scheduling, official SPDX, same-environment compiler reproducibility, signed release/readback and actual 0.0.3-to-0.0.4 upgrades have evidence. The [per-item acceptance ledger](docs/successor-acceptance.md) is the authoritative closure map. GAP-03 remains a deliberate transaction limit; GAP-04 discard is tested; physical IO/model tokens, GUI rendering, Authenticode and broader environments are not promoted beyond their evidence. New uncommitted process-query work has separate ownership.


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
| CDX-09C | Native line-range reading | ✅ local gate passed | One-shot read_file and memory workspace_read line mode; 16 shared real MCP checks; unchanged-SHA comparisons for TableStore.cs Skip 950 / First 245 and encoding_posix.c First 100. Follow-up 2026-11-04: installed 0.0.5 matches all three reported Skip/First slices (1990/67, 60/30, 290/27); MCP parameter guidance and bilingual examples now state the mapping |
| CDX-09D | Native tail and complete-text reading | ✅ local gate passed | 14 shared MCP checks; real Tail 55 and Raw examples match PowerShell with unchanged source SHAs; over-budget complete reads fail explicitly |
| CDX-09E | Native directory listing and path resolution | ✅ local gate passed | 12 shared MCP checks; hidden/system entries, limits, normalization and explicit missing paths; existing-directory comparison matches PowerShell |
| CDX-09F | Native batch encoding verification | ✅ local gate passed | 14 shared MCP checks; seven distinct CP936 fixtures match .NET strict verification without writes; scope/count preflight, BOM mismatch, timeout/cancel/EOF and NOT_RUN evidence |
| CDX-09G | Execution boundary diagnostics and caller guidance | ✅ local gate passed, unreleased | PowerShell array binding reproduced; cleanup observations exposed; Windows 96/96 and Linux 100/100 real MCP checks. Persistent sessions remain outside this API; installed-client adoption is separate |
| CDX-09I | Native C SSH client and configurable bastion routes | 🟡 Windows implementation and local gates PASS | Direct IP, direct-tcpip jump and PTY selection; 27/27 actual loopback protocol checks and 27/27 ASan client checks; full Windows 339 and Linux Git-scope 299 PASS. Linux SSH NOT_SUPPORTED; deployed bastions and installed-client adoption NOT_RUN |

## Next item

CDX-10F default local-proxy selection is ✅ implemented and locally verified
on Windows: direct first, then both local 7890/1080 candidates with real
CONNECT/SOCKS5 recognition, shared deadline and no replay after dispatch.
Shared proxy 23, native HTTP 29 / MCP 26, network 38 and SSH-proxy 6 checks
PASS; full Windows 365 and Linux Git-scope 309 checks PASS. The HTTP loopback
destination boundary, Linux NOT_SUPPORTED and installed-client/deployed-host
NOT_RUN remain explicit in [local proxy routing](docs/local-proxy.md).

| Item | Decision / local state | Remaining acceptance |
|---|---|---|
| CDX-10A HTTP/HTTPS | ✅ WinHTTP native requests; 19 real protocol checks, ASan and system TLS readback PASS | Broader curl features and Linux backend |
| CDX-10B SFTP | ✅ Existing libssh2 reused; 11 subsystem checks including lost OPEN reply and cancellation, ASan PASS | Large transfers, deployed servers and atomic publication |
| CDX-10C Serial | 🟡 Passive diagnosis and explicit bounded I/O; 16 native/ASan checks PASS | Physical UART, modem signals and driver cancellation under load NOT_RUN |
| CDX-10D TCP/Telnet/MQTT | 🟡 24 loopback/ASan checks PASS; MQTT CONNECT/QoS0 subset | MQTT TLS/auth/subscription/QoS1/2 and persistent terminals |
| CDX-10E FTP/FTPS | ⬜ Evaluated, deferred pending a legacy compatibility requirement | Backend/dependency approval and real FTP fixtures |

The integrated communication layer passes Windows 361 and Linux Git-scope 305 checks. The four new tools are native Windows implementations; Linux returns NOT_SUPPORTED. The scope/installation-cost decision is in [communication protocols](docs/communication-protocols.md).

CDX-09I implements a native C SSH client (Windows, static libssh2/WinCNG) with direct, direct-tcpip jump and configurable interactive-bastion routes. It has explicit host-key pinning, credential aliases, bounded cancellation/output and remote UNKNOWN semantics. Local loopback protocol, full regression, deployed-bastion compatibility and installed-client adoption are separate gates; see [native SSH](docs/native-ssh.md). Linux currently returns NOT_SUPPORTED.

CDX-09H adds native Windows `query_processes` with original command-line and creation-time observations, 19 native checks and shared MCP wiring. Linux process enumeration is explicitly NOT_SUPPORTED. The local skill migration consolidates the old adapter skill into Cordonix; published-release and fresh Codex-client adoption remain separate from local C/CLI/MCP validation.

CDX-09B2 (child environments) passes Windows 35 / Linux 39 process checks and 11 shared MCP checks. CDX-09B3 (external adapters) passes Windows 26 / Linux Git 17 native checks; Linux full rg passes 29 checks locally and in required CI. Official Python SDK acceptance passes 12 workflows. Compiler reproducibility and SPDX schema have their own gates; they do not establish a signing identity.

CDX-09B source gates are complete. Finish exact-commit remote CI, new-version release/readback and installed Codex adoption. Both isolated schedulers have naturally completed hourly recurrence and task cleanup. The Linux observation records a host suspend that delays monotonic time. Signing identity, Linux Codex model authentication, UI title rendering and other distributions retain separate gates. See [the complete acceptance ledger](docs/successor-acceptance.md). Published v0.0.1/v0.0.2 tags and assets are immutable.

## Capability gaps

| ID | Reproduction / scale | Expected and actual | Owner | Blocked scope | Closing evidence |
|---|---|---|---|---|---|
| GAP-01 | C core packaging | Reusable header/library and native executable | maintainers | Windows MVP | ✅ native build and process checks |
| GAP-02 | Memory edits and external compiler reads | Explicit commit materializes memory before external execution | maintainers | Windows MVP | ✅ MCP edit/convert/commit/readback |
| GAP-03 | External writer during final replacement | Hash/identity checks prior changes; no filesystem compare-and-swap | maintainers | stronger transaction guarantees | 🟡 documented limitation; no speculative locking |
| GAP-04 | Crash before memory commit | Uncommitted session data is discarded | maintainers | crash recovery | ✅ declared discard behavior verified by real SIGKILL and unchanged disk bytes; recovery NOT_SUPPORTED |
| GAP-05 | Performance/token/physical-I/O claims | Measure fixed workloads first | maintainers | public performance claims | ✅ fixed 128 KiB, 3 x 100 fresh/memory measurements and equal response SHA; physical IO/token NOT_MEASURED, no general savings claim |
| GAP-06 | File creation and execution environment overrides | Native creation and isolated child overrides | maintainers | retirement of remaining adapters | ✅ local source gates; release and installed adoption remain separate |
| GAP-07 | Tool titles and per-call summaries | Fixed tools/list title plus deterministic result metadata and opt-in progress messages | maintainers/client | dynamic collapsed rows | 🟡 native C11 summaries implemented without extra model calls/input fields/content; protocol gates and client adoption are recorded separately in HANDOFF |
