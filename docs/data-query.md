# Structured data queries

`data_query` performs read-only native queries over JSON, JSONL and CSV. Supply
exactly one absolute `path` or inline `text`, plus `format`. It does not invoke a
shell, interpreter or database engine, and does not write source files.

```json
{
  "path": "C:/reports/jobs.json",
  "format": "json",
  "records_path": "/jobs",
  "where": {"field": "/state", "equals": "failed"},
  "select": ["/id", "/state", "/details/message"],
  "offset": 0,
  "limit": 50
}
```

`operation` defaults to `rows`. `count` returns the number of matching records;
`group_count` requires `group_by` and returns scalar values with their counts,
in first-seen order. `offset` and `limit` page rows or groups. `select` applies
only to rows and produces an object whose keys are the supplied pointers.
Missing projections become `null`. Missing group values have `missing:true`
and remain distinct from an explicit JSON null. A container at `group_by`
returns `invalid_group_value`.

All field references, including `records_path`, use RFC6901 JSON Pointer:
the empty string selects the current value, `/a~1b` selects key `a/b`, and
`/~0x` selects key `~x`. Array indices have canonical decimal spelling.
`records_path` applies only to JSON and must locate an array (each element is
a record) or object (one record). JSONL treats each nonblank physical line as
one record. Blank physical lines are skipped; malformed lines stop the scan
and report `error_line` with completed records retained.

`where` compares one scalar by type and value. Number matching and grouping
use the original JSON token: `1`, `1.0` and `1e0` are distinct. Serialization
preserves number spelling, so large integer IDs are never rounded through
the parser's double representation. Missing fields do not satisfy an equality
predicate, including equality to null. Sorting, arithmetic, mutation and
arbitrary query expressions are outside this contract.

CSV requires a first header record with 1–128 unique, nonempty names. All cells
remain strings, including empty cells and numeric-looking text. Commas,
CRLF/LF/CR record endings, quoted embedded newlines and doubled quotes are
supported. Quotes must delimit the whole field; spaces after a closing quote
are rejected. Header duplicates, missing names and ragged rows are explicit
errors. Use pointers such as `/Name` to select or filter header names.

`encoding` applies only to files and uses the existing strict document loader:
`auto`, `utf-8`, `utf-8-bom`, `utf-16-le`, `utf-16-be`, `cp936` or `gbk`.
Automatic decoding accepts BOM or strict UTF-8. Embedded NUL is rejected.

The decoded input limit is 1 MiB (`max_input_bytes`, default 1 MiB). JSON uses
the existing parser's depth 32 and 65,536-node limits. `max_records` defaults
to 10,000 and permits at most 100,000. `max_groups` defaults to 256 and permits
at most 1,024, with a shared 1 MiB budget for distinct serialized group keys.
`limit` defaults to 100 and permits 1–1,000. `max_output_bytes` defaults to
65,536 and permits 1,024–1,048,576, including the complete operation result
but excluding its MCP envelope. A row that cannot fit is never cut into invalid
JSON. The caller can raise the budget and repeat the read.

Results include `examined`, `matched`, `returned`, `scan_complete`, `truncated`,
`next_offset` and `stop_reason`. Counts and group counts are exact for the
examined prefix; only `scan_complete:true` confirms the whole source was
examined. `record_limit`, `group_limit`, `output_limit`, parse errors,
`timed_out` and `cancelled` preserve completed output and identify an incomplete
scan. When only serialization of already-counted groups hits `output_limit`,
the completed scan remains `scan_complete:true`. Pagination alone can set
`truncated:true` while leaving the scan complete.
`next_offset` is returned only after at least one row/group was returned.
Repeat pagination against an unchanged source; offsets are not snapshot tokens.

`timeout_ms` defaults to 10 seconds and permits 1–60,000 ms. Cancellation and
deadlines are checked between bounded steps. Existing synchronous file loading
and one bounded JSON parse are not forcibly interrupted. The document loader
may read up to its existing 8 MiB disk cap before the smaller decoded input
budget is checked. No background worker or persistent workspace document is
created.
