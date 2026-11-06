# File discovery

`find_files` runs a supplied absolute ripgrep executable with a literal argument
array. It uses `rg --files --null`; it does not parse a shell command or implement
its own recursive walker. The working directory, roots and glob arguments are
separate fields. The program, working directory and roots must be absolute paths.
Paths are validated and passed unchanged, so filesystem traversal through links
and `..` retains its native meaning. `include_hidden` adds ripgrep's `--hidden`
flag. Positive glob arguments can also explicitly include hidden files, following
ripgrep's own precedence rules.
Ripgrep's ordinary ignore rules still apply. The adapter does not pass `--follow`;
explicit roots and ancestor symlinks retain ripgrep's native traversal behavior.

The operation captures a bounded scan, validates UTF-8, sorts the returned names
by their UTF-8 bytes, removes duplicate names, and then applies `offset` and
`limit`. Names retain the form emitted by ripgrep. Newlines and literal `$()` in
discovered names remain ordinary JSON string content. Invalid UTF-8 filenames
return `encoding_error` without a partial page.

Pagination requires a complete scan. If ripgrep times out, is cancelled, exceeds
`capture_limit_bytes`, or produces more than `max_files` names, the result has
`scan_complete: false` and contains no page. This prevents a partial, differently
ordered scan from looking like a valid offset page. Each request performs a new
scan; offsets do not provide a filesystem snapshot or a cursor across requests.

`max_output_bytes` independently bounds the public JSON result. A complete scan
can therefore return a smaller page with `truncated: true`; `next_offset`
identifies the first item not returned. `total_files` is available only after a
complete scan. A successful empty scan has status `no_match`.

| Field | Default | Bounds |
| --- | --- | --- |
| `roots` | Required | 1–32 absolute paths |
| `globs` | Empty | At most 32 literal rg glob arguments |
| `offset` | 0 | 0–100,000 |
| `limit` | 200 | 1–1,000 |
| `max_files` | 10,000 | 1–100,000 captured names before deduplication |
| `timeout_ms` | 10,000 | 1–60,000 |
| `capture_limit_bytes` | 1,048,576 | 1,024–4,194,304 |
| `max_output_bytes` | 65,536 | 1,024–1,048,576 |

`max_output_bytes` covers the tool's JSON result, excluding the MCP envelope. If
the next name cannot fit, `next_offset` can equal the requested offset; increase
the output budget to make progress. Diagnostic stderr is a bounded UTF-8 prefix;
`stderr_truncated` indicates omitted bytes.

This operation does not change files, launch a persistent process, disable ignore
rules, or infer which ripgrep executable to use.
