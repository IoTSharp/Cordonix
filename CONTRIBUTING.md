# Contributing to Cordonix

Thank you for helping improve Cordonix (`cdx`). Start with [AGENTS.md](AGENTS.md), the [roadmap](ROADMAP.md), and the [quality gates](docs/quality-gates.md).

## Scope a change

Choose one observed defect or roadmap item. Include the trigger, current behavior, expected behavior and a small reproducible input. Preserve unrelated working-tree changes. Discuss a new dependency, framework, build system or unproven defensive mechanism before implementing it.

The current production code is C11 on Windows, built with MSVC. Linux is planned. Runtime code must not require Python or PowerShell; existing development build scripts remain the build entry points.

## Implementation expectations

- Keep executable paths, arguments, document paths, contents and encodings in separate fields. Validate external requests at their entry boundary and pass established invariants internally.
- Keep the normal flow easy to read. Extract functions for reuse, complex logic or a meaningful domain concept; avoid wrappers used only to make code look uniform.
- Comment constraints that are not evident from the code. Update generated output through its generator or source data.
- Preserve program exit codes, dirty-memory state and explicit commits. Never silently replace undecodable data or automatically repeat an uncertain mutation.

## Validate behavior

Use real bounded local processes and temporary source fixtures when practical. Tests must call the implementation rather than repeat its algorithm. Regressions involving argv, CP936/GBK, UTF-8/UTF-16, BOM, EOL, stale hashes, cancellation and descendant cleanup need a focused observable check.

Record the commands, compiler/runtime version, exit result and cleanup outcome. A Windows unit-test result does not prove Linux support, MCP interoperability, long-running stability or a performance claim. Do not reduce a failed gate to make a contribution appear complete.

## Submit a pull request

Use a title such as `fix(workspace): preserve the original BOM on commit`. Describe the concrete problem, resulting behavior and relevant validation. Update documentation and the changelog when the public contract changes. Do not include compiled artifacts, personal settings, raw agent transcripts, credentials or local receipts.

Security-sensitive reports follow [SECURITY.md](SECURITY.md), not a public proof containing private data.
