# Cordonix Roadmap

## Active context-efficiency batch (2026-11-12)

The user authorized a new implementation chat with multiple agents to deliver the next useful
capabilities after the TokenEconomizer comparison. This batch takes priority over the historical
worker-start instructions below. The [implementation plan](docs/context-efficiency-plan-2026-11-12.zh-CN.md)
defines the interfaces to settle, non-overlapping ownership, compatibility and closing gates.
CDX-14A/B/C now have local source acceptance below; they remain separate from
released and installed capabilities.

Current reference: clean source `22956e8` before this planning change; the connected installed
runtime is 0.5.0 from `00e07a0`, exposing 35 tools. Its complete `tools/list` tools array is
62,487 UTF-8 bytes after compact JSON serialization. Selecting the 27 local-code tools gives
38,227 bytes (38.8% less). This is a local schema projection measurement, not model token,
provider cost or task-success evidence. Existing read batches, HTTP representation and data
query remain delivered capabilities rather than new work.

| ID | Delivery | Priority / status | Dependencies and acceptance |
|---|---|---|---|
| CDX-14A | Explicit MCP tool profiles | ✅ Local source PASS, 2026-11-13 | `--mcp --tool-profile full\|local-code`: default/full 35, local-code 27; every retained schema is identical. C helper 46/46 and real stdio 11/11 on Windows/Linux prove membership, unknown-profile error and unchanged direct-call validation. Discovery selection is not authorization. |
| CDX-14B | Compact execution results with raw-output readback | ✅ Local source PASS, 2026-11-13 | `result_mode=full\|summary`, required summary `capture_log_path`, bounded Base64 tails and captured-byte SHA-256; native no-replace logs with stream offsets. Real stdio 12 groups/27 calls on each platform prove status/encoding/output-limit/timeout/cancel, fallback and one execution. GCC analyzer and Linux ASan/UBSan projection suite pass. |
| CDX-14C | Reproducible context-efficiency measurements | ✅ Local fixed-corpus PASS, 2026-11-13 | Five rounds per platform: 152 checks, 50 execution outcomes, 90 total RPC, 0 retries/unknown; exact schema/result/wire bytes, raw hashes and median/P95 retained. Twelve harness tests cover transport UNKNOWN and tokenizer batching. Actual tokens/provider usage/billing/real agent task success remain NOT_MEASURED. |
| CDX-14D | Model-free project facts and failure fingerprints | P1 / follow-up after CDX-14C | Keep host-side evidence separate from C11 execution. Bound candidate/active/revoked facts, confirmation and contradiction evidence, normalized error signatures and injected context. Reuse observed commands and project rules before introducing an automatic learner. |
| CDX-14E | Host-side failure-loop reminders | P1 / follow-up after CDX-14C/14D | Start with repeated-failure rules and progress evidence; one redirect per turn, human-controlled stopping. A model judge is a later explicit choice with its cost included. Timeout/UNKNOWN does not authorize replay. |
| CDX-14F | Grouped search context and deterministic build/test diagnostics | P1 / complete CDX-11B2 and CDX-12C incrementally | Choose from representative real rg/build logs after the first batch. Preserve paths, error coverage, partial/unknown parsing and original-log access; no invented compiler semantics, LSP dependency or silent loss of unparsed lines. |
| CDX-14G | Batched single-document edits and diff preview | P1 / complete CDX-12B after the first batch | Reuse encoding/current hash/count checks, specify edit ordering and overlap, and validate real CP936/BOM/EOL/conflict cases. Ordinary UTF-8 edits still use apply_patch; no multi-file atomicity claim. |

Three workers implement CDX-14A, CDX-14B and CDX-14C with disjoint owned files. The coordinator
settles the shared interfaces before parallel edits, owns shared schemas/CLI/build wiring and
README/ROADMAP/HANDOFF/CHANGELOG, serializes builds and runs the final Windows/Linux gates.
Raw fixture receipts belong under ignored `build/`. Full contracts and all named parameter
descriptions must be readable before acceptance. Paid model calls, new dependencies, commit/push,
release publication and replacement of shared installed runtimes are separate later actions.

