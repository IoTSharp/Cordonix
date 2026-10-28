# Cordonix Architecture

Cordonix separates the public protocol from platform-specific process and file APIs.

```text
Codex / MCP client
        |
  JSON-RPC stdio adapter
        |
  typed contract and state model
        |
  Cordonix core
   /              \
Windows backend   POSIX backend
CreateProcessW   execve/posix_spawn
Job Object       process group/cgroup
UTF-16/CP936     UTF-8/locale-explicit
ReplaceFileW     rename/fsync
```

The core accepts `program`, `args[]`, absolute `cwd`, timeout, expected exit codes and explicit environment values. It returns structured status, exit code, bounded output and cleanup evidence. It never receives a shell command string.

The document workspace keeps raw bytes, decoded text, encoding, BOM, EOL style, original SHA-256 and an in-memory revision. Edits can remain dirty in memory until an explicit commit. Before commit, the core rechecks the disk file identity and hash. External programs see only the materialized disk revision; the runtime must report that boundary clearly.

The first Windows implementation is the reference because the current failures are Windows-specific. Linux reuses the protocol and state semantics but implements process groups/cgroups, file descriptor polling and POSIX-safe file operations in separate platform files.

The shipped MVP uses only C11 and Windows system libraries. `cdx --mcp` owns one bounded document session (16 documents, at most 8 MiB encoded bytes per file). Original bytes, edited bytes and a UTF-8 view are separate allocations; this is not a 128 MiB process memory guarantee. Same-path opens reuse the existing document. Disk changes are checked on commit, not every cached read.

The stdio adapter reads requests while one program executes in a worker so `notifications/cancelled` can signal the owned Job. Other tool calls receive busy while execution is active. Stdout contains JSON-RPC only. An uncommitted document is discarded at session EOF with a stderr diagnostic. The session is bounded to 24 hours or 100,000 messages and then exits cleanly; a new session requires reopening files.

UTF-8 is the wire and in-memory text encoding. The document's selected disk encoding and BOM are preserved unless `workspace_convert_encoding` explicitly changes them in memory. Every conversion is strict: an unrepresentable CP936 character is rejected. The final replacement is a single-file operation, with a documented last-compare/replace interval for external writers; no cross-file transaction or OS sandbox is claimed.
