# Handoff

Date: 2026-10-29 Asia/Shanghai

## Current state

- Project: Cordonix / cdx, Apache-2.0, version 0.1.0-dev.
- Repository: https://github.com/IoTSharp/Cordonix
- Canonical checkout: `D:\GitHub\Cordonix`.
- Branch: `main`; the previous implementation baseline was `bc988f8`. Read the current revision and working-tree state with `git log -1` and `git status`.
- The user authorized the release/CI implementation and code commit in the current request. GitHub/Gitee push, repository creation and `v0.0.1` publication are pending the local commit and final remote checks.
- The checkout was clean at intake. The process, document and packaging agents used disjoint source ownership; the supervising agent integrated shared protocol, gates and documentation.

## Completed in this session

- CDX-07A: Linux literal-argv execution, process groups, bounded streams, timeout/cancellation, closed-stdin handling and cleanup observation; 16 real process checks.
- CDX-07B: shared document editing algorithm, Linux strict encoding/SHA implementation and explicit single-file commits; 42 real file checks, including sanitizer validation.
- CDX-07C: Linux CLI/MCP, shared JSON/stdio tests, platform build scripts, output backpressure/SIGTERM/EOF cleanup, and restoration of stdout flags.
- CDX-08A: Windows portable package and verification scripts; final package gate PASS.
- CDX-09C: `read_file` one-shot disk line ranges and `workspace_read` line mode. The actual TableStore.cs 951–1195 slice matches all 245 PowerShell reference lines without changing the source SHA.
- CDX-09D: native `tail_lines` and `raw` modes on both read tools. Actual Tail 55 on test_document_posix.c and Raw on process_posix.c match PowerShell and preserve source SHAs; complete-result output limits and mode conflicts are explicit.
- First N uses the existing native line range (`start_line=1`, `line_count=N`). This follow-up added a First 100 example and checked document_common.c Raw plus encoding_posix.c First 100 against PowerShell, with unchanged source SHAs; no C runtime behavior changed.
- CDX-09E: native `list_directory` and `resolve_path`, with literal absolute paths, explicit hidden/system handling, bounded nonrecursive listings and lexical path normalization. The requested LaneApp scratch directory was absent and both operations returned `not_found`; an existing Cordonix directory matched PowerShell's six immediate items and normalized path.
- CDX-09F: native `verify_encoding` with explicit encoding, expected count and lexical allowed-root preflight; no source contents are read until the complete path batch is valid. Seven distinct CP936 fixture files matched the posted .NET verification logic without writes. BOM mismatches, timeout, active cancellation and EOF are covered; real LaneApp verification was not requested for this turn.
- MCP tool definitions now include readable Chinese titles. The client decides how to display them; current Codex UI rendering is not established by a tools/list test.
- Roadmap now distinguishes completed local tasks, partial parent deliveries and unrun adoption/release gates with emoji.
- Tagged release preparation centralizes `CDX_VERSION`, builds Windows/Linux assets from `vMAJOR.MINOR.PATCH` tags, includes installation/update guidance, and provides a bounded GitHub-first/Gitee-fallback prompt.
- `.github/workflows/release.yml` builds and verifies both platform packages, creates the GitHub Release, mirrors `main`/tag to `IoTSharp/Cordonix`, and publishes the same assets to Gitee when `GITEE_TOKEN` is configured.

## Validation

