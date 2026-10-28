# Handoff

Date: 2026-10-29 Asia/Shanghai

## Current state

- Project name: Cordonix
- Short form: cdx
- Repository: https://github.com/maikebing/Cordonix
- Canonical location: `D:\GitHub\Cordonix`
- Branch: `main`; inspect `git log -1` and `git status` for live state.
- License: Apache-2.0
- Version: 0.1.0-dev (Windows MVP, not a stable release)

## Completed

- Pure C11 Windows process core, strict encoding/document core, CLI and ten-tool MCP adapter.
- Memory open/read/search/edit/encoding conversion and explicit single-file commit; same-path cache reuse and conflict detection.
- Eight project agent rules, English/Chinese README, contributor/security guidance and Apache-2.0.
- Native CI workflow uses the same `eng\test.cmd` gate as the local build.

## Next order

1. Continue first-party use from the fresh Codex session that verified the installed C11 runtime and open/read/search/close lifecycle. Keep actual usage receipts separate from test receipts.
2. Continue one roadmap item at a time. Linux, crash recovery, benchmarks and signed releases are not implied by this Windows MVP.
3. Preserve the explicit commit boundary; external tools read disk and session EOF discards dirty memory.

## Validation

Local core gates: process 15, document 36, JSON 46, encoding/integration edges 25 and real native MCP stdio 17: 139 checks passed. The independent official SDK acceptance completed 14 workflow checks. Fresh Codex-session acceptance confirmed version 0.1.0-dev/C11, one document load, zero commits, memory search and unchanged disk SHA. Windows GitHub Actions passed the implementation commit. Run the final `eng\test.cmd` for changed code and inspect GitHub Actions for the current remote result.

## Authorization and cleanup

The user authorized this project, its public repository, local checkout, Codex integration and collaborative implementation. Do not infer authority for unrelated repositories, production hosts or future external releases. Test fixtures are created under `build/`, cleaned by their owners, and excluded from Git. Runtime installation/configuration stays outside the public repository; no private transcripts, credentials or personal backups belong here.
