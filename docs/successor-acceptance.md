# Successor acceptance ledger

Date: 2026-10-30, Asia/Shanghai. Canonical checkout: `D:\GitHub\Cordonix`. Intake was clean at `61eb8f2537332e4decb6104e4da2af469c11b6c1`; both remotes and every step of its CI run `40986467787` were independently verified. Three workers used disjoint source/output ownership; their findings were reviewed and integrated by the supervisor.

The implementation was delivered in `8ee549a`, followed by CI prerequisite and process-audit fixes. Release **v0.0.3** points to `8894b74dbf1254172c6142017ebd99092eee8dee`; signed-provenance release **v0.0.4** points to `34115ba52cf15c465a92516e9cc9aa284840c65a`, annotated tag object `a3732687187e67d82d5ed2a7a82893a833402c23`. Both repositories retain the same source/tag identities. Existing tags and assets were not moved or overwritten.

| Item | Accepted result | Boundary |
|---|---|---|
| CDX-09B2 / GAP-06 | ✅ Child-only environments: 35 Windows / 39 Linux native process checks and 11 shared MCP checks | Parent stays unchanged; null removal, empty values and platform case rules are distinct |
| CDX-09B3 | ✅ Parsed external Git/rg: 26 Windows / 29 Linux native checks; 12 Windows / 9 Linux independent CLI/MCP checks | Git/rg remain external programs; raw-byte, NUL, timeout, cancel and limit semantics are explicit |
| CDX-09B1 | ✅ New-file creation delivered in published packages and exercised by actual Codex clients | No overwrite, existing parent, explicit encoding and immediate write contract |
| CDX-09G | ✅ Execution-boundary feedback integrated with separate process/encoding states and cleanup counts | Persistent browsers/services use their lifecycle owner; counts are observations |
| CDX-07D | ✅ Independent Linux client 44/44; actual ext4/tmpfs/9p; 60 rounds over 600.004 seconds | One WSL Ubuntu distribution plus hosted Ubuntu CI; longer soak/other distributions are NOT_RUN |
| CDX-07D metadata | ✅ Declared owner/group/mode/mtime/conflict policy verified on ext4 | ACL/xattr preservation remains NOT_SUPPORTED |
| CDX-08D Windows | ✅ Original hourly task naturally succeeded at 09:58:08 and 10:58:09, interval 3601 seconds, exit 0 | Isolated task/install/process cleanup PASS; no default permanent task enabled |
| CDX-08D Linux | ✅ Original hourly timer naturally repeated at 11:17:49; monotonic interval 3641.826 seconds, exit 0 | Host suspend explains longer wall time; no manual service start or shortened product period; cleanup PASS |
| CDX-08A packages | ✅ Windows ZIP and Linux package 120 checks; 18 payload / 20 SPDX files | Same-input archive reproducibility is separate from compiler reproducibility |
| CDX-08 provenance | ✅ Actual binary exposes build revision/dirty; tagged release requires matching clean source; two same-environment compiler builds match | Cross-toolchain/independent-builder reproducibility is NOT_RUN |
| CDX-08 SPDX | ✅ Official SPDX 2.3 schema plus inventory/content verification | Reviewed schema SHA-256 `239208b7ac287b3cf5d9a9af23f9d69863971102a5e1587a27a398b43490b89b` |
| CDX-08B release | ✅ v0.0.3 and v0.0.4 dual publication; 10 and 12 product assets respectively downloaded from each host and equal by SHA-256 | Gitee-generated source archives are not product release assets |
| CDX-08C signing | ✅ Four public originals pass strict identity/subject verification; four one-byte tampered copies are rejected | gh 2.95 hides low-level rejection detail; no network/timeout was counted as a negative pass. Not Authenticode |
| CDX-09 actual clients | ✅ Actual 0.0.3-to-0.0.4 upgrade on both platforms; installed v0.0.4 native Codex app-server 12/12 each and all 17 tools called | Old connections remain on 0.0.3 and old binary bytes survive; new connections use 0.0.4; no model request needed |
| CDX-09 registration | ✅ Another user-authorized migration has retired the old registration and configured an 18-tool local development runtime | Preserve that newer `query_processes` work; do not replace it with the 17-tool signed release |
| GAP-03 | 🟡 Documented final comparison/replacement window | No filesystem compare-and-swap; stronger transactions were not silently added |
| GAP-04 | ✅ Actual SIGKILL after dirty memory leaves original disk bytes | Deliberate discard contract; crash recovery/journal NOT_SUPPORTED |
| GAP-05 | ✅ Fixed 128 KiB, three alternating 100-call fresh/memory pairs with identical response SHA | CPU, elapsed and logical/OS I/O measured; physical disk I/O and model tokens NOT_MEASURED |
| GAP-07 | 🟡 All 17 human-readable titles retained in actual Codex inventory | GUI title rendering and dynamic per-call labels remain UNKNOWN |

Release CI [41002515571](https://github.com/IoTSharp/Cordonix/actions/runs/41002515571) passed every version/build/package/SPDX/reproducibility/installer/attestation/GitHub/Gitee step for v0.0.4. Its ordinary CI also passed. The signed release's native totals are **284 Windows** and **276 Linux**; later uncommitted process-query work has a separate test/version boundary.

The user explicitly approved scoped installation of ripgrep 15.2.0 and official Codex CLI 0.162.0 in WSL, and separately approved the reviewed GitHub OIDC signing action. The dependency versions, sources, archive hashes and exact installed paths are retained in local receipts. No credentials, HOME/CODEX_HOME or global PATH were changed. Existing shared MCP processes were not stopped.

Evidence is under ignored `build/successor-20261030-{supervisor,env,adapters,linux,performance,windows-scheduler,windows-scheduler-regression,signing,release}/`. Original FAIL, timeout and partial-download receipts remain alongside corrected results. Release/readback files and installed runtimes are deliverables; disposable task fixtures and private processes are reclaimed separately.
