# Cordonix Quality Gates

Validated locally on 2026-10-29 (Asia/Shanghai). Each row proves only its stated boundary.

| Gate | What it proves | Current state |
|---|---|---|
| Windows C compile | MSVC C11 warnings as errors | PASS; final `eng\test.cmd` |
| Windows process ownership / argv | Literal Unicode/quotes/newlines, timeout/cancel, Job cleanup | 15/15 PASS |
| Windows encoding / documents | UTF-8/UTF-16/CP936, explicit conversion, SHA/count/conflict, memory/disk boundaries | 36 core + 25 integration checks PASS |
| JSON contract | Malformed UTF-8/JSON, Unicode escapes, original IDs, bounded parsing | Shared 46/46 PASS on Windows and Linux |
| Windows MCP stdio | Initialize/list/call, fourteen titled tools, native file/directory queries and batch encoding verification, process cancellation and active EOF cleanup | 75/75 PASS |
| Linux process ownership / argv | Literal args, pipes, root cwd, closed stdin, timeout/cancel and process-group cleanup | 16/16 PASS, WSL Ubuntu 24.04 / GCC 13.3.0 |
| Linux encoding / documents | Strict encodings, four SHA-256 vectors, edits, conversion, conflict and commit readback | 42/42 PASS, including ASan + UBSan + leak detection |
| Linux MCP / CLI | Shared protocol, line ranges/tail/raw, plus output backpressure, SIGTERM, failed-test cleanup and stdout flags | 79/79 PASS |
| Windows portable package | Same inputs produce identical ZIP; manifest/SPDX/hash checks and execution after extraction to a Unicode path | PASS for `CDX_VERSION=0.0.2`; 17 manifest inputs, 19 SPDX files, 20 relationships; both installers included |
| Linux portable package | Repeated tar.gz, manifest/SPDX/checksum checks and extracted runtime | 115 checks PASS for `0.0.2`, `eng/test-package-linux.sh` |
| Windows installer | Real local HTTP and actual cdx runtime, fallback, stable launcher and failed-update preservation | 12 checks PASS; Gitee public `0.0.1` install, `0.0.2` upgrade and both launcher MCP lifecycles PASS |
| Linux installer | Bounded HTTP fixture, real processes, fallback, upgrade and failed-update preservation | 14 checks plus 11 published-package replay checks PASS; old MCP survives upgrade and a new MCP reports `0.0.2`; WSL direct Internet download NOT_RUN |
| User scheduling | Register and execute recurring updates in an actual user session | Task XML and systemd definition verification PASS; persistent user task registration NOT_RUN in isolated tests |
| Independent clients | Official SDK and actual Codex integration | Earlier Windows baseline: 14 SDK workflows and fresh Codex read lifecycle PASS; changed runtime and title rendering NOT_RUN |
| Filesystem / wider Linux | Independent ext4, ACL/xattr preservation, multiple distributions and soak | NOT_RUN; current file tests use WSL DrvFS and preserve owner/group/mode only |
| Release | GitHub/Gitee releases and independent published-asset readback | `v0.0.1` and `v0.0.2`: each version's ten assets downloaded from both sources, identical SHA-256; compiler reproducibility, official SPDX schema and signing remain NOT_RUN |
| Remote CI | Execution of the changed GitHub Actions workflow | `v0.0.2` run `40892674149` PASS for every build, installer, package and dual-publication job; ordinary branch/tag CI also PASS |

Final native totals: Windows **197** checks; Linux **183** checks. Pilot checks are recorded separately and are not added twice.

Run `eng\test.cmd` for Windows and `sh eng/test-linux.sh` for Linux. The shared CI workflow calls those same native gates and the PowerShell 7 package gate. Test binaries, fixture directories and local receipts belong under `build/`.

The native MCP gate launches the actual executable, rather than mocking the protocol. It does not prove how a particular Codex app version renders MCP `title`. Linux ownership covers the created process group; processes that deliberately leave it are outside this guarantee, and incomplete `/proc` observation retains `cleanup_complete=false`.

One passing gate does not close another. Local tests do not establish installation, release provenance, long-running stability, performance or production acceptance.

The line-range gate covers a real 951–1195 slice, mixed/blank/unterminated lines, empty files/EOF, CP936 and UTF-16/BOM, invalid ranges, byte-budget pagination, long Unicode lines and fresh-disk versus uncommitted-memory reads. The actual TableStore.cs example matched PowerShell Skip 950 / First 245 for all 245 lines and retained the same source SHA.

Tail/raw add 14 shared MCP checks, including exact tail boundaries, blank/mixed/unterminated lines, empty files, complete text exceeding 1000 lines, encoding/BOM handling, decoded UTF-8 budgets, incompatible argument rejection and dirty-memory versus disk behavior. The actual requested Tail 55 and Raw source files also matched PowerShell with unchanged SHAs.

The First 100 example reuses the existing native line-range contract. Direct CLI results for encoding_posix.c First 100 and document_common.c Raw matched PowerShell with unchanged source SHAs. This follow-up added recipes and package contents without changing C source or expanding the native check counts.

Filesystem queries add 12 shared MCP checks for Unicode/space paths, lexical normalization, ordinary-file sizes, directory types, hidden/system filtering, nonrecursive behavior, empty directories, exact/truncated bounds and distinct missing/invalid inputs. Windows additionally passed 18 native NTFS checks; Linux passed 19 isolated ASan/UBSan checks. An invalid UTF-8 directory-entry fixture is NOT_RUN on DrvFS because the filesystem transformed its raw name; the initial fixture failure is retained. Forced permission-denial and 5-second enumeration-expiry cases were not separately triggered. The limits are checked between synchronous OS calls, so this is not a hard preemption guarantee for stalled filesystem drivers.

Batch encoding verification adds 14 shared MCP checks: seven CP936 files, fixed hashes and unchanged bytes, aliases/duplicate entries, malformed encoding/NUL/missing files, incompatible/compatible BOMs, UTF-16, count/path/root preflight, full 32-entry batches, timeout, active cancellation and EOF with NOT_RUN. The Linux production executable also passed all 79 real MCP checks under ASan/UBSan with leak detection. A separate seven-file fixture batch with seven distinct hashes matched the supplied .NET CP936 exception-fallback, NUL and exact-round-trip logic. Real LaneApp files were explicitly outside this turn's validation scope.
