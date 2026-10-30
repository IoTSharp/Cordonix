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
| CDX-09C | Native line-range reading | ✅ local gate passed | One-shot read_file and memory workspace_read line mode; 16 shared real MCP checks; unchanged-SHA comparisons for TableStore.cs Skip 950 / First 245 and encoding_posix.c First 100 |
| CDX-09D | Native tail and complete-text reading | ✅ local gate passed | 14 shared MCP checks; real Tail 55 and Raw examples match PowerShell with unchanged source SHAs; over-budget complete reads fail explicitly |
| CDX-09E | Native directory listing and path resolution | ✅ local gate passed | 12 shared MCP checks; hidden/system entries, limits, normalization and explicit missing paths; existing-directory comparison matches PowerShell |
| CDX-09F | Native batch encoding verification | ✅ local gate passed | 14 shared MCP checks; seven distinct CP936 fixtures match .NET strict verification without writes; scope/count preflight, BOM mismatch, timeout/cancel/EOF and NOT_RUN evidence |
| CDX-09G | Execution boundary diagnostics and caller guidance | ✅ local gate passed, unreleased | PowerShell array binding reproduced; cleanup observations exposed; Windows 96/96 and Linux 100/100 real MCP checks. Persistent sessions remain outside this API; installed-client adoption is separate |

## Next item

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
| GAP-07 | Readable tool titles in Codex | tools/list includes title; UI selection belongs to the client | maintainers/client | dynamic per-call labels | 🟡 protocol title readback passes; fresh Codex rendering and dynamic labels UNKNOWN |
