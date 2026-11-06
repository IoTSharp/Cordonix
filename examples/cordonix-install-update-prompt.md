# 复制给 AI：安装 Cordonix 并自动更新

请在这台机器上安装 Cordonix (cdx)，配置到 Codex MCP，并启用每天首次启动时的独立后台更新。已有安装则安全更新，保留旧版本。执行前读取安装说明：

- 优先：<https://github.com/IoTSharp/Cordonix/blob/main/docs/install-update.md>
- 回退：<https://gitee.com/IoTSharp/Cordonix/blob/main/docs/install-update.md>

按以下步骤完成，不需要我反复确认同一项安装操作：

1. 识别系统和架构。仅支持 Windows x64 或 Linux x64；其他平台报告 NOT_RUN。从对应仓库的 main/install/install.ps1 或 main/install/install.sh 下载脚本并保存到独占临时目录。GitHub raw 地址是 https://raw.githubusercontent.com/IoTSharp/Cordonix/main/install/<文件名> ，不通则使用 https://gitee.com/IoTSharp/Cordonix/raw/main/install/<文件名> 。单次下载最多 20 秒、脚本最多 1 MiB，每个源最多尝试一次；不要把网络响应直接 pipe 到 Shell。
2. 阅读保存的脚本，通过已有 typed 执行工具调用。首次安装不要求事先有 cdx；已经有 Cordonix 时优先用 execute_program，把 program、argv、cwd 和超时分开。Windows 只用 PowerShell 7 的 -NoProfile -File <绝对脚本路径>，不使用 PowerShell 5.1，不嵌套 -Command。Linux 使用 sh <绝对脚本路径>，安装器需要 Python 3.9+ 和 GNU timeout；缺少依赖时明确报告，不自动猜测或安装额外工具。
3. Windows 运行 install.ps1，默认安装到 %LOCALAPPDATA%\Cordonix；Linux 运行 sh install.sh，默认安装到 ~/.local/opt/cordonix。默认在每天首次启动时开启独立后台更新实例，主程序继续启动，不要求计划任务；原生命令是 cdx --checkupgrade，0.3.1 采用独立脚本过渡。若用户曾显式关闭，保留选择，只有明确要求重新开启时再使用 enable 参数；该兼容参数还会注册每日备用任务，需单独验证其执行。默认 source=auto：GitHub Release 不通则从 Gitee 获取成果物。不得把 Gitee 的源码 ZIP 当作可执行包，也不得省略文件名和 SHA-256 校验。
4. 只接受规范 tag vMAJOR.MINOR.PATCH。按完整名选择 Windows 的 cordonix-<version>-win-x64.zip 或 Linux 的 cordonix-<version>-linux-x64.tar.gz，以及同名 .sha256。运行 --version 与 tool_info 确认下载内容，验证通过才切换。相同版本不重复下载，较旧版本不降级；错误保留旧版本。
5. 把安装器返回的稳定入口合并进用户的 Codex MCP 配置，保留其他配置项。Windows 的 command 是 PowerShell 7 绝对路径，args 是 ['-NoLogo','-NoProfile','-File','<安装根目录>\launch.ps1','--mcp']；Linux command 是 <安装根目录>/launch.sh，args 是 ['--mcp']。不要固定到某一个版本目录。旧安装部署新的安装脚本后，再核对任务是否注册、last-update-check.json 是否记录实际检查；任务失败时单列状态，不能把二进制安装成功当作定时更新已运行。
6. 核验自动检查已启用：检查 update-settings.json、稳定启动器和 update-request.json，验证首次启动产生独立工作进程及实际检查回执，同日再次启动跳过。原生能力由 tool_info.update_command 公布，不能只看版本号推断；0.3.1 使用独立脚本兼容。计划任务或 systemd timer 是显式开启的备用机制，注册成功与执行成功分别报告。使用文档里的 disable 命令可随时停用检查。
7. 当前 MCP 进程继续运行；下次启动才使用新版本，不强行停止任何其他会话。只清理本次临时下载和确认归属的进程，保留旧安装。最后输出以下回执，并说明是否需要新建 Codex 会话。

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
stable_command: actual command and literal args | none
config_updated: true | false | unknown
auto_update: daily_startup | enabled | disabled | startup_fallback | UNKNOWN | NOT_RUN
auto_update_task: task or timer name | none
new_session_verified: true | false
notes: network, missing asset, unsupported platform, scheduler or other remaining boundary
```

可以只发送这段短提示词以及上面的说明链接：

> 按链接中的说明为本机安装或更新 Cordonix，开启每日首次启动的独立后台检查并配置 Codex MCP。优先 GitHub，失败从 Gitee 下载 Windows/Linux 对应的正式成果物，校验 SHA-256 和实际版本后切换；保留旧版本及其他配置，分别报告安装、后台更新和新连接验证结果。
