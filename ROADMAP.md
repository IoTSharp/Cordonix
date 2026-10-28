# Cordonix Roadmap

Baseline: 2026-10-29, Asia/Shanghai. The IDs below are project planning IDs, not GitHub issues or pull requests.

## Completion standard

An item is complete only when the real product entry point exercises it and the listed gate has evidence. C source review, Windows build, unit tests, encoding corpus, process cleanup, MCP interoperability, performance, recovery, Linux behavior, packaging, and release are separate gates.

## Deliveries

| ID | Delivery | Status | Dependencies | Acceptance | Evidence / boundary |
|---|---|---|---|---|---|
| CDX-01 | Project charter, name and public contracts | complete | none | Name, rules, documentation and license | Cordonix/cdx, all eight agent rules, Apache-2.0 and bilingual README |
| CDX-02 | C11 Windows process core | complete for Windows MVP | CDX-01 | Typed argv, Job ownership, timeout/cancel, bounded raw output | 15 real native-process checks; broader workloads/soak remain separate |
| CDX-03 | Encoding-safe document core | complete for Windows MVP | CDX-01 | UTF-8/BOM, UTF-16 BOM, explicit CP936, SHA, exact edits and commits | 36 core checks; explicit lossless memory transcoding included |
| CDX-04 | C CLI and deterministic JSON contract | complete for Windows MVP | CDX-02, CDX-03 | Native binary, UTF-8 JSON, original request IDs, no shell parsing | `cdx --request`, `--version`; 46 JSON behavior checks |
| CDX-05 | C MCP stdio adapter | implemented; client rollout in progress | CDX-04 | Real stdio, asynchronous cancellation, official SDK and Codex | Native stdio tests and 14 official-SDK workflow checks passed; client reload is distinct |
| CDX-06 | Windows memory workspace | complete for bounded single-file MVP | CDX-03 | Reuse open documents, memory editing/search, explicit commit/conflict | 25 encoding/integration edge checks; external programs still read disk |
| CDX-07 | Linux POSIX backend | planned | CDX-02, CDX-04 | POSIX process ownership and encoding/file parity | No Linux implementation or acceptance claim |
| CDX-08 | Packaging and repeatable releases | partial | CDX-05 | Source license, Windows CI artifact, install guide; later signed release/SBOM | Apache-2.0 and build instructions present; no signed release claimed |

## Next item

Finish CDX-05's actual Codex-session rollout, then use the Windows MVP on bounded daily tasks before expanding the platform or tool surface. Track observed failures with reproducible fixtures. CDX-07 and signing/packaging are independent future deliveries.

## Capability gaps

| ID | Reproduction / scale | Expected and actual | Owner | Blocked scope | Closing evidence |
|---|---|---|---|---|---|
| GAP-01 | C core packaging | reusable header/library and native executable | maintainers | Windows MVP | closed by native build and process checks |
| GAP-02 | memory edits and external compiler reads | explicit commit materializes the memory revision before external execution | maintainers | Windows MVP | closed by MCP edit/convert/commit/readback workflow |
| GAP-03 | noncooperating external writer during final replacement | hash/identity checks detect prior changes; no filesystem compare-and-swap claim | maintainers | stronger transactional guarantees | documented limitation, no speculative locking layer |
| GAP-04 | crash before memory commit | uncommitted session data is discarded | maintainers | crash recovery | journaling not implemented; no implicit writes |
| GAP-05 | performance/token/physical-I/O claims | measure on fixed workloads first | maintainers | public performance claims | not measured; logical load/commit counters only |