Current-tree acceptance uses `build/context-efficiency-20261113/`: Windows `eng/test.cmd`
exit 0 (197/197 shared stdio), WSL Ubuntu `eng/test-linux.sh` exit 0 (198/198 shared
stdio; native Linux rg remains NOT_RUN). Tools-array bytes are 63,569 full versus
39,309 local-code; wire bytes are 63,614 versus 39,354. These are byte measurements,
not token savings. For the fixed five-case execution mix, summary increases small-result
median bytes while reducing P95; all receipts, corrections and unrun gates are in HANDOFF.
This closes the first local implementation batch, not later host features or all roadmap stages.

## 0.5.0 release (2026-11-10)

Published clean `00e07a0` on GitHub and Gitee. Source/tag CI and the final tagged release workflow pass. Twelve mirrored asset pairs, four original archive attestations and four tampering rejections pass public readback. This computer is upgraded from 0.4.1 to 0.5.0 with unchanged updater settings; installed Windows MCP 198/198, VNC 54/54 and downloaded Linux MCP 198/198 pass. A fresh stable-launcher process exposes 35 tools and completes overlapping directory/batch calls. Existing chat reconnection and WinGet catalog acceptance retain separate boundaries in HANDOFF.md.

## MCP overlapping directory requests (2026-11-10)

The bounded FIFO scheduler is integrated into both current Windows/Linux dispatchers for 0.5.0. The observed 0.4.1 failure rejected `list_directory` and `tool_info` while a batch read occupied the worker. Local directory/batch-read, ordering, cancellation, capacity and shutdown checks pass as part of 198/198 real MCP checks per platform. The user authorized source delivery, formal 0.5.0 publication and installation on this computer; hosted gates, public readback and installation remain separately recorded in HANDOFF.md. See [request scheduling](docs/mcp-request-scheduling.md).

## VNC operations screenshots (2026-11-08)

The authorized addition is `vnc_screenshot`: remote IP, optional port defaulting
to 5900, optional password and a new PNG path. Windows native RFB supports None
and classic VNC authentication; missing passwords use the current user's
credential or a bounded masked dialog. Save only after successful authentication.
The implementation shares PNG encoding/publication with local screenshots,
adds no dependency, and exposes 35 development tools. Linux reports
`not_supported`. See [the contract](docs/native-vnc.md) and HANDOFF.md for
native/MCP/dialog validation and separate remote/install gates.

## 0.4.1 release (2026-11-08)

Published from clean `d8a67cf` to GitHub and Gitee. Main/tag CI and release attempt 2 pass; all 12 mirrored asset pairs, archive checksums, exact-tag signatures and public runtime identities are verified. OSChina news 503467 is submitted pending editorial review; tweet 30402017 has verified text/image readback. WinGet PR #474302 remains an independent upstream gate. HANDOFF.md contains receipts and installed-client boundaries.

The user added a release gate requiring an audit of all tool guidance. The audit found 167 missing property descriptions across 25 tools; the release now fills those gaps, clarifies misleading defaults and documents all 34 tools in the selection guide. Real MCP schema checks cover nested fields and required names. Source, hosted validation and final publication evidence remain in HANDOFF.md.

The user authorized a new release containing daily background upgrade checks and the create-file argument diagnostic repair. The minor version reflects the new `--checkupgrade` CLI capability and updater behavior; the existing 34-tool MCP contract is retained. Source commit `40426dc` passed both hosted Windows/Linux CI jobs, including package, reproducibility and installer gates. Tagged signing, dual-host publication and public-download acceptance are recorded separately in HANDOFF.md.

## Create-file argument diagnostics (2026-11-08)

An active chat supplied `text` instead of the required `content` while correctly specifying `utf-8`. The shared validation message incorrectly suggested an auto-encoding problem. The scoped repair clarifies tool/property descriptions and reports the actual invalid parameter, including the `text` to `content` correction. Real MCP tests verify rejection before creating a file. Validation and installed-client adoption are recorded separately in HANDOFF.md.

