# Cordonix

**面向 AI 编程代理的原生执行与内存工作区。**

[![Windows 检查](https://github.com/IoTSharp/Cordonix/actions/workflows/ci.yml/badge.svg)](https://github.com/IoTSharp/Cordonix/actions/workflows/ci.yml)

[English](README.md) · [路线图](ROADMAP.md) · [安装与更新](docs/install-update.md) · [参与贡献](CONTRIBUTING.md) · [安全说明](SECURITY.md)

Cordonix，简称 **cdx**，为智能体提供直接调用本地程序和处理源文件的工具。程序参数保持原样，文本编码明确可控，多次读取和编辑可以留在内存中，直到显式提交。

通过[安装与更新说明](docs/install-update.md)或[可复制 AI 提示词](examples/cordonix-install-update-prompt.md)安装 Windows x64 ZIP 或 Linux x64 tar.gz。安装器检查 SHA-256 和实际程序版本，GitHub 不通时转向 Gitee，并可启用每小时自动检查。正式 tag 使用 `vMAJOR.MINOR.PATCH`；[GitHub Release](https://github.com/IoTSharp/Cordonix/releases) 和 [Gitee Release](https://gitee.com/IoTSharp/Cordonix/releases) 提供相同成果物。

当前实现是 **支持 Windows 和 Linux 的纯 C11 MVP**，可执行程序运行时不依赖 Python 或 PowerShell。Windows 构建使用 MSVC；Linux 使用 GCC、系统 libc/iconv 和 pthread。Cordonix 是独立开源项目，不属于 OpenAI 官方产品。

## 解决什么问题

AI 编程过程中，一些反复出现的失败来自工具边界：Shell 转义改变了参数，旧编码被替换成乱码，过期补丁覆盖了新内容，取消命令后仍留下子进程。

Cordonix 把这些边界变成明确的请求和结果：

- 通过 `program`、`args[]`、`cwd` 和超时启动原生程序，用 Windows Job Object 或 Linux 进程组管理本次创建的工作负载。
- 文件只需首次读入，后续读取、搜索和编辑使用内存中的版本，提交由调用方明确发起。
- 严格处理 UTF-8、UTF-16、BOM、换行符和显式指定的 CP936/GBK，不使用静默丢字节或替换字符。
- 编辑前检查预期 SHA-256 和匹配次数，提交前再次核对磁盘文件版本。
- 分别报告程序退出码、输入错误、编码错误、文件冲突和内存脏状态。

Git、ripgrep、编译器和 Codex 的 `apply_patch` 仍是配套工具。Cordonix 负责它们与智能体之间的执行、文档边界。

## 当前状态

项目处于开发 MVP 阶段。Windows 已有实际 Codex 使用证据；Linux 已在 WSL Ubuntu 24.04 完成本地原生测试和 MCP stdio 验证。

| 部分 | 当前证据 | 尚未覆盖的边界 |
|---|---|---|
| Windows 进程核心 | 本地 35 项检查通过，包含子进程环境隔离 | 更广泛应用和长时间负载仍需验证 |
| 编码与内存文档核心 | 36 项核心检查与 25 项编码/接线边界检查通过 | 外部工具读取磁盘，看不到未提交的内存修改 |
| CLI 与 MCP | 17 个带可读标题的原生工具；Windows 96 项、Linux 100 项真实 stdio 检查，以及 12 项当前官方 Python SDK 检查 | 先前 Windows SDK/Codex 证据与本次修改树分开；Codex 是否显示新标题待验证 |
| 新文本文件 | 两平台各 20 项原生检查，以及 8 项共享 MCP 检查 | 已有路径不覆盖，父目录必须存在 |
| Linux | WSL Ubuntu 下 276 项完整检查（含 Git/rg 适配器）；独立客户端 44/44，覆盖 ext4/tmpfs/9p 以及 60 轮、600 秒有限长稳 | 原生 rg 与 Codex CLI 已安装；Linux Codex 模型调用仍需认证。ACL/xattr 不支持，多发行版和更长时段仍单独验收 |
| Windows/Linux 便携包 | ZIP/tar.gz、校验和、checkout 来源、SPDX 文件清单与解压运行门禁；v0.0.1 已在两端发布并核对全部附件 | 包未签名；官方 SPDX schema 已验证，构建 revision/dirty 从实际二进制回读；这些不是签名构建证明 |

上述结果是本地验证，不是性能基准，也不代表不会发生错误。各项验收边界见[质量门禁](docs/quality-gates.md)。

## 内存工作区如何使用

```text
workspace_open -> workspace_read / workspace_search
               -> workspace_edit（预演或修改内存）
               -> workspace_commit -> 外部构建 / Git 操作
               -> workspace_close
```

`workspace_edit` 默认只预演。设置 `dry_run=false` 后只改内存；`workspace_commit` 才会重新读取磁盘文件标识和 SHA，随后替换单个文件。检测到冲突时，磁盘内容保持原样。

结果明确区分 `dirty_in_memory` 与 `written_to_disk`。关闭脏文档需要先成功提交，或者明确设置 `discard=true`。会话结束会丢弃未提交内存，不会偷偷写回磁盘。

内存文档属于一个运行中的会话。MCP 保持进程常驻；多次独立执行 `cdx --request` 不会共享文档 ID 或未提交修改。

## 工具接口

| 工具 | 用途 |
|---|---|
| `tool_info` | 查询版本、平台及文档加载/提交计数 |
| `execute_program` | 原样传递 argv，限制输出和超时，回收任务所属进程 |
| `create_file` | 按严格编码创建新文本文件，不覆盖已有路径，父目录须存在 |
| `list_directory` | 列出目录的直接子项，可包含隐藏项并限制结果数 |
| `resolve_path` | 规范化已存在的绝对文件系统路径，返回路径及类型 |
| `verify_encoding` | 按声明编码批量验证磁盘文件，可约束数量和目录范围 |
| `workspace_open` | 按显式编码或严格 BOM/UTF-8 检测读入文件 |
| `workspace_read` | 按字节/行范围、末尾 N 行或完整原文读取内存版本 |
| `read_file` | 按路径和编码，一次读取磁盘的行范围、末尾 N 行或完整原文 |
| `workspace_edit` | 根据 SHA 与匹配次数预演或执行精确内存替换 |
| `workspace_convert_encoding` | 预演或执行 UTF-8/UTF-16/CP936 的无损内存转码 |
| `workspace_commit` | 校验当前磁盘版本后提交单个文档 |
| `workspace_status` | 查询内存版本和脏状态，不重新加载磁盘 |
| `workspace_search` | 在内存版本中查找 UTF-8 字面文本 |
| `workspace_close` | 释放文档，显式处理未提交内容 |

工具定义现在提供中文 `title`，例如“执行本地程序”“编辑文本（仅内存）”“提交文件到磁盘”，原来的工具标识保持不变。客户端决定是否采用标题；服务端不能为每次调用指定动态标题。Cordonix 执行本地程序，不会仅凭脚本名称把操作标成“远程方法”或“新增文件”。

## 程序执行边界

`execute_program` 管理一次有界调用。主进程退出后，即使退出码为 0，仍会回收本次所属的剩余进程。需要持续运行的浏览器、守护进程和服务应由现有的生命周期管理入口启动。`completed` 表示命令退出状态符合预期，并不保证它启动的浏览器继续存在。结果同时提供 `active_processes_before_cleanup`、`active_processes_after_cleanup` 和 `cleanup_complete`。

`args` 是扁平的原生 argv 数组。PowerShell 7 的 `-File` 不会把后续 argv 自动重组为脚本的命名 `[string[]]` 参数；应在保存的脚本中构造数组，或使用目标脚本明确支持的数据接口。`output_encoding` 只指定捕获字节的解码方式，不改变子程序的输出编码。遇到 `encoding_error`，先查看 `process_status`、`exit_code` 和保留的 Base64 字节，再决定是否重试。详见[执行边界与示例](docs/execution-boundaries.md)。

## 子进程环境与外部工具适配器

`execute_program` 的可选 `env` 对象只覆盖子进程环境，例如 `{"HTTPS_PROXY":"http://127.0.0.1:7890","REMOVE_ME":null,"EMPTY":""}`。其余变量从父环境继承；父进程不变。null 删除变量，空字符串保留空值。最多 64 项；名称不空、不含 `=`，最多 255 个原生字符/字节，值最多 32767。Windows 名称不区分大小写，POSIX 区分；非法编码及按平台规则重复的名称在启动前拒绝。

`search_text` 要求 rg 的绝对 `program`、`cwd`、1～32 项 `patterns` 和 1～8 个绝对 `roots`。可选 `globs` 最多 16 项，`literal` 默认 true。每个模式单独传 `-e`，根路径前传 `--`，使用 `--no-config --json`，结果保留 rg 的 `{text}` 或 Base64 `{bytes}` 字段。退出码 1 返回 `no_match`。默认 `encoding=none` 保留字节；显式 UTF-8/UTF-16/GBK 转换由 rg 完成，可能替换非法字节。严格编码验证仍使用 `verify_encoding`。

`git_status` 要求 Git 的绝对 `program` 与 `cwd`，解析 `--no-optional-locks status --porcelain=v2 --branch -z` 的分支、普通、重命名、冲突和未跟踪记录；重命名前后路径分别保留。输出路径允许空格、引号、换行。非法 UTF-8 返回 `encoding_error` 和原始 Base64，不猜测替换编码。`untracked` 可选 `normal`（默认）、`all` 或 `no`。

两个适配器都有超时、取消和输出上限：`timeout_ms` 为 1～60000（默认 30000），`output_limit_bytes` 为 1～262144，`max_entries` 为 1～1000（默认 200）。搜索另有 `max_matches_per_file` 为 1～1000（默认 50），结果会披露该限制。截断不能写成完整成功；Git 与 rg 仍须单独安装。

## 创建新文本文件

`create_file` 会立即创建一个新文件。传入绝对字面路径 `path`、UTF-8 文本 `content` 和可选 `encoding`（默认 `utf-8`）。支持 UTF-8、UTF-8 BOM、带 BOM 的 UTF-16LE/BE，以及 CP936/GBK；拒绝 `auto`。允许空文件；NUL、非法 UTF-8 或目标编码无法表示的字符在写入前拒绝。C API 的输入 UTF-8 与编码后字节均最多 8 MiB，CLI/MCP 还受现有 JSON 帧大小限制。

```json
{"operation":"create_file","arguments":{"path":"D:\\project\\new.txt","content":"中文\r\n","encoding":"cp936"}}
```

父目录必须已经存在，已有文件、目录或链接返回 `already_exists`。实现先写入并刷新同目录临时文件，再以不覆盖目标的方式发布；正文和换行原样保留。新文件使用 Windows 目录继承权限或 Linux `0600` 权限，不创建持久内存文档。

结果包含 `written_to_disk`、`cleanup_complete`、编码后 `bytes`、`encoding`，成功时附带 `disk_sha256`。有界 I/O 操作之间检查取消和 15 秒写入期限，不能强制抢占阻塞的同步系统调用。发布后发生错误时仍可能返回 `written_to_disk=true`，应先回读再决定是否重试。`document_commits` 仍只统计内存文档提交。

## 按行读取

`Get-Content ... | Select-Object -Skip 950 -First 245` 对应 `start_line: 951`、`line_count: 245`。新工具 `read_file` 直接按绝对路径和编码读取磁盘快照，也支持一次性 CLI 请求：

```text
build\bin\cdx.exe --request examples\read-lines.json
```

[请求示例](examples/read-lines.json) 中的路径需对应本地文件；已知 GBK 必须指定 `cp936`。如果文件已经通过 `workspace_open` 打开，将相同行参数传给 `workspace_read` 即可读取当前内存版本，包含尚未提交的修改；`read_file` 则始终读取磁盘并释放临时文档。

行号从 1 开始。返回文本保留 LF、CRLF、CR；空文件为零行，末尾换行不会多算一行。结果包含实际行数、下一行 `next_line`（到达 EOF 时为 null）、`eof`、编码、换行、SHA 和 `source`。默认最多 200 行、32768 UTF-8 字节；可设置到 1000 行、262144 字节。字节预算不足时只返回完整行并标记 `truncated=true`；首行过长则返回 `line_too_long`，可增加预算或使用原有字节分页。行模式不能与 `offset`/`limit` 混用。

读取前 N 行直接使用原生行范围：`start_line: 1, line_count: N`。[First 100 示例](examples/read-first.json) 直接读取 `encoding_posix.c`，不经过 Shell 管道。

| PowerShell 读取方式 | Cordonix 读取参数 |
|---|---|
| `Get-Content -First N` | `start_line: 1, line_count: N` |
| 读取文件后 `Select-Object -Skip M -First N` | `start_line: M + 1, line_count: N` |
| `Get-Content -Tail N` | `tail_lines: N` |
| `Get-Content -Raw` | `raw: true` |

## 读取末尾行和完整原文

`tail_lines: 55` 对应 `Get-Content -Tail 55`；`raw: true` 对应 `Get-Content -Raw`。`read_file` 和 `workspace_read` 都支持，分别读取磁盘快照和当前内存版本。可用 [read-tail.json](examples/read-tail.json)、[read-raw.json](examples/read-raw.json) 配合 `cdx --request` 直接调用，路径按本机位置调整。

Tail 支持 1～1000 行，文件行数不足时返回全部；Raw 不受行数限制。两者保留原始换行符及末尾换行，返回解码后的文本，编码 BOM 通过元数据表示。成功结果包含 `mode`、实际起始行和行数，并保证 `eof=true`、`next_line=null`、`truncated=false`。

输出仍受 `max_bytes` 限制，默认 32768、最多 262144 个 UTF-8 字节。Tail/Raw 必须完整放入预算；不足时返回 `output_limit`、`bytes_required` 和 `max_bytes`，不返回半份 `text`。可提高预算或改用范围分页。Tail/Raw 不能混用，也不能与 `start_line`、`line_count`、`offset`、`limit` 混用；`raw=false` 保持原有行范围语义。

## 列出目录与解析路径

`list_directory` 接受绝对字面路径 `path`、默认 false 的 `include_hidden` 和默认 200、范围 1～1000 的 `max_entries`。设置 `include_hidden: true` 会包含 Windows HIDDEN/SYSTEM 属性项或 POSIX 点文件，对应文件系统中的 `Get-ChildItem -Force`。结果包含名称、类型、普通文件大小（其他类型为 null）、hidden/system 标志。只列直接子项，不递归，不保证顺序；观察到额外可见项时标记 `truncated=true`。空目录成功返回空数组。

`resolve_path` 规范化绝对路径中的分隔符、`.`、`..`，核对目录项存在后返回 `path` 和 `type`，保留链接名称。根路径可以是指向目录的链接；枚举时不遍历子项链接。Windows reparse 项和 POSIX 符号链接标为 `link`。这两个工具处理文件系统字面路径，不展开通配符或 PowerShell Provider。

不存在返回 `not_found`，与空目录区分；权限、编码、I/O、超时和取消也分别返回。枚举最多 100000 步，5 秒截止与取消在同步系统调用之间检查。[list-directory.json](examples/list-directory.json) 和 [resolve-path.json](examples/resolve-path.json) 可通过 `cdx --request` 或 MCP 使用。

## 编码必须明确

`auto` 识别 BOM 或严格 UTF-8，不会把无法解析的 UTF-8 自动猜成 GBK。旧中文源码应明确指定 `cp936` 或 `gbk`：

```json
{
  "operation": "workspace_open",
  "arguments": {
    "path": "D:\\project\\legacy.c",
    "encoding": "cp936"
  }
}
```

目标编码无法表示新增字符时，工具返回编码错误，不会为了写入成功而丢字节、替换成问号。`workspace_edit` 默认保留当前编码。`workspace_convert_encoding` 在提供最新 SHA 和 `dry_run=false` 后显式改变内存编码，仍须 commit 才落盘。含 Emoji 的 UTF-8 文本转换成 CP936 会被拒绝，内存和磁盘均不变。纯 ASCII 同时兼容多种编码，AUTO 是解释策略，不能证明历史编码；已知 GBK 文件应明确声明。

## 批量验证编码

`verify_encoding` 复用文档核心检查新读取的磁盘字节：严格解码、文本无 NUL、重新编码后字节完全一致。传入 1～32 个绝对路径条目 `paths` 和显式 `encoding`，不接受 `auto`。可选 `expected_count` 必须等于路径条目数，重复条目照常计数。可选 `allowed_root` 按规范化路径做词法范围检查，不约束符号链接的物理目标。整批参数、路径和范围预检完成后才读取文件内容。

替代七文件 CP936 脚本时，使用 `encoding: "cp936"`、`expected_count: 7`、对应 workflow 目录的 `allowed_root`，并传入调用方原来的七个路径。[可运行示例](examples/verify-encoding.json) 使用仓库中的一个 UTF-8 文件，可按实际清单调整。

不相容的 BOM 返回 `encoding_mismatch`；声明 UTF-8 时也接受 UTF-8 BOM。通过表示与声明编码相容，不证明历史编码。逐项结果保留状态及 PASS/FAIL，成功加载文档时附带字节数、SHA 和实际编码。汇总包含请求条目数、已尝试数、通过/失败/未运行数和 `written_to_disk: false`。只有整批成功时，严格解码、无 NUL、往返检查汇总才为 PASS；否则保留 UNKNOWN，不猜测未细分的失败步骤。

默认期限 10000 ms，可设置 1～60000 ms，在文件操作之间检查。MCP 主动取消和 EOF 通过既有工作线程处理，尚未检查的条目标记 NOT_RUN；不能强制抢占已阻塞的系统文件调用。验证不输出源码正文、不修改或提交文件，也不占用持久文档槽位。

## 构建与运行

克隆 [IoTSharp/Cordonix](https://github.com/IoTSharp/Cordonix)，使用仓库已有的 MSVC 构建入口：

```text
eng\build.cmd
eng\test.cmd
build\bin\cdx.exe --version
build\bin\cdx.exe --request examples\request.json
```

使用 Visual Studio 2022 或更新版本的 x64 开发命令行，并安装 C++ 桌面开发工作负载；脚本也能识别已知 Community/Enterprise 安装位置。构建使用 `/TC /std:c11 /W4 /WX /O2 /MT`，输出 `build/bin/cdx.exe`。运行此程序不需要 Python 或 PowerShell。

Linux 安装好 GCC、libc 开发头文件和 GNU `timeout` 后使用：

```sh
sh eng/build-linux.sh
sh eng/test-linux.sh
build/linux/bin/cdx --version
```

两平台使用相同的 JSON 请求和十七个 MCP 工具。Linux 使用绝对 UTF-8 路径，执行错误字段为 `posix_error`；Windows 保留 `win32_error`。Linux 程序动态链接系统库。Windows 完成 `eng\test.cmd` 后，在 PowerShell 7 运行 `./eng/test-package.ps1` 构建和验证本地便携 ZIP，详见[打包说明](docs/packaging.md)。

CLI 入口为：

```text
cdx --version
cdx --request C:\absolute\request.json
cdx --mcp
```

`--request` 处理一次 UTF-8 JSON 操作并返回结果；`--mcp` 启动持续的 stdio 会话。参考 [Codex 配置示例](examples/codex-config.toml)，将 command 改成实际可执行文件绝对路径，再新建 Codex 会话加载服务。

## 使用边界

- 缓存减少 Cordonix 会话内部的重复读取，不接管所有操作系统文件访问，也不会让编译器自动读取内存版本。
- 一个会话最多保留 16 个文档，每份编码后的文件最多 8 MiB；原始字节、修改后字节和 UTF-8 视图会额外占用内存。文本分页最多 32 KiB。
- 提交以单文件为单位，不宣称跨文件事务原子性。
- `workspace_status` 展示会话已知状态；提交时才进行新的磁盘变化检查。
- 进程归属用于管理生命周期，不是安全沙箱。被授权启动的程序保留其正常系统权限。
- Linux 清理限定为本次创建的进程组，并有界读取 `/proc` 核实。主动脱离进程组的程序不在保证范围；观察不完整时返回 `cleanup_complete=false`。
- Linux 文档提交支持只有一个硬链接的普通文件，拒绝符号链接；保留 owner/group/mode，写入时间正常更新。当前不保留 ACL 和扩展属性，请勿用于需要这些元数据保证的文件。
- 外部操作发出后超时，不代表它没有完成；再次执行前应先回读状态。
- 没有测量基线时，不承诺性能、磁盘 I/O 或 token 的降低比例。

## 参与贡献

先阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 与 [AGENTS.md](AGENTS.md)。每项贡献围绕一个真实问题或路线任务，遵循已有工具链，用实际运行结果和受控 fixture 验证行为。

使用 [Apache-2.0](LICENSE) 许可证。
