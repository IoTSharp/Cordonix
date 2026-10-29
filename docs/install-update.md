# Cordonix 安装与自动更新

把[安装提示词](../examples/cordonix-install-update-prompt.md)复制给 AI，或让 AI 读取这个链接，就可以安装 Cordonix (cdx) 并开启每小时检查新版本。安装器能从零启动，不要求事先安装 cdx。运行时仍是纯 C；Windows 安装器及稳定启动器需要 PowerShell 7，Linux 安装器需要 Python 3.9+ 标准库和 GNU timeout。

## 下载入口

| 内容 | GitHub（优先） | Gitee（回退） |
| --- | --- | --- |
| 仓库 | [IoTSharp/Cordonix](https://github.com/IoTSharp/Cordonix) | [IoTSharp/Cordonix](https://gitee.com/IoTSharp/Cordonix) |
| Release | [发布与附件](https://github.com/IoTSharp/Cordonix/releases) | [发布与附件](https://gitee.com/IoTSharp/Cordonix/releases) |
| Windows 安装脚本 | [install.ps1](https://raw.githubusercontent.com/IoTSharp/Cordonix/main/install/install.ps1) | [install.ps1](https://gitee.com/IoTSharp/Cordonix/raw/main/install/install.ps1) |
| Linux 安装脚本 | [install.sh](https://raw.githubusercontent.com/IoTSharp/Cordonix/main/install/install.sh) | [install.sh](https://gitee.com/IoTSharp/Cordonix/raw/main/install/install.sh) |
| AI 提示词 | [复制提示词](https://github.com/IoTSharp/Cordonix/blob/main/examples/cordonix-install-update-prompt.md) | [复制提示词](https://gitee.com/IoTSharp/Cordonix/blob/main/examples/cordonix-install-update-prompt.md) |

先将对应脚本下载并保存到本地，检查来源后执行，避免把网络响应直接送进解释器。安装器查询 GitHub https://api.github.com/repos/IoTSharp/Cordonix/releases/latest；GitHub 网络、元数据或资产下载失败时，再使用 Gitee https://gitee.com/api/v5/repos/IoTSharp/Cordonix/releases?page=1&per_page=20 。Gitee 在最多 20 个条目中选择数值最高的稳定版本，忽略预发布和草稿。

Tag 必须是规范的 vMAJOR.MINOR.PATCH，例如 v0.0.1。两个发布源提供相同 tag、源提交和二进制资产：

| 平台 | 压缩包 | 校验文件 |
| --- | --- | --- |
| Windows x64 | `cordonix-<version>-win-x64.zip` | 同名 `.zip.sha256` |
| Linux x64 | `cordonix-<version>-linux-x64.tar.gz` | 同名 `.tar.gz.sha256` |

Linux 发布包由 Ubuntu 24.04 构建，动态链接 glibc；其他发行版需通过实际运行验证。当前不提供 ARM64、macOS 或 musl/Alpine 二进制。Gitee 还会列出自动生成的源码压缩包，安装器只匹配上表中的完整资产名。

## Windows

使用 PowerShell 7 执行已保存的脚本：

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoLogo -NoProfile -File 'C:\absolute\install.ps1' -EnableAutoUpdate
```

默认安装根目录为 `%LOCALAPPDATA%\Cordonix`，版本位于 `versions\<version>`；`current.json` 记录当前版本，稳定启动器是 `launch.ps1`。有需要可传入 `-InstallRoot 'D:\Tools\Cordonix'`、`-Source gitee` 或 `-Proxy 'http://127.0.0.1:7890'`。默认 `-Source auto` 会在 GitHub 不通时转向 Gitee；自动任务也保留选定的 source 和显式代理。

首次安装成功后，将以下区块合并到 ~/.codex/config.toml，把 USER 替换成真实用户目录；保留已有服务器和其他配置：

```toml
[mcp_servers.cordonix]
command = 'C:\Program Files\PowerShell\7\pwsh.exe'
args = ['-NoLogo', '-NoProfile', '-File', 'C:\Users\USER\AppData\Local\Cordonix\launch.ps1', '--mcp']
startup_timeout_sec = 15
tool_timeout_sec = 300
```

-EnableAutoUpdate 通过 Windows Task Scheduler COM 注册当前用户任务，每小时检查一次，每次最多 3 分钟，重叠运行会跳过；需要该用户登录。任务名带安装目录哈希。脚本不调用 Windows PowerShell 5.1，也不修改系统 PATH。

```powershell
# 只查询版本；手动更新；停止自动检查（保留安装文件）。
.\install.ps1 -CheckOnly
.\install.ps1
.\install.ps1 -DisableAutoUpdate
```

-TaskDefinitionOnly 只输出任务 XML，不注册任务，可用于审查计划任务定义。任务实际注册是否成功，与二进制安装是否成功分别报告。

## Linux

```sh
sh /absolute/install.sh --enable-auto-update
```

默认安装根目录是 `~/.local/opt/cordonix`，可用 `--root /absolute/path` 更换。版本位于 `versions/<version>`，稳定路径是 `<root>/current/cdx`；`current` 是验证通过后原子切换的符号链接。`--source gitee` 可固定来源，默认 `--source auto` 保留 GitHub 优先、Gitee 回退。网络代理采用进程环境的 `HTTPS_PROXY`/`HTTP_PROXY`。

```toml
[mcp_servers.cordonix]
command = '/home/USER/.local/opt/cordonix/current/cdx'
args = ['--mcp']
startup_timeout_sec = 15
tool_timeout_sec = 300
```

--enable-auto-update 保存安装器并启用当前用户的 systemd timer，每小时检查一次。服务单次最多 190 秒，安装器内部另有 170 秒预算及 180 秒外层超时。它不使用 sudo，也不修改全局 PATH。需要可用的用户 systemd 管理器；没有用户会话或用户管理器停止时，检查不会运行。需要退出登录后持续运行的机器，应由机器管理员按其策略配置用户管理器生命周期。

```sh
sh /absolute/install.sh --check
sh /absolute/install.sh
sh /absolute/install.sh --disable-auto-update
```

`--schedule-dry-run` 与 enable/disable 配合，只处理 `<root>/systemd` 中的定义，回执标记真实注册 `NOT_RUN`。缺少 systemd 或注册失败时，安装器会报告自动更新未启用，已验证的安装文件仍保留。

## 校验、切换和回滚

每个源最多一次元数据请求、一次校验文件和一次压缩包下载，单个请求最多 20 秒。元数据最多 1 MiB，压缩包最多 256 MiB。文件名与 SHA-256 必须匹配，再检查归档路径和大小，随后运行实际 cdx --version 和 tool_info，确认 tag、平台和 runtime=C11。失败时保留旧入口；Windows 的 SHA 校验失败会立即停止，Linux 会记录失败原因并在 auto 模式尝试另一个来源。

只有所有验证通过才切换当前版本。相同版本返回 up_to_date，远端更旧返回 older_release，不会降级。旧版本目录保留；需要回滚时，Windows 可把 current.json 指回已验证的旧版本，Linux 可将 current 指回旧版本目录。回滚期间应先停用自动检查，避免下一次检查再次升级。

正在运行的 MCP 会话继续使用原来的进程；新版本在下次启动 MCP 时生效。升级不会强行终止其他会话。首次修改 Codex MCP 配置后，新建会话并调用 tool_info，确认实际连接的版本。配置字段参考 [OpenAI 官方 MCP 文档](https://developers.openai.com/codex/extend/mcp)。

SHA-256 校验保护下载一致性；当前发布包没有代码签名，不宣称签名认证或跨文件事务。PASS、FAIL、UNKNOWN、NOT_RUN 分开保留；下载完成、安装通过、自动任务注册成功及新会话加载新版本是四项不同证据。
