# Successor acceptance ledger

Intake: 2026-10-30, Asia/Shanghai. Canonical checkout `D:\GitHub\Cordonix`, branch `main`, clean at `61eb8f2537332e4decb6104e4da2af469c11b6c1`.

Both GitHub and Gitee `main` were independently read back at that revision. GitHub run [40986467787](https://github.com/IoTSharp/Cordonix/actions/runs/40986467787) and every Windows/Linux step passed. This supersedes the earlier remote NOT_RUN note for that revision only. Published v0.0.1/v0.0.2 tags and assets remain immutable.

The supervisor owns public headers, shared protocol, CLI dispatch, build/CI, documentation and Git writes. Workers own disjoint process platform sources/tests, new structured adapter sources/tests, and Linux acceptance scripts. All output directories are distinct under ignored `build/successor-20261030-*`.

| Item | Work and acceptance boundary | State at intake / next evidence |
|---|---|---|
| CDX-09B2 / GAP-06 | Per-child environment overrides on Windows and Linux; native plus real MCP tests; parent unchanged | 🚧 implementation; local/CI/install NOT_RUN |
| CDX-09B3 | Parsed external ripgrep and NUL-safe Git status, literal arguments, failure/encoding/limit semantics | 🚧 implementation; local/CI/install NOT_RUN |
| CDX-09B1 | New-file creation adoption | ✅ source and exact-commit CI; new package and installed-client adoption NOT_RUN |
| CDX-07D | Independent Linux client use and actual filesystem policy | 🟡 prior ext4 14/14 and documents 42/42; independent client/wider workloads pending; ACL/xattr NOT_SUPPORTED |
| CDX-07D | Finite soak and available environment matrix | 🚧 define and run bounded workloads; no unmeasured general stability claim |
| CDX-08D | Real Linux user timer and natural recurrence | 🚧 systemd user manager available; isolate install/task and retain original period |
| CDX-08D | Windows natural hourly firing | 🟡 prior register/manual run/disable 7/7; natural recurrence NOT_RUN |
| CDX-08C | Signing identity and independent signature validation | ⬜ inspect existing configured identity; no new identity/dependency without authorization |
| CDX-08 | Official SPDX schema validation | ⬜ validate generated artifacts against the official schema with available tooling |
| CDX-08 | Build provenance and compiler reproducibility | ⬜ determine executable source evidence and fixed-input compiler checks; packaging-only provenance is insufficient |
| CDX-08 | New version packaging/publication and real upgrade | ⬜ after feature gates; preserve existing versions and receipts |
| CDX-09 / GAP-07 | Actual Codex tools, title rendering and migration | 🟡 current chat is connected to the previous tool surface; avoid stopping shared MCP services |
| GAP-03 | Final comparison/replacement window | 🟡 deliberate single-file boundary; no filesystem compare-and-swap, no speculative lock/transaction work |
| GAP-04 | Crash before memory commit | 🟡 deliberate discard-on-session-loss contract; confirm real disk bytes, no journal or implicit write |
| GAP-05 | Performance / physical IO / token claims | ⬜ fixed baseline required; logical counters alone do not prove these claims |

Each row retains source, local native/MCP, remote CI, package, actual installation, release and timed observation separately. A missing external condition leaves its gate NOT_RUN or UNKNOWN while other rows proceed. Worker reports require independent review and final integration before closure.

## Implemented and locally accepted

Latest state: full Windows 284 / Linux 276 and every step of remote CI `40998386056` on `35350ce` PASS. Both natural hourly recurrences and cleanup PASS. Linux rg 15.2.0 and native Codex CLI 0.162.0 are installed under the explicitly approved user prefix; full rg 29/29 PASS. Model-backed Linux Codex remains blocked by missing authentication. The original intake table above is historical; these later outcomes supersede its implementation and scheduler NOT_RUN rows.

- CDX-09B2: Windows 35 and Linux 39 process checks; 11 shared environment MCP checks; /analyze and ASan/UBSan/leaks PASS. Parent environment and simultaneous children stay isolated.
- CDX-09B3: Windows 26 and Linux Git 17 native adapter checks; actual CLI/MCP Windows 12 and Linux Git 9. Local Linux rg NOT_RUN; CI requires it. Official Python SDK passes 12 new-runtime workflows.
- CDX-07D/GAP-04: independent client 44/44, ext4/tmpfs/9p and 60 rounds over 600.004 seconds. Actual SIGKILL of dirty memory leaves original disk bytes. ACL/xattr remain NOT_SUPPORTED; native Linux Codex package is absent.
- CDX-08D Windows natural recurrence: 09:58:08 and 10:58:09 Asia/Shanghai, 3601 seconds, both LastTaskResult 0. Product disable removed the task and installation; zero matching owned processes. Linux continues its original hourly timer; host suspend and bounded observation adjustment are recorded.
- CDX-08: current ZIP and Linux package (120 checks) PASS; 18 payload and 20 SPDX files. Official SPDX schema and same-environment duplicate compiler output PASS. Embedded revision/dirty is queried from the binary; signing remains NOT_RUN with no configured identity.
- GAP-05: fixed 128 KiB, three 100-call fresh/memory pairs, same response SHA; raw timing/CPU/logical and OS I/O evidence retained. Physical disk I/O/model tokens NOT_MEASURED.
- The user authorized merging the concurrent execution-boundary diagnostics. Their source, tests and docs are included in the unified 284 Windows / 264 local Linux gate, with other personal-skill changes kept outside this product commit.

New-commit remote CI, new release and installed adoption are the next gates. Existing release tags/assets and shared MCP services are preserved.
