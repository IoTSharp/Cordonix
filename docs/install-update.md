# Cordonix 安装与自动更新

把[安装提示词](../examples/cordonix-install-update-prompt.md)复制给 AI，或让 AI 读取这个链接，就可以安装 Cordonix (cdx)。当前源码默认在每天首次启动时开启独立后台实例，自动检测和安装新版本；显式关闭后的选择会保留。安装器能从零启动，不要求事先安装 cdx。运行时仍是纯 C；Windows 安装器及启动器需要 PowerShell 7，Linux 安装器和更新辅助逻辑使用既有 Python 3.9+ 标准库及 GNU timeout。Linux 缺少更新解释器时仍继续启动原生程序。这些更新触发及原生命令改动尚未随正式版本发布。

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
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoLogo -NoProfile -File 'C:\absolute\install.ps1'
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

若该表已存在，编辑原表，不要再追加同名表。自定义安装根目录时替换启动器路径，并核对 PowerShell 7 的实际绝对路径。启动器在每次启动时读取 `current.json` 的 `version`，选择 `versions/<version>/cdx.exe`，因此配置应指向稳定启动器。安装器回执中的 `config_updated=false` 表示它不会自动修改 Codex 配置。

自动更新默认由稳定启动器触发，不需要计划任务。`-EnableAutoUpdate` 可重新启用曾关闭的更新，并按兼容行为额外注册每日及登录后的当前用户任务，单次最多 3 分钟。任务名带安装目录哈希；注册与执行是不同证据，不能用注册成功推断任务已经运行。注册失败时回执另报 `schedule_registration=FAIL` 和 `schedule_error`，每日启动检查仍可使用。脚本不调用 Windows PowerShell 5.1，也不修改系统 PATH。

```powershell
# 只查询版本；手动更新；停止自动检查（保留安装文件）。
.\install.ps1 -CheckOnly
.\install.ps1
.\install.ps1 -DisableAutoUpdate
```

`-TaskDefinitionOnly` 只输出任务 XML，不注册任务，可用于审查计划任务定义。`-ScheduleDryRun` 执行安装但跳过任务注册，适用于隔离测试。任务实际注册是否成功，与二进制安装是否成功分别报告。

## Linux

```sh
sh /absolute/install.sh
```

默认安装根目录是 `~/.local/opt/cordonix`，可用 `--root /absolute/path` 更换。版本位于 `versions/<version>`，稳定启动入口是 `<root>/launch.sh`；`current` 是验证通过后原子切换的符号链接。`--source gitee` 可固定来源，默认 `--source auto` 保留 GitHub 优先、Gitee 回退。网络代理采用进程环境的 `HTTPS_PROXY`/`HTTP_PROXY`，初次安装的代理环境会保存供自动任务使用；可在 `update-settings.json` 调整。不要在代理 URL 中写入凭据。

```toml
[mcp_servers.cordonix]
command = '/home/USER/.local/opt/cordonix/launch.sh'
args = ['--mcp']
startup_timeout_sec = 15
tool_timeout_sec = 300
```

Linux 启动器补查后通过 `current` 符号链接选择版本；自定义安装根目录时，同样使用该根目录下的稳定绝对路径。旧的 `<root>/current/cdx` 入口仍然有效，但绕过启动补查，只能依赖定时任务。

默认 `auto_update=daily_startup`，无需用户 systemd 管理器。`--enable-auto-update` 可重新启用曾关闭的更新，并按兼容行为额外注册每日用户 timer。该备用服务单次最多 190 秒，安装器内部另有 170 秒预算及 180 秒外层超时。它不使用 sudo，也不修改全局 PATH。用户管理器不可用时回执报告 `auto_update=startup_fallback` 和注册错误，每日启动检查仍可使用。

```sh
sh /absolute/install.sh --check
sh /absolute/install.sh
sh /absolute/install.sh --disable-auto-update
```

`--schedule-dry-run` 只处理 `<root>/systemd` 中的定义，回执标记真实注册 `NOT_RUN`。

## 自动触发、诊断和旧安装迁移

两个平台都在安装根目录保存 `update-settings.json`（是否启用、下载源及网络设置）和 `last-update-check.json`（最近尝试的时间、结果、版本、来源或错误）。普通重装保留显式关闭的选择，enable 参数恢复自动更新。`CheckOnly`/`--check` 不改变设置和任务。自动任务从设置读取来源，不会继续使用过时的命令行来源。

