# Cordonix Quality Gates

Native, ext4 and current development-package gates validated locally on 2026-10-30 (Asia/Shanghai). Published release/upgrade results below retain their 2026-10-29 release boundary. Each row proves only its stated boundary.

| Gate | What it proves | Current state |
|---|---|---|
| Windows C compile | MSVC C11 warnings as errors | PASS; final `eng\test.cmd` |
| Windows process ownership / argv | Literal Unicode/quotes/newlines, timeout/cancel, Job cleanup | 15/15 PASS |
| Windows encoding / documents | UTF-8/UTF-16/CP936, explicit conversion, SHA/count/conflict, memory/disk boundaries | 36 core + 25 integration checks PASS |
| JSON contract | Malformed UTF-8/JSON, Unicode escapes, original IDs, bounded parsing | Shared 46/46 PASS on Windows and Linux |
| New-file creation | Strict encoding/bytes, no overwrite, missing parents, cancellation and cleanup | 20/20 native checks on each platform, plus eight shared MCP checks; Windows /analyze and Linux ASan/UBSan/leaks PASS |
| Windows MCP stdio | Initialize/list/call, fifteen titled tools, native creation/read/directory queries and encoding verification, process cancellation and active EOF cleanup | 83/83 PASS |
| Linux process ownership / argv | Literal args, pipes, root cwd, closed stdin, timeout/cancel and process-group cleanup | 16/16 PASS, WSL Ubuntu 24.04 / GCC 13.3.0 |
| Linux encoding / documents | Strict encodings, four SHA-256 vectors, edits, conversion, conflict and commit readback | 42/42 PASS, including ASan + UBSan + leak detection |
| Linux MCP / CLI | Shared protocol, creation/line ranges/tail/raw, plus output backpressure, SIGTERM, failed-test cleanup and stdout flags | 87/87 PASS |
| Windows portable package | Same inputs produce identical ZIP; manifest/SPDX/hash checks and execution after extraction to a Unicode path | Current `0.1.0-dev` PASS; 17 manifest inputs, 19 SPDX files; published `0.0.2` remains historical release evidence |
| Linux portable package | Repeated tar.gz, manifest/SPDX/checksum checks and extracted runtime | Current `0.1.0-dev`: 115 checks PASS, `eng/test-package-linux.sh`; published `0.0.2` remains historical release evidence |
| Windows installer | Real local HTTP and actual cdx runtime, fallback, stable launcher and failed-update preservation | 12 checks PASS; Gitee public `0.0.1` install, `0.0.2` upgrade and both launcher MCP lifecycles PASS |
| Linux installer | Bounded HTTP fixture, real processes, fallback, upgrade and failed-update preservation | 14 checks plus 11 published-package replay checks PASS; old MCP survives upgrade and a new MCP reports `0.0.2`; WSL direct Internet download NOT_RUN |
| User scheduling | Register, manually run and disable an actual user task | Windows isolated public-v0.0.2 task 7/7 PASS, LastTaskResult=0, task/install/process cleanup PASS; natural hourly firing and Linux user registration NOT_RUN |
| Independent clients | Official SDK and actual Codex integration | Earlier Windows baseline: 14 SDK workflows and fresh Codex read lifecycle PASS; changed runtime and title rendering NOT_RUN |
| Filesystem / wider Linux | Actual ext4 filenames and documented metadata policy | 14 MCP checks and 42 document checks PASS on ext4; owner/group/mode preserved, ACL/xattr NOT_SUPPORTED. Other distributions and soak NOT_RUN |
| Release | GitHub/Gitee releases and independent published-asset readback | `v0.0.1` and `v0.0.2`: each version's ten assets downloaded from both sources, identical SHA-256; compiler reproducibility, official SPDX schema and signing remain NOT_RUN |
| Remote CI | Execution of the changed GitHub Actions workflow | Current local changes NOT_RUN remotely; historical `v0.0.2` run `40892674149` PASS for every build, installer, package and dual-publication job |

Final native totals: Windows **225** checks; Linux **211** checks. Pilot checks are recorded separately and are not added twice. The 14-check ext4 gate and seven-check Scheduler acceptance remain separate totals.

Run `eng\test.cmd` for Windows and `sh eng/test-linux.sh` for Linux. The shared CI workflow calls those same native gates, package checks, and `timeout --kill-after=10s 75s sh eng/test-filesystem-linux.sh "$PWD/build/linux/bin/cdx" --receipt "$PWD/build/linux/filesystem.json"`. The ext4 gate uses Python 3 and `findmnt`, has an internal 60-second timeout and accepts `--pilot`; other filesystems return NOT_RUN/77. Test binaries and receipts belong under `build/`; the ext4 gate removes its owned `/tmp` fixture and MCP process.

The native MCP gate launches the actual executable, rather than mocking the protocol. It does not prove how a particular Codex app version renders MCP `title`. Linux ownership covers the created process group; processes that deliberately leave it are outside this guarantee, and incomplete `/proc` observation retains `cleanup_complete=false`.

One passing gate does not close another. Local tests do not establish installation, release provenance, long-running stability, performance or production acceptance.

The line-range gate covers a real 951–1195 slice, mixed/blank/unterminated lines, empty files/EOF, CP936 and UTF-16/BOM, invalid ranges, byte-budget pagination, long Unicode lines and fresh-disk versus uncommitted-memory reads. The actual TableStore.cs example matched PowerShell Skip 950 / First 245 for all 245 lines and retained the same source SHA.

Tail/raw add 14 shared MCP checks, including exact tail boundaries, blank/mixed/unterminated lines, empty files, complete text exceeding 1000 lines, encoding/BOM handling, decoded UTF-8 budgets, incompatible argument rejection and dirty-memory versus disk behavior. The actual requested Tail 55 and Raw source files also matched PowerShell with unchanged SHAs.

The First 100 example reuses the existing native line-range contract. Direct CLI results for encoding_posix.c First 100 and document_common.c Raw matched PowerShell with unchanged source SHAs. This follow-up added recipes and package contents without changing C source or expanding the native check counts.

Filesystem queries add 12 shared MCP checks for Unicode/space paths, lexical normalization, ordinary-file sizes, directory types, hidden/system filtering, nonrecursive behavior, empty directories, exact/truncated bounds and distinct missing/invalid inputs. Windows additionally passed 18 native NTFS checks; Linux passed 19 isolated ASan/UBSan checks. The earlier invalid UTF-8 directory-entry fixture was NOT_RUN on DrvFS because it transformed the raw name; that initial failure is retained. The new actual-ext4 gate stores a raw `0xff` filename and proves the MCP response is `encoding_error` without replacement. Forced permission-denial and 5-second enumeration-expiry cases were not separately triggered. The limits are checked between synchronous OS calls, so this is not a hard preemption guarantee for stalled filesystem drivers.

Batch encoding verification adds 14 shared MCP checks: seven CP936 files, fixed hashes and unchanged bytes, aliases/duplicate entries, malformed encoding/NUL/missing files, incompatible/compatible BOMs, UTF-16, count/path/root preflight, full 32-entry batches, timeout, active cancellation and EOF with NOT_RUN. The previous Linux production executable passed its 79-check MCP suite under ASan/UBSan with leak detection; that is historical evidence, separate from the current new-file sanitizer checks. A separate seven-file fixture batch with seven distinct hashes matched the supplied .NET CP936 exception-fallback, NUL and exact-round-trip logic. Real LaneApp files were outside that validation scope.
