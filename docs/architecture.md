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
CreateProcessW   fork/execv
Job Object       process group + bounded /proc observation
UTF-16/CP936     UTF-8/locale-explicit
ReplaceFileW     rename/fsync
```

The core accepts `program`, `args[]`, absolute `cwd`, timeout and expected exit codes. It inherits the environment; explicit overrides remain a separate capability gap. It returns structured status, exit code, bounded output and cleanup evidence. It never receives a shell command string.

The document workspace keeps raw bytes, decoded text, encoding, BOM, EOL style, original SHA-256 and an in-memory revision. Edits can remain dirty in memory until an explicit commit. Before commit, the core rechecks the disk file identity and hash. External programs see only the materialized disk revision; the runtime must report that boundary clearly.

Windows remains the reference backend. Linux reuses JSON operations and document editing algorithms, with process groups, nonblocking pipes, bounded polling and POSIX file operations in separate platform files. The root stays waitable through group termination to retain its identity. A bounded `/proc` read confirms active group members; incomplete observation cannot report successful cleanup. This is Linux support, not a claim of compatibility with every POSIX operating system.

Windows uses C11 and Windows system libraries. Linux uses libc/iconv and pthreads, with an internal SHA-256 implementation checked against four known vectors. `cdx --mcp` owns one bounded document session (16 documents, at most 8 MiB encoded bytes per file). Original bytes, edited bytes and a UTF-8 view are separate allocations; this is not a 128 MiB process memory guarantee. Same-path opens reuse the existing document. Disk changes are checked on commit, not every cached read.

The stdio adapter reads requests while a program or encoding-verification batch runs in the single active worker, so `notifications/cancelled` can signal that operation. Other tool calls receive busy while the worker is active. Stdout contains JSON-RPC only. An uncommitted document is discarded at session EOF with a stderr diagnostic. The session is bounded to 24 hours or 100,000 messages; a new session requires reopening files. An operation that cannot finish within the shutdown grace period exits the server with an error.

UTF-8 is the wire and in-memory text encoding. The document's selected disk encoding and BOM are preserved unless `workspace_convert_encoding` explicitly changes them in memory. Every conversion is strict: an unrepresentable CP936 character is rejected. The final replacement is a single-file operation, with a documented last-compare/replace interval for external writers; no cross-file transaction or OS sandbox is claimed.

Linux commits accept regular files with one hard link and reject symlinks. They preserve owner/group/mode and update modification time; ACLs and extended attributes are outside the current preservation contract. The local file gate ran on WSL DrvFS, so independent ext4 acceptance remains separate.

MCP tools expose readable Chinese `title` values while keeping their machine identifiers. Titles describe actual boundaries, including memory-only edits and disk commits. Rendering is a client decision; no dynamic per-invocation title or remote-method classification is claimed.

A single bounded line scanner serves both `workspace_read` and `read_file`. The former reads an existing memory revision; the latter opens, reads and releases a fresh disk snapshot without occupying a workspace slot. It scans the bounded UTF-8 view with a 2-second scan deadline and cancellation checkpoints, preserves line delimiters, and emits only complete lines within a bounded output page. Byte paging remains a separate mode.

Tail selection retains at most 1000 line-start offsets in a bounded ring while scanning the decoded view; raw returns that whole view. The same deadline and cancellation checkpoints bound scanning. Tail/raw output is all-or-error within the requested UTF-8 byte budget, while line-range pages retain their existing complete-line pagination semantics.

`cdx/filesystem.h` defines typed read-only filesystem requests and results. The Windows and POSIX implementations normalize absolute paths, check existence, and enumerate bounded immediate entries using their native APIs; common ownership cleanup stays independent of JSON. Protocol dispatch serializes UTF-8 names and explicit status/type/limit metadata. Link names remain lexical, and directory ordering is unspecified.

Encoding verification separates pure path preflight from file reads. Platform normalization and lexical containment helpers serve both filesystem queries and batch scope checks. A batch holds at most 32 normalized paths, then opens and frees one temporary document at a time using the existing strict decode/NUL/round-trip core. It checks the selected decoder against the explicit requested encoding and never commits. Results retain per-entry status, requested/actual encoding evidence, and NOT_RUN after interruption; generic loader failures do not invent individual sub-check outcomes. Execution and verification share the single active worker, while other tool calls report busy.