## Automatic update triggering (2026-11-07)

The user reported stale installations, authorized installation and requested a separate `cdx --checkupgrade` instance on the first startup each day. Windows/Linux now provide that native command and advertise `tool_info.update_command`; stable launchers claim the local calendar day and start a bounded background worker while the main runtime continues. Daily startup is enabled by default without a scheduler, explicit opt-out persists, and optional legacy enable flags can add daily backup tasks. Future package gates must exercise the native command. This machine now has clean formal 0.3.1, a migrated launcher and a fresh 34-tool MCP readback; 0.3.1 uses the script-worker compatibility path until the new native command is published. The failing optional Windows task is disabled; the installed background startup check and same-day suppression pass. HANDOFF.md records gates and the retained active connection. The MCP tool count remains unchanged.

## Measured capability gaps (2026-11-06)

The user authorized multiple agents to implement the measured gaps in sequence. Release 0.3.0 adds native `stat` (general file metadata) and `find_program` (bounded executable discovery) to the existing JSON/JSONL/CSV `data_query`, rg-backed `find_files`, binary hash and no-replace file operations. The published schema has 34 tools. PR #1 merged at `5b43630`; Windows/Linux CI, tagged publication, all 12 mirrored asset hashes and exact-tag OIDC verification pass. [Scope, measured demand and gates](docs/capability-gaps-20261106.zh-CN.md) distinguish these slices from remaining SQLite, grouped search/context, snapshot cursors, archive, browser, image and Office workflows. HANDOFF.md records the delivery receipts. Installed runtime adoption remains a separate delivery.

## 0.3.1 regression repair (2026-11-06)

Post-publication review reproduced four 0.3.0 regressions with real fixtures: an absent PATH skips valid explicit candidates on both platforms; Windows stat omits a representable pre-1970 timestamp; POSIX stat can return a normalized path for a different entry after symlink/`..` traversal; physical executable paths lose Python virtual-environment identity. The authorized 0.3.1 patch repairs all four, directory hidden-name flags, legacy executable selection, normalization failures and interruption after the final missing probe. PR #3 merged at `0c50aab`; Windows/Linux CI, tagged gates, dual-host publication, all 12 mirrored asset hashes and exact-tag OIDC verification pass. Downloaded binaries pass 165/166 real MCP checks and actual CLI regressions. Published 0.3.0/0.2.0 tags and assets are preserved. WinGet PR #472173 still requires Microsoft investigation of its Defender installation failure; the automatic 0.3.1 submission is PR #472267 and has its own upstream gates. See HANDOFF.md for evidence.

## Native screenshot gap (2026-11-06)

The user reported repeated `Add-Type`/`System.Drawing.Common` compilation failures while capturing a WinForms demo. The authorized local addition is a native Windows `screenshot` tool with bounded window discovery, desktop/region/window/foreground PNG capture and explicit screen-versus-target rendering. It reuses the process core to bound synchronous capture, GDI/WIC from Windows and the existing C11/MSVC workflow. No new third-party dependency or UI automation is introduced. Development now has 27 tools. [The screenshot contract](docs/native-screenshot.md) documents visual-verification limits and separate build/native/MCP/package/installed-client gates; current results belong in HANDOFF.md.

## Active three-stage program (2026-11-05)

The user authorized planning and implementation in a new chat with multiple agents, and explicitly requires independent decisions based on observed tasks, data, algorithms and business value. The [implementation plan](docs/implementation-plan-2026-11-05.zh-CN.md) defines ownership, dependencies, decision gates and closure. The [competitive assessment](docs/competitive-assessment-2026-11-05.zh-CN.md) is dated input, not an instruction to implement every competitor feature. This active program takes precedence over historical "Next item" and parallel-task descriptions below.

Baseline: source and connected installed runtime retain separate evidence. Release 0.3.0 exposes 34 tools, including `stat` and `find_program`; Windows/Linux native, MCP, package and publication gates are recorded in HANDOFF.md. Native directory creation remains part of the existing surface. Installed adoption remains a separate gate. No token-saving percentage is accepted as measured or as a fixed gate before a reproducible baseline exists.

