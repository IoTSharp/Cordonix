# Cordonix Quality Gates

| Gate | What it proves | Current state |
|---|---|---|
| C compile | MSVC C11 build with warnings as errors | local MSVC PASS; `eng\build.cmd` |
| argv fidelity | Unicode, spaces, quotes, newlines and literal shell characters survive | native process suite PASS |
| process ownership | timeout/cancel and descendant cleanup are limited to the created Job | included in 15 native process checks |
| encoding | strict UTF-8/UTF-16/CP936 read/write, conversion and round-trip | 36 file checks + 25 edge checks PASS |
| document conflict | stale hash, ambiguous match and external writer produce zero write | real file checks PASS; no filesystem CAS claim |
| memory workspace | repeated reads use memory and commits are explicit | same-path cache, locked-disk read and commit readback PASS |
| JSON contract | malformed UTF-8/JSON, Unicode escapes, IDs and bounded parse behavior | 46 native checks PASS |
| MCP | initialize, tools/list, tools/call, cancellation and EOF | 17 real stdio checks + 14 official SDK workflow checks PASS; fresh Codex session also verified the installed runtime and memory read lifecycle |
| Linux | POSIX backend matches the public result contract | planned |
| packaging | install, version, provenance, SBOM and license are reproducible | Apache-2.0/source builds present; signed release/SBOM planned |

One passing gate does not close another. In particular, a local C test does not prove Codex interoperability, long-running stability, package installation or production acceptance.

Run `eng\test.cmd` on the final tree. It builds the production executable and calls real native fixtures for processes, encoding, edits and stdio. The official MCP Python SDK is used as an independent local acceptance client only; it is not a production dependency or required for the native CI suite.
