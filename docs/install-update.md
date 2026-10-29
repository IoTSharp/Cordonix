# Cordonix 安装与自动更新

本文定义 Cordonix (`cdx`) 的公开下载约定，也提供可以直接交给 AI 的安装/更新提示词。发布工作流以 `vMAJOR.MINOR.PATCH` 形式的 tag 为输入，例如 `v0.0.1`；没有符合这个格式的 tag 时，安装器不得把分支、commit 或开发版本当成正式版本。

## 发布源和成果物

两个公开源使用相同的仓库名和 tag：

| 优先级 | 源 | Release API | 发布页面 |
| --- | --- | --- | --- |
| 1 | GitHub `IoTSharp/Cordonix` | `https://api.github.com/repos/IoTSharp/Cordonix/releases/latest` | `https://github.com/IoTSharp/Cordonix/releases` |
| 2 | Gitee `IoTSharp/Cordonix` | `https://gitee.com/api/v5/repos/IoTSharp/Cordonix/releases/latest` | `https://gitee.com/IoTSharp/Cordonix/releases` |

每个正式 release 应提供以下资产。`<version>` 是去掉 tag 前缀 `v` 后的 SemVer，例如 tag `v0.0.1` 对应 `0.0.1`：

| 平台 | 压缩包 | 校验文件 |
| --- | --- | --- |
| Windows x64 | `cordonix-<version>-win-x64.zip` | `cordonix-<version>-win-x64.zip.sha256` |
| Linux x64 | `cordonix-<version>-linux-x64.tar.gz` | `cordonix-<version>-linux-x64.tar.gz.sha256` |

安装器应从 release API 返回的资产列表中按**完整文件名**选择资产，并读取同名 `.sha256` 文件。不要根据页面中的第一个附件、重定向后的临时名称或一个模糊的 `*.zip`/`*.tar.gz` 匹配来下载。校验文件使用一行标准 SHA-256 格式：

```text
<64 位十六进制 sha256>  <完整资产文件名>
```

如果某个平台的资产、校验文件、tag 或 API 响应缺失，保留 `NOT_RUN` 或 `UNKNOWN`，不要把另一个平台的二进制改名后当成目标平台安装包。

## 安装流程

1. 识别操作系统和架构。当前正式资产只支持 Windows x64 和 Linux x64；其他架构应明确返回 `NOT_RUN`。
2. 先读取 GitHub `releases/latest`，确认 `tag_name` 完全匹配 `^v[0-9]+\\.[0-9]+\\.[0-9]+$`，再按上表选择两个精确资产。单个 HTTP 请求的连接和读取截止时间为 20 秒，元数据正文最多读取 1 MiB。
3. GitHub 出现 DNS、TLS、超时、非 2xx、响应格式错误或缺少精确资产时，最多切换一次到 Gitee 的对应 API。不要在一个不确定的下载写入后无限重试；每个平台至多一次元数据请求和一次资产下载。
4. 在任务目录中下载压缩包与 `.sha256`，先验证文件名、长度上限（默认 256 MiB）和 SHA-256，再解压。SHA 不匹配时删除本次任务创建的临时文件并返回 `FAIL`；不要覆盖旧安装。
5. 解压到版本目录，运行 `cdx --version`，结果必须等于 `Cordonix cdx <version>`。Windows 还应运行 `cdx --request <temporary-tool-info.json>` 并确认 `platform=windows`、`runtime=C11`；Linux 对应确认 `platform=linux`、`runtime=C11`。
6. 只有上述检查通过，才把版本目录注册到本机的 Codex MCP 配置。保留旧版本目录，更新失败时可以把配置指回旧版本。配置示例见 [`examples/codex-config.toml`](../examples/codex-config.toml)。
7. 输出可审计回执：源平台、tag、资产名、下载 SHA、安装路径、旧版本、新版本、验证结果和任何 `UNKNOWN`/`NOT_RUN` 边界。

建议的安装目录是 Windows `%LOCALAPPDATA%\\Cordonix\\<version>\\`，Linux `~/.local/opt/cordonix/<version>/`。目录只是约定，安装器可以使用用户指定目录；不得写入仓库源码树，也不得静默修改系统级 PATH。

## 更新流程

更新先读取当前配置指向的 `cdx --version`，再执行与安装相同的双源查询。只有最新 tag 的 SemVer 严格大于当前版本时才下载；相等时返回 `up_to_date`，小于当前版本时返回 `older_release` 并保持原配置。下载、解压、版本检查和 `tool_info` 验证均在新版本目录完成，全部通过后再切换配置，切换失败仍保留旧版本可用。

网络失败、两个源都没有正式 tag、校验文件缺失、SHA 不匹配、架构不支持或验证命令超时都必须单独报告。外部写入超时后无法判断是否完成时，状态为 `UNKNOWN`；先检查目标目录和配置，再决定是否重试。更新流程最多处理两个源、一个候选 tag 和一个目标平台，不能使用无界轮询。

## 给 AI 的复制提示词

可以复制 [`examples/cordonix-install-update-prompt.md`](../examples/cordonix-install-update-prompt.md) 作为一次性安装或以后更新的任务说明。提示词要求 AI 优先使用 GitHub、失败时切换 Gitee，按完整资产名和 SHA-256 校验，并将所有结果分成 `PASS`、`FAIL`、`UNKNOWN`、`NOT_RUN`。它也要求使用 Cordonix 的 typed `execute_program` 和保存的 PowerShell 7 脚本，不把路径、参数或远程请求拼成 shell 命令字符串。

当前仓库尚未因本文而声明正式发布。只有 CI 在真实 `v0.0.1` 或更高 SemVer tag 上成功生成并在两个 Release 页面独立回读资产后，安装器才可以报告对应版本已发布。