| ID | Stage / delivery | Dependencies | Initial decision and status | Closing evidence |
|---|---|---|---|---|
| CDX-11A | 1: Optional compact output, starting with HTTP representation selection | Existing HTTP/encoding contracts; CDX-11C baseline | ✅ Local implementation and acceptance: optional both/text/base64; old default preserved | Real loopback CLI/MCP comparisons, byte-exact raw fallback, unchanged error/outcome/cleanup states; full-response bytes and tokens only when measured |
| CDX-11B1 | 1: Bounded multi-file reads | Existing read modes/strict decoding; CDX-11C | ✅ Local implementation and acceptance: 1–32 sequential reads, text/output budgets and preserved native path traversal | Same requested text as separate reads; per-file failure and NOT_RUN; final serialized byte budget; Windows/Linux worker dispatch and real MCP deadline/cancellation |
| CDX-11B2 | 1: File discovery, grouped search/context and resumable listing | CDX-11B1; measured high-frequency discovery tasks | 🟡 `find_files` implemented with complete-scan offset pages; grouped context and stable cursors remain open | Real rg, sorting/deduplication, bounded incomplete scans and Unicode/invalid-byte fixtures; each request rescans |
| CDX-11B3 | 1: Native executable discovery | Existing absolute-program contract and PATH observations | ✅ `find_program` resolves explicit candidates and absolute PATH entries without execution | Windows `.exe` validation, POSIX execute checks, unique bounded results, not-found and incomplete scan states; installed adoption remains separate |
| CDX-11C | 1: Reproducible efficiency and task-success baseline | Frozen source/binary/input corpus | ✅ Three-round byte/call baseline: Windows 25 cases/725 checks, Linux 15/292; token/cost/RSS/IO unmeasured | Counts, bytes, time and correctness first; exact tokenizer/provider usage only when available; raw receipts and all failures retained |
| CDX-11D | 1: Capability/version discovery and truthful package/document state | Actual schemas, package manifests; existing CDX-09E1 | ✅ Local schema/package drift repaired: 26 tools, 50 payload files, linked documents and runnable batch example; installed adoption separate | CLI/MCP/source/install boundaries agree; packaged links and examples resolve; current directory feature is not reimplemented |
| CDX-11E | 1: MCP result semantics and protocol/client compatibility | Current official specification plus real client behavior | 🟡 Local deterministic mapping accepted on Windows/POSIX; modern SDK/client compatibility remains open | `tools/call` rejects malformed/unknown calls with `-32602`; deterministic domain failures add outer `isError` while preserving inner status; no blanket handshake rewrite or duplicated structured/text payload |
| CDX-12A | 2: Finite background jobs with status/output/cancel | CDX-11C, CDX-11E; output-retention decision | 🟡 Deferred: current terminal lifecycle and cleanup contract remain sufficient; decision recorded in the handoff | Reopen after evidence of client deadline mismatch or a bounded task that needs cross-request status/output; preserve explicit deadline/cancel/EOF and uncertain outcomes |
| CDX-12B | 2: Single-document edit batches and diff preview | Existing hash/count/encoding contract | ⬜ Select after first-wave measurements | Real multi-edit/conflict/encoding fixtures; unchanged ordinary apply_patch route; no multi-file atomicity claim |
| CDX-12C | 2: Git change and build/test diagnostic views | CDX-11A/11C, actual high-frequency tools | ⬜ Deterministic views before general log intelligence | Exit status, errors and original-log access survive reduction; representative failed/successful builds |
| CDX-12D | 2: Symbol-aware code navigation | Real target-language corpus and backend cost assessment | ⬜ Evaluate LSP/IDE reuse; no new dependency by implication | Cold/warm latency, references/diagnostics correctness and end-to-end benefit over rg; approve any required new dependency separately |
| CDX-12E | 2: JSON/JSONL/CSV local projection and aggregation | CDX-11C; concrete data tasks | ✅ Implemented, behavior/sanitizer checked; reproduced C6001 repaired and `/analyze /W4 /WX` passes without suppression | Exact numeric tokens, strict decoding, partial counts/rows, pagination, real CLI/MCP and cancellation; see current HANDOFF evidence |
| CDX-12F | 2: File copy/move/metadata/hash gaps | Observed daily file tasks; existing directory/create contracts | ✅ `stat` plus hash/copy/move implemented; recursive copy, overwrite and delete remain open | Exact bytes, metadata readback flags, normalized stat fields, ext4 move success and explicit platform limitations |
| CDX-13A | 3: Local image/asset processing and browser preview | Stage 1 result budgets; browser lifecycle owner | ⬜ First design slice after representative workflow selection | Editable artifact, local preview, bounded image/region evidence and actual visual inspection |
| CDX-13B | 3: Optional Windows UI Automation | Actual applications, CDX-13A evidence | ⬜ Prove element coverage and benefit before generalizing | Real window/element actions and state readback across selected DPI/theme cases |
| CDX-13C | 3: Office/PDF content, rendering and delivery adapters | Concrete document tasks, engine/dependency decision | ⬜ Reuse existing approved engines; scope explicitly | Both content checks and rendered-page inspection; openable output and clear engine/font requirements |

