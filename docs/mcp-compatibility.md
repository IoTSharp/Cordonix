# MCP compatibility audit — 2026-11-06

The current Cordonix stdio adapter declares `2025-06-18`, requires initialization before tool calls, and returns typed operation JSON inside a text content block. This audit does not claim compatibility with every requirement of that revision or with the latest revision.

## Verified specification and current behavior

The official `specification/latest` endpoint returned HTTP 307 to `2026-07-28` during this audit. Its [versioning contract](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning) describes request-level version/capability metadata, `server/discover`, explicit unsupported-version errors and optional legacy interoperability. This changes the request lifecycle; replacing the current version string would not implement it.

The official [2025-06-18 lifecycle](https://modelcontextprotocol.io/specification/2025-06-18/basic/lifecycle) permits a server that does not support a client's requested version to return a version it supports. The client decides whether to continue. Current Cordonix always offers `2025-06-18`; existing local fixtures and the retained installed client exercise this legacy path. A fresh current-host session against the development binary and a modern SDK compatibility matrix have not been demonstrated by the raw protocol fixtures alone.

Under the [2025-06-18 tools contract](https://modelcontextprotocol.io/specification/2025-06-18/server/tools), text results are supported and structured content is optional. A supplied output schema requires conforming structured content; backward compatibility guidance also recommends a serialized text copy. Therefore adding both representations cannot be assumed to reduce model-visible output. Current Cordonix advertises no output schemas and emits no structured content.

There is a separate confirmed gap: ordinary tool failures are represented by the inner operation `status`, but the outer result generally omits `isError`. Unknown tools likewise pass through the operation dispatcher instead of the documented protocol-error path. Preserve distinctions such as `no_match`, HTTP status versus transport failure, expected process exit codes, preview, partial results and unknown mutation outcome when defining the mapping. A blanket `status != ok` rule would misclassify successful operations.

## Decision and next acceptance

Keep the current handshake and text representation for the HTTP/batch slice. A protocol migration changes both platform adapters, client fallback, cancellation and session-owned documents; the first-wave output measurements provide no evidence that this broader change improves the existing host. CDX-11E is **audit complete, implementation/client acceptance open**, not complete protocol interoperability.

The smallest next change is an explicit tool/error classification contract with real client tests, followed by a separate decision about supporting both protocol eras. Compare current text-only, structured-plus-text and negotiated structured output on the same real host before selecting an output migration. Native finite background jobs must also be compared with the now-documented Tasks extension before designing their protocol surface. No new dependency or installed-server replacement is authorized by this audit.

Acceptance must include version mismatch/fallback, unknown tool/invalid arguments, representative operation failure/partial/success states, actual SDK/client interpretation, cancellation, EOF ownership and serialized output limits. Retain raw messages and any client-visible duplicate content. HTTP retrieval receipts, including headers and complete specification bodies, are in ignored `build/three-stage-20261105/coordinator/mcp-*.json`; final local evidence is in [HANDOFF](../HANDOFF.md).