- Final `eng\test.cmd`: PASS, exit 0, Windows 197/197 (15 process + 36 document + 46 JSON + 25 encoding/integration + 75 stdio).
- Final `sh eng/test-linux.sh`: PASS, exit 0, Linux 183/183 (16 process + 42 document + 46 JSON + 79 stdio), WSL Ubuntu 24.04 / GCC 13.3.0.
- Earlier Linux document ASan/UBSan/leak-detection run: 42/42 PASS. The current Linux production binary additionally passed all 79 real MCP checks under ASan/UBSan with leak detection.
- The independent review found and repaired real output-blocking and test-cleanup defects; the new fixtures are included in the final Linux gate.
- Current native receipts: `build/encoding-verify/windows.json` and `linux.json`; independent seven-file .NET comparison: `comparison.json`. Linux ASan/UBSan/leak-detection production-MCP receipt: `sanitized.json` (79/79). Pure normalization/scope fixtures: Windows 19 and POSIX 17 checks PASS, receipt `build/encoding-scope/results.json`. Earlier evidence remains in its existing task directories.
- Filesystem component checks: Windows 18 real NTFS checks PASS; Linux 19 isolated ASan/UBSan checks PASS. Invalid UTF-8 directory-entry testing is NOT_RUN because DrvFS converted the test name to replacement-character bytes; the original 18 PASS / 1 FAIL fixture evidence and explanation are retained in `build/filesystem-posix/verification.md`. A request-path encoding check does not substitute for that OS-entry gate.
- Current `eng/test-package.ps1 -Version 0.0.1`: PASS. Windows ZIP `build/packaging/cordonix-0.0.1-win-x64.zip` passed two-run reproducibility, 14-file manifest, 16-file SPDX/hash relationships and Unicode-path CLI checks. `CDX_VERSION=0.0.1 sh eng/package-linux.sh 0.0.1` also passed and produced the Linux x64 tar.gz plus checksum and metadata. The package now includes the install/update guide and copyable AI prompt.
- Packaging first exposed multiple Git application paths being joined into one executable path. The script now selects the first resolved application and emits UTF-8 diagnostics; the successful rerun and original failure are both retained.
- Earlier baseline SDK/Codex receipts remain historical: 14 official-SDK workflows and a fresh Windows Codex open/read/search/close lifecycle. They do not validate the changed executable or UI titles.
- Remote CI, official SPDX schema validation, signing, release publication, fresh Linux Codex acceptance, Gitee repository creation and ext4/ACL/xattr behavior are NOT_RUN.

## Next order

1. Commit the owned release/CI/docs changes, then create the empty `IoTSharp/Cordonix` Gitee repository, set the local GitHub origin to `https://github.com/IoTSharp/Cordonix.git`, and push `main` plus `v0.0.1`.
2. Read back both GitHub and Gitee release pages and all Windows/Linux assets after the tagged workflow completes; retain any missing/unknown publication state.
3. Verify the new runtime in a fresh Codex session, including whether the client displays MCP titles. Keep CDX-07D, signing and CDX-09B remaining tool-adapter gaps separate.
4. Declare file encoding before editing. Known GBK uses explicit CP936; AUTO is BOM/strict UTF-8 interpretation, not GBK detection. Commit memory edits before external builds.

## Local integration changes and boundaries

Outside this public repository, the authorized skill migration retired `powershell7-safe-exec`, narrowed `codex-local-workflow` to collaboration/handoff, and made `cordonix` the preferred execution/read/edit path. The local skill inventory was regenerated and validated while preserving unrelated dirty files. No skill commit/push, runtime replacement, MCP configuration change or shared MCP-process termination occurred.

The inspected LaneApp CP936 editing script was read only. Its general hash-checked editing can migrate to cdx, but the 16-document session bound and explicit per-file commits remain; no multi-file atomicity or new-file creation is implied.

## Cleanup and ownership

All final Cordonix execution receipts report cleanup_complete=true. Tests remove their own disposable files and process fixtures. The final Windows/Linux audit found no active build executables (`build/encoding-verify/cleanup.json`); the sanitizer output directory contains only its test executable and no diagnostic reports (`sanitizer-log-check.json`). Build products, prior reproduction requests, receipts and the portable ZIP remain ignored under `build/`; disposable build/comparison/sanitizer scripts were removed. No real LaneApp source or posted task script was changed. Existing installed runtimes and unrelated services remain owned by their original sessions.