稳定启动器根据本地日历日期，在每天首次启动时开启一个独立后台实例，主程序继续启动。当日已有检查或后台启动记录时，后续启动跳过；次日首次启动重新检查。启动声明使用短期排他锁，避免并发启动重复开启实例。Windows 隐藏后台控制台，Linux 记录后台输出；检查结果不会混入 MCP 标准输出。

新版本的原生命令是 `cdx --checkupgrade`，可用 `cdx --checkupgrade --install-root <绝对目录>` 指定安装根目录。它读取安装器生成的 `update-request.json`，复用 C 进程核心，以独立的 program、args、cwd、timeout 字段运行既有更新器；Windows 使用 PowerShell 7 保存脚本，Linux 使用从 `install.sh` 原样生成的 Python 安装器主体，不通过 shell 命令字符串执行。命令的非零退出码保留执行失败，进程核心负责子进程超时、取消及清理。`tool_info.update_command` 公布该能力。

后台实例拥有完整更新预算：Windows 更新器 150 秒，进程核心 170 秒；Linux 更新器 170 秒，进程核心 180 秒。GitHub 优先，首源只使用剩余请求预算的一半，为 Gitee 留出时间。网络或更新失败后主程序继续使用已安装版本，错误保留在回执中。显式请求的备用定时任务独立于每日启动节流。

已发布的 0.3.1 尚不包含原生 `--checkupgrade`，因此新启动器根据安装时实际读取的能力字段，使用独立的既有更新脚本作为过渡；安装到带有该字段的后续版本后会自动采用原生命令，不根据猜测的版本号选择能力。

后台更新和备用定时任务共用安装目录的排他锁；已有更新正在执行时跳过重复写入。`last-update-launch.json` 保存工作进程 PID、创建时间及命令；最近检查保存在 `last-update-check.json`。超时或取消后不自动重放写入。回执若停留在 `checking`，表示该次尝试未完成，不能当作更新成功。

更新器还会从已经校验并验证的版本包中采用更高 `Cordonix updater generation` 的安装脚本，后续版本可以更新更新逻辑本身。没有该标记或代数更低的旧包不会覆盖当前更新器。

已经部署的旧安装器不会凭空获得新的触发逻辑。部署本次改动时，需由维护方统一运行一次新的安装脚本，刷新稳定启动器、保存设置并核对每日后台入口；备用任务可按需开启。这属于旧安装迁移。Linux 同时把 MCP 配置入口从 `current/cdx` 迁移到 `launch.sh`。此后的版本更新由后台实例触发。直接解压 ZIP/tar.gz 并运行二进制的便携安装不提供这些自动触发器，需采用安装器入口。

## 同类工具的更新方式

以下对照在 2026-11-07 查阅官方文档：