The original first-wave worker assignments to CDX-11A, CDX-11B1 and CDX-11C are historical:
those slices already have delivery evidence. The active 2026-11-12 batch above supersedes those
start instructions. Stage dependencies and all remaining acceptance boundaries still apply.

Completion requires a real CLI/MCP entry point, behavioral fixtures, the applicable current-tree platform gates, compatibility readback, an independently checked efficiency result and updated README/ROADMAP/HANDOFF/CHANGELOG. Package, installed-client adoption, remote CI and public release have separate status; publication, production changes, new dependencies and replacement of shared installed services are not implied by this program. Each implementation decision records the smallest alternative, evidence, expected cost and a falsification/stop condition. Details and the initial ownership matrix are in the implementation plan.

## 2026-11-06 follow-up decisions

The CDX-11E minimum slice is covered by real Windows and Linux stdio fixtures, including all 34 titled tools and `stat`/`find_program` calls. Current full native gates pass on Windows and WSL Ubuntu ext4 with actual rg configured. The metadata and discovery fixtures also cover Unicode, cancellation, links, timestamp range, result bounds and original POSIX traversal. Exact counts, analysis, sanitizer and package receipts are recorded in HANDOFF.md. Installed-client adoption remains separate.

The earlier CDX-11B2 decision deferred grouped context and stable continuation cursors because no real `truncated=true` result or continuation failure was captured. The measured capability-gap batch separately implements bounded rg file discovery with offset pages; each request rescans, so snapshot cursors and grouped context remain deferred. CDX-12A remains deferred because the current terminal session lifecycle already provides bounded execution, cancellation, EOF cleanup and explicit unknown outcomes, and no client deadline mismatch requiring cross-request task state has been demonstrated. The evidence and reopen conditions are recorded in [HANDOFF.md](HANDOFF.md).

## Earlier deliveries and evidence

Next-release directory creation (2026-11-04): `create_directory` is an authorized local addition to the C API, CLI and cancellable MCP worker on Windows/POSIX. `parents` and `exist_ok` explicitly select ensure-directory behavior; normalized paths, leaf/total creation counts and partial effects remain observable. Development has 25 tools; release publication and installed-client adoption are separate gates. See the current batch in [HANDOFF.md](HANDOFF.md).

