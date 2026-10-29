# Cordonix 安装/更新提示词

请把我当作 Cordonix (`cdx`) 的安装与更新助手，完成一次**可验证**的安装或更新，并在最后给出简洁回执。

## 目标

- 根据当前操作系统和 CPU 架构选择一个目标：Windows x64 或 Linux x64。
- 先查询 GitHub `IoTSharp/Cordonix`，失败后再查询 Gitee `IoTSharp/Cordonix`。
- 只接受 tag `vMAJOR.MINOR.PATCH`，并从 release 资产列表中按完整文件名选择：
  - Windows：`cordonix-<version>-win-x64.zip` 和同名 `.sha256`
  - Linux：`cordonix-<version>-linux-x64.tar.gz` 和同名 `.sha256`
- 下载后严格校验 SHA-256，解压到新的版本目录，运行 `cdx --version` 和 `tool_info`，所有检查通过后才切换 Codex MCP 配置。
- 已安装版本相同则报告 `up_to_date`；远端版本更旧则报告 `older_release`；不要降级或覆盖可用的旧版本。

## 来源顺序和截止时间

1. GitHub API：`https://api.github.com/repos/IoTSharp/Cordonix/releases/latest`
2. Gitee API：`https://gitee.com/api/v5/repos/IoTSharp/Cordonix/releases/latest`

每个源最多一次元数据请求和一次目标资产下载，连接/读取截止时间 20 秒，元数据最多 1 MiB。GitHub 的 DNS、TLS、超时、非 2xx、JSON 错误或缺少精确资产时，才切换到 Gitee；不要无限重试。优先使用 API 返回的资产下载地址，不要猜测附件路径。

## 执行约束

- 使用 Cordonix 的 typed `execute_program`，将程序、参数、工作目录和超时作为独立字段；需要 PowerShell 时只能调用 `C:\Program Files\PowerShell\7\pwsh.exe -NoProfile -File <绝对脚本路径>`。
- 不调用 Windows PowerShell 5.1，不把 `$()`、管道、重定向或远程脚本嵌入 `-Command` 字符串，不把代码形状的值当作文件路径。
- 下载、校验和解压只写入本次任务创建的临时目录与用户安装目录；不要修改仓库源码、删除旧版本、全局改 PATH 或清理不属于本任务的进程/文件。
- 下载资产和校验文件后，解析校验文件中的文件名必须与实际资产完全相同，SHA-256 必须是 64 位十六进制并完全匹配。
- 验证失败时保留旧安装，删除已确认属于本任务的临时文件；外部写入超时无法判断结果时标记 `UNKNOWN`，先回读状态再决定下一步。

## 安装/更新步骤

1. 读取当前配置和 `cdx --version`（若没有安装，当前版本为 `none`）。
2. 查询两个来源中的第一个可用正式 release，验证 tag 格式和目标平台资产。
3. 下载压缩包与 `.sha256` 到有界临时目录，核验长度和 SHA-256。
4. 解压到新版本目录：Windows 建议 `%LOCALAPPDATA%\\Cordonix\\<version>\\`，Linux 建议 `~/.local/opt/cordonix/<version>/`。
5. 运行 `cdx --version`，必须得到 `Cordonix cdx <version>`；再运行 `tool_info`，确认 `name=Cordonix`、`version=<version>`、目标 `platform` 和 `runtime=C11`。
6. 只有第 5 步通过才更新 Codex MCP 配置中的 `command`/`args`，然后保留旧版本目录作为回滚点。
7. 生成回执并停止，不要因为“下载成功”就声称安装成功。

## 回执格式

```text
status: PASS | FAIL | UNKNOWN | NOT_RUN
operation: install | update | up_to_date | older_release
source: github | gitee | none
tag: vX.Y.Z | none
asset: exact filename | none
sha256: verified value | none
previous_version: value | none
installed_version: value | none
install_path: absolute path | none
config_updated: true | false | unknown
notes: network, missing asset, unsupported architecture, timeout or other boundary
```

`PASS` 只表示本机的下载、校验、解压和运行时回读全部通过；它不代表 GitHub/Gitee 之外的部署或现场验收。