| 工具 / 安装方式 | 检测和更新方式 | 对 Cordonix 的参考价值 |
| --- | --- | --- |
| [Claude Code 原生安装](https://code.claude.com/docs/en/setup#auto-updates) | 启动及运行期间检查，后台下载和安装，下次启动生效；包管理器安装有独立规则 | 自动检测、自动安装和当前会话采用新版本应分别处理 |
| [VS Code Windows User Setup](https://code.visualstudio.com/docs/setup/windows#updates) | 支持后台更新，按提示安装；系统安装需要提权，ZIP 安装需手动更新 | 使用用户目录和稳定入口；明确安装渠道影响更新能力 |
| [uv 独立安装](https://docs.astral.sh/uv/getting-started/installation/#upgrading-uv) | 提供 `uv self update`；其他渠道交给包管理器 | 有自更新命令仍需自动触发，不能把命令存在当作自动更新 |
| [WinGet / Homebrew 等包管理器](https://code.claude.com/docs/en/setup#auto-updates) | 由包管理器执行升级；Claude Code 对 WinGet/Homebrew 另提供显式开启后台升级的选项 | 发布包到目录或包仓库本身不会自动更新现有用户 |

Cordonix 复用现有安装器、版本目录和校验流程，默认每日首次启动开启独立后台更新实例；当前会话保留原进程，后续启动使用验证后的入口。Windows/Linux 的打包门禁必须验证原生更新命令、能力字段及真实的有界执行，防止后续发布漏掉自动更新入口。

## 校验、切换和回滚

每个源最多一次元数据请求、一次校验文件和一次压缩包下载，单个请求最多 20 秒。元数据最多 1 MiB，压缩包最多 256 MiB。文件名与 SHA-256 必须匹配，再检查归档路径和大小，随后运行实际 cdx --version 和 tool_info，确认 tag、平台和 runtime=C11。失败时保留旧入口；Windows 的 SHA 校验失败会立即停止，Linux 会记录失败原因并在 auto 模式尝试另一个来源。

只有所有验证通过才切换当前版本。相同版本返回 up_to_date，远端更旧返回 older_release，不会降级。旧版本目录保留；需要回滚时，Windows 可把 current.json 指回已验证的旧版本，Linux 可将 current 指回旧版本目录。回滚期间应先停用自动检查，避免下一次检查再次升级。

正在运行的 MCP 会话继续使用原来的进程；新版本在下次启动 MCP 时生效。升级不会强行终止其他会话。新建聊天可能复用现有连接，不能仅凭“新聊天”断言已加载更新；按下节验证实际连接的版本和工具。

当前 Release 工作流通过 GitHub Actions OIDC 和固定版本的 attestation action，为 Windows ZIP、Linux tar.gz 生成随附件发布的 `<archive>.sigstore.json` 构建来源证明。验证命令与适用版本见[发布签名说明](release-signing.md)。这是 Sigstore artifact provenance，不等于 Windows Authenticode 或 SmartScreen 信任；安装器当前只自动检查 SHA-256 和运行时身份，不自动验证 Sigstore bundle。不得把安装校验成功写成已完成来源证明验证。

PASS、FAIL、UNKNOWN、NOT_RUN 分开保留；下载完成、安装通过、自动任务注册成功及新客户端加载新版本是四项不同证据。

## 确认客户端发现新增工具

安装器选择已发布稳定版本；源码中新加的能力，只有连接到包含该能力的构建后才能使用。完整选择表、参数示例与验证流程见 [MCP 工具发现与选择](mcp-tool-guide.md)。

1. 记录安装回执及稳定入口。Windows 读取 `<root>/current.json`，Linux 核对 `current` 的目标。单独运行另一个 `cdx --request` 只验证那个二进制，不证明当前客户端已采用它。
2. 保存 MCP 配置，并启动新的客户端连接，或使用该客户端实际提供的 MCP 重连机制；保留其他会话及共享服务的生命周期，不结束共享 MCP 进程。
3. 调用实际连接的 `tool_info`，核对 `version`、`platform`、`runtime=C11`。开发构建可能复用版本号，因此还需查看实际 `tools/list` 和所需工具的 `inputSchema`，不能靠版本或固定工具总数推断能力。
4. 采用本次源码能力时，确认 `data_query`、`find_files`、`hash_file`、`copy_file`、`move_file`；所需旧能力另核对 `read_files`、`create_directory`、`screenshot`。工具出现在列表中，仍可能在特定平台返回 `not_supported`，例如 Linux 截图。
5. 运行[只读 smoke check](mcp-tool-guide.md#fresh-client-smoke-check)：内联 CSV 的 `data_query` 应返回 `count:2` 且 `scan_complete:true`。文件与写入能力按已授权的实际工作流另行验证，未运行的项目保留 `NOT_RUN`。

旧版 `examples/codex-config.toml` 中的固定 `enabled_tools` 列表会隐藏未列入的新工具。复用旧配置时，若无工具白名单需求，可省略 `enabled_tools`；若有需求，则在原列表中补齐所需名称，保留其他已用工具，并同时核对 `disabled_tools` 的有意排除项。修改配置后仍须建立新的连接并验证实际可见 schema。

这两个过滤字段已根据 [OpenAI 官方 Codex 配置 schema](https://github.com/openai/codex/blob/main/codex-rs/core/config.schema.json) 核对；它们属于客户端配置，不是 Cordonix 工具参数。客户端配置与可用重连方式以[官方 MCP 文档](https://developers.openai.com/codex/mcp/)及实际安装版本为准。
