# Cordonix

**面向 AI 编程代理的原生执行与内存工作区。**

[![Windows 检查](https://github.com/maikebing/Cordonix/actions/workflows/ci.yml/badge.svg)](https://github.com/maikebing/Cordonix/actions/workflows/ci.yml)

[English](README.md) · [路线图](ROADMAP.md) · [参与贡献](CONTRIBUTING.md) · [安全说明](SECURITY.md)

Cordonix，简称 **cdx**，为智能体提供直接调用本地程序和处理源文件的工具。程序参数保持原样，文本编码明确可控，多次读取和编辑可以留在内存中，直到显式提交。

当前实现是 **Windows 上的纯 C11 MVP**，可执行程序运行时不依赖 Python 或 PowerShell。开发构建使用 MSVC，Linux 后端列入后续路线。Cordonix 是独立开源项目，不属于 OpenAI 官方产品。

## 解决什么问题

AI 编程过程中，一些反复出现的失败来自工具边界：Shell 转义改变了参数，旧编码被替换成乱码，过期补丁覆盖了新内容，取消命令后仍留下子进程。

Cordonix 把这些边界变成明确的请求和结果：

- 通过 `program`、`args[]`、`cwd` 和超时启动原生程序，用 Windows Job Object 管理本次创建的进程树。
- 文件只需首次读入，后续读取、搜索和编辑使用内存中的版本，提交由调用方明确发起。
- 严格处理 UTF-8、UTF-16、BOM、换行符和显式指定的 CP936/GBK，不使用静默丢字节或替换字符。
- 编辑前检查预期 SHA-256 和匹配次数，提交前再次核对磁盘文件版本。
- 分别报告程序退出码、输入错误、编码错误、文件冲突和内存脏状态。

Git、ripgrep、编译器和 Codex 的 `apply_patch` 仍是配套工具。Cordonix 负责它们与智能体之间的执行、文档边界。

## 当前状态

项目处于开发 MVP 阶段。维护者先在自己的 Windows 编程工作流中使用，再逐步扩大平台和发行支持。

| 部分 | 当前证据 | 尚未覆盖的边界 |
|---|---|---|
| Windows 进程核心 | 本地 15 项检查通过 | 更广泛应用和长时间负载仍需验证 |
| 编码与内存文档核心 | 36 项核心检查与 25 项编码/接线边界检查通过 | 外部工具读取磁盘，看不到未提交的内存修改 |
| CLI 与 MCP | 10 个原生工具；真实 stdio 和官方 SDK 互操作验收通过 | 尚未形成稳定发布接口 |
| Linux | 已规划 | 没有 Linux 实现或验收结论 |
| 公共安装包 | 已规划 | 本页不提供尚不存在的安装器或 Release 下载 |

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
| `workspace_open` | 按显式编码或严格 BOM/UTF-8 检测读入文件 |
| `workspace_read` | 从内存版本分页读取 UTF-8 文本 |
| `workspace_edit` | 根据 SHA 与匹配次数预演或执行精确内存替换 |
| `workspace_convert_encoding` | 预演或执行 UTF-8/UTF-16/CP936 的无损内存转码 |
| `workspace_commit` | 校验当前磁盘版本后提交单个文档 |
| `workspace_status` | 查询内存版本和脏状态，不重新加载磁盘 |
| `workspace_search` | 在内存版本中查找 UTF-8 字面文本 |
| `workspace_close` | 释放文档，显式处理未提交内容 |

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

## 构建与运行

克隆 [maikebing/Cordonix](https://github.com/maikebing/Cordonix)，使用仓库已有的 MSVC 构建入口：

```text
eng\build.cmd
eng\test.cmd
build\bin\cdx.exe --version
build\bin\cdx.exe --request examples\request.json
```

使用 Visual Studio 2022 或更新版本的 x64 开发命令行，并安装 C++ 桌面开发工作负载；脚本也能识别已知 Community/Enterprise 安装位置。构建使用 `/TC /std:c11 /W4 /WX /O2 /MT`，输出 `build/bin/cdx.exe`。运行此程序不需要 Python 或 PowerShell。

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
- 外部操作发出后超时，不代表它没有完成；再次执行前应先回读状态。
- 没有测量基线时，不承诺性能、磁盘 I/O 或 token 的降低比例。

## 参与贡献

先阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 与 [AGENTS.md](AGENTS.md)。每项贡献围绕一个真实问题或路线任务，遵循已有工具链，用实际运行结果和受控 fixture 验证行为。

使用 [Apache-2.0](LICENSE) 许可证。
