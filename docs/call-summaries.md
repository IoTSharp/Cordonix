# Per-call summaries

Cordonix derives one short Chinese summary from a tool call's existing arguments. A client can show which program runs, the action recognized from its arguments, and the file, directory, repository or line range involved.

| Existing request | Example summary |
|---|---|
| `execute_program` with `git.exe status` in Cordonix | `git.exe：查看仓库修改状态 · Cordonix` |
| `execute_program` with `git diff` | `git：查看代码差异 · Cordonix` |
| `execute_program` with `where.exe $PATH:git.exe $PATH:rg.exe` | `where.exe：查找程序路径 · git.exe、rg.exe · Cordonix` |
| `execute_program` with `dotnet test` | `dotnet：运行 .NET 测试 · Cordonix` |
| `execute_program` with PowerShell 7 `-File .../eng/test.ps1` | `pwsh.exe：运行 PowerShell 脚本 · eng/test.ps1 · Cordonix` |
| `read_file`, start 61, count 30 | `读取文件 · Services/DingTalkOptions.cs · 第 61～90 行` |
| `list_directory` for the eng directory | `列出目录 · Cordonix/eng` |
| `resolve_path` for the checkout | `确认路径是否存在 · GitHub/Cordonix` |

The summary describes the requested operation. The existing status, exit code and result fields determine whether it succeeded. Read ranges describe the requested page; actual returned lines and EOF remain in the operation result. Full paths remain available in the original arguments. Calls with only a document ID display that ID. Unknown commands retain the executable name without inventing the script's business purpose.

## Result metadata

The MCP `tools/call` result keeps the original `content` array and adds:

```json
{
  "_meta": {
    "cordonix/summary": "git.exe：查看仓库修改状态 · Cordonix"
  }
}
```

This field is presentation metadata. A host should retain it for its UI and keep it out of model context. Cordonix does not add a summary field to tool inputs, `content`, `structuredContent`, or CLI `--request` operation results. It makes no model request. The formatter uses a 512-byte output buffer, bounded string/path/argument scans and code point boundaries; it never reads a script or queries another process to explain a command. The metadata adds a small local protocol payload, rather than another model task.

## Progress before execution

A client that wants the summary while a call runs supplies the standard progress token in `params._meta`:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "read_file",
    "arguments": {"path":"D:/project/Services/DingTalkOptions.cs","start_line":61,"line_count":30},
    "_meta": {"progressToken":"read-1"}
  }
}
```

Before the operation, the server sends:

```json
{
  "jsonrpc":"2.0",
  "method":"notifications/progress",
  "params":{
    "progressToken":"read-1",
    "progress":0,
    "message":"读取文件 · Services/DingTalkOptions.cs · 第 61～90 行"
  }
}
```

The final tool result carries the same summary. Tokens may be strings or numbers; numeric spelling is retained without conversion through floating point. Calls without a token receive no new notification. An unsupported token type is ignored for presentation and leaves operation validation unchanged.

## Client rendering and validation

A client can use the progress message while running and result `_meta["cordonix/summary"]` after completion, falling back to the fixed tool title. Grouped calls can list their individual summaries without asking the model to write another overview. Cordonix cannot force a host to replace its collapsed-row labels.

The formatter tests exercise real C output, including command meaning, Unicode truncation and literal paths. Real MCP stdio checks cover the metadata envelope, progress ordering and exact tokens, unchanged file/process content and validation errors. Model API calls are not needed for these tests. Desktop rendering, token accounting in a particular host, release publication and installed-client adoption remain separate gates.

OpenAI's [tool result reference](https://developers.openai.com/apps-sdk/reference/#tool-results) describes `_meta` as component-only metadata, distinct from model-visible `content` and `structuredContent`. That documented separation does not by itself establish that a particular Codex desktop version will render this custom summary key.