Release 0.1.0 (2026-11-04) is published on GitHub/Gitee from clean source `6bf1e14`. All tagged CI jobs pass; 12 uploaded assets match across hosts; four original archives pass exact-tag OIDC verification and four tampered copies fail. Both downloaded platform binaries expose all 24 MCP tools, and the public Windows executable passes 108 real protocol checks. WinGet update PR [#470229](https://github.com/microsoft/winget-pkgs/pull/470229) is submitted; community review/indexing, installed-client adoption and physical/deployed-device acceptance remain separate. See [HANDOFF.md](HANDOFF.md).

Execution follow-up (2026-11-02): reproduced the same Store PowerShell request failing before spawn on retained 0.0.4 and succeeding on 0.0.5. The reported Ghidra interruption was a 300-second client deadline, distinct from the 30-minute child limit. Caller guidance and broader Store-path coverage pass the Windows final gate, including 16/16 PowerShell and 106/106 real MCP checks; the separate Git/rg adapter run passes 26/26. Built MCP schema readback passes. These follow-up changes are local and unreleased; see [execution boundaries](docs/execution-boundaries.md). No asynchronous job API or new process-launch mechanism is introduced.

Release 0.0.5 (2026-11-02) includes the current process-query, native-phone and PowerShell compatibility work. Tagged CI, dual-host publication and this machine's natural hourly update remain separately observed release gates in HANDOFF.md; earlier acceptance below describes v0.0.4.

The tagged release and dual publication pass. On 2026-11-02 the existing Windows hourly task naturally upgraded 0.0.4 to 0.0.5 at 03:27:03 Asia/Shanghai with exit 0 and the published archive hash. Fresh MCP through the existing stable launcher passes initialization, the 19-tool listing and clean release metadata readback. Existing connections retain their earlier processes; broader installed-client acceptance remains separate.

WinGet follow-up: `IoTSharp.Cordonix` uses the existing Windows portable archive. Source templates, verified manifest generation and authenticated submission pass locally and in workflow `41340981155`; initial community PR #468155 is open. The follow-up workflow submits version PRs after dual publication. Community review, source indexing and the bootstrap installation's existing hourly task remain separate gates. See [WinGet distribution](docs/winget.md).

Historical v0.0.4 acceptance: CDX-09B adapters, independent Linux/client and finite-soak gates, natural hourly scheduling, official SPDX, same-environment compiler reproducibility, signed release/readback and actual 0.0.3-to-0.0.4 upgrades have evidence. The [per-item acceptance ledger](docs/successor-acceptance.md) records that earlier closure map; current v0.1.0 publication is recorded above. GAP-03 remains a deliberate transaction limit; GAP-04 discard is tested; physical IO/model tokens, GUI rendering, Authenticode and broader environments retain their separate evidence boundaries.


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
| CDX-09E1 | Native directory creation | ✅ local gates passed; next release | Windows/POSIX 22 native checks each, 12 new shared MCP checks, 25-tool schema, Windows ASan and actual-ext4 ASan/UBSan/leaks; existing C6001 baseline recorded separately; publication/adoption deferred |
| CDX-09F | Native batch encoding verification | ✅ local gate passed | 14 shared MCP checks; seven distinct CP936 fixtures match .NET strict verification without writes; scope/count preflight, BOM mismatch, timeout/cancel/EOF and NOT_RUN evidence |
| CDX-09G | Execution boundary diagnostics and caller guidance | ✅ local gate passed, unreleased | PowerShell array binding reproduced; cleanup observations exposed; Windows 96/96 and Linux 100/100 real MCP checks. Persistent sessions remain outside this API; installed-client adoption is separate |
| CDX-09I | Native C SSH client and configurable bastion routes | 🟡 Windows implementation and local gates PASS | Direct IP, direct-tcpip jump and PTY selection; 27/27 actual loopback protocol checks and 27/27 ASan client checks; full Windows 339 and Linux Git-scope 299 PASS. Linux SSH NOT_SUPPORTED; deployed bastions and installed-client adoption NOT_RUN |
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
| GAP-07 | Tool titles and per-call summaries | Fixed tools/list title plus deterministic result metadata and opt-in progress messages | maintainers/client | dynamic collapsed rows | 🟡 native C11 summaries implemented without extra model calls/input fields/content; protocol gates and client adoption are recorded separately in HANDOFF |
| GAP-08 | Repeated process/task observation scripts | A bounded native snapshot with explicit inaccessible/timeout/partial states | maintainers | Browser DOM, vendor APIs, scheduler registration and other external interfaces | 🟡 Windows source and MCP checks pass locally; POSIX is explicit `not_supported`; unreleased |
