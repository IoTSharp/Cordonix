# Cordonix 上下文效率首批实施计划

日期：2026-11-12，Asia/Shanghai。对应 [ROADMAP](../ROADMAP.md) 的 CDX-14A/B/C。

2026-11-13 本地实施验收已完成；以下初始比较和目标记录保留。三个互不重叠的智能体实际并行实施，
主协调者接线并最终验收。源码合同见 [工具指南](mcp-tool-guide.md#tool-profiles-and-execution-projections)，
完整命令、二进制 SHA-256、失败修复记录与门禁边界见 [HANDOFF](../HANDOFF.md)。本次没有提交、推送或发布。

| 交付 | 当前本地证据 | 未测边界 |
| --- | --- | --- |
| CDX-14A | `--mcp --tool-profile full\|local-code`；35/27，46 项 C 测试及每平台 11 项真实 stdio 测试通过 | 现有安装连接仍为 0.5.0；新客户端采用未执行 |
| CDX-14B | `result_mode=full\|summary`、`summary_tail_bytes`、`capture_log_path`；每平台 12 组/27 次真实执行检查通过，包括副作用只一次及日志失败 full 回退 | 发布、跨发行版/更多工作负载未执行 |
| CDX-14C | 五轮、每平台 152 项检查、50 个执行样本、90 RPC、0 retry/unknown；12 项统计/异常路径测试通过 | 实际 tokenizer、provider usage、费用、真实 agent 任务成功率均 NOT_MEASURED |

Windows 最终 `eng/test.cmd`、WSL Ubuntu `eng/test-linux.sh`、修改协议文件 GCC 静态分析及 Linux
ASan/UBSan 下的真实投影回归均通过；Ubuntu 无原生 rg，对应既有用例保持 NOT_RUN。
最终 tools 数组 full/local-code 为 63,569/39,309 bytes，wire 为 63,614/39,354 bytes。
Windows full/summary 结果 wire 中位数为 955/1,232 bytes，P95 为 9,118/1,397；Linux 分别为
950/1,214 与 9,112/1,378。小结果因 metadata 增大，大结果尾部投影缩小；不宣称普遍 token 或成本下降。
原始证据保存在 ignored `build/context-efficiency-20261113/`，包括失败过的早期回执。

## 目标与现有证据

用户授权在新会话中使用多个智能体完成一批最值得实现的能力。首批交付明确的工具能力档案、
执行结果精简与原文回看、可复现效率基线。现有批量读取、HTTP 表示选择和数据查询继续复用。

- 初始源码：`main`，`22956e8841833da5e6ed61c548f19c0a83f34e9d`，规划前工作树干净。
- 连接中的已安装程序：0.5.0，`00e07a0`，35 个 MCP 工具。
- 本轮真实 stdio `tools/list` 的 tools 数组紧凑 JSON 为 62,487 UTF-8 bytes；去掉八个网络、
  设备和截图工具后的 27 项 local-code 投影为 38,227 bytes，减少约 38.8%。这不是 token 数据。
- 八个投影外工具：`ssh`、`sftp`、`http`、`network`、`serial`、`phone`、`screenshot`、`vnc_screenshot`。
- 历史 HTTP 固定语料已证明可去掉重复表示，见 [native-http](native-http.md)；没有证明整项任务费用下降。
- 本轮宽搜索实际触发 `output_limit`；当前适配器在外部进程未正常结束时返回捕获输出的 Base64。
  大结果和不完整输出有真实使用证据，但本批不扩大进程捕获上限、不改变现有超限清理语义。

参考 [TokenEconomizer](https://github.com/microsoft/TokenEconomizer/tree/0566b6036749326b865e0ce073cf894f198b4886)
的经验学习、工具裁剪与循环提醒；吸收其先测量、统计自身开销、保护正常进展和可恢复配置的做法。
本批只实现确定性能力；Python 沿用仓库现有测试设施，生产核心保持 C11。

## CDX-14A：显式工具能力档案

最小交付为 `full` 和 `local-code` 两种会话启动选项。协调者检查现有 CLI/stdio 后确定参数名。

1. 未选择时保持现有完整工具面，所有已有工具名、参数、描述和注解保留。
2. local-code 采用上述明确的 27 项；它适用于只需本地代码工具的任务。网络、设备与视觉任务可选 full。
3. 会话内能力表固定；切换通过新会话显式选择完成。拒绝未知档案，并提供清楚用法。
4. 只控制发现面，不充当权限或沙箱。直接 `tools/call` 继续使用原有分派及验证合同。
5. 复用现有 schema 源，不复制维护另一份工具定义，不通过删减必要参数说明省体积。

验收：真实 CLI/stdio 的默认与 full 等价、local-code 精确成员、未知输入、工具说明覆盖、
直接调用兼容、两平台行为，以及实际 wire/tools-array bytes。token/费用和客户端实际注入另计。

## CDX-14B：精简执行结果与原始捕获回看

在 `execute_program` 增加显式 `full|summary` 结果选择，full 保持默认。新增字段的实际命名和
最小接线由协调者在查阅源码后确定；不引入任意格式解释器、通用结果库或额外模型。

1. 同一次有界子进程执行提供两种投影；不修改 literal argv、环境、期限、输出上限和后代清理。
2. summary 保留 status、process_status、已知或未知退出码、编码、cleanup、原生错误与过程截断。
   增加每个捕获流的原始字节数、SHA-256、有界尾部和投影省略标识。
3. hash 范围为实际捕获的原始 bytes；过程已超限时不能声称完整程序输出。投影省略必须与
   `output_truncated` 分开。实施选择 raw-byte Base64 尾部以精确保留非法或分割的多字节字符，
   不做替换解码。完整结果的原有严格文本解码保持不变。
4. 调用者为 summary 明确提供原始 stdout/stderr 的新文件目标，复用现有文件操作；不覆盖既有文件，
   原始捕获 bytes 可独立读回校验。实施使用一个新日志文件，ASCII header 后按 stdout/stderr 拼接，
   header 与响应记录流长和 offsets；SHA-256 范围只含 captured payload，不含 header。
   词法目标验证在启动前完成，发布状态与进程状态分开记录。
5. 解码失败、超时、取消、输出超限仍保留原始捕获与原过程状态。日志发布失败时，同一次响应回退到
   原有完整结果并说明日志状态；不能丢失已执行命令的证据，也不能重放命令补救。
6. 保持有限内存与已有捕获上限；不增加后台进程、自动日志清理器或跨会话缓存。调用者持有日志生命周期。

验收通过真实小进程和临时文件：大量正常输出、错误退出、空流、Unicode/CP936、非法解码、
已有目标、日志写入失败、timeout、cancellation、descendant cleanup、output limit。核对 raw bytes/hash，
对有副作用的 fixture 计数证明只执行一次；完整与精简模式的过程结果相同，仅表示发生改变。

## CDX-14C：可复现效率与行为基线

沿用 [three-stage baseline](../tests/acceptance/three_stage_baseline.py) 的真实入口和证据习惯，
新增独立 acceptance 脚本，不复制生产算法。先与实现智能体约定 CLI/结果字段，使语料可以并行准备。

- 固定工具清单和输出 fixture；默认三轮，最多五轮，每轮最多二十个案例，整体 deadline 可配置且有硬上限。
- 分别计 tools 数组、操作结果 JSON、最终 MCP 响应的 UTF-8 bytes；保留原响应、语料版本、二进制身份和 hash。
- 记录 calls、fixture success、unexpected failure、unknown、retry、耗时中位数/P95 与样本数。
- 可用时使用已安装、名称/版本明确的 tokenizer；缺失时记录 `NOT_MEASURED`，不安装新依赖，不用 bytes/4 冒充 token。
- provider usage、cached tokens、billing 和真实代理任务完成率属于独立证据；没有真实数据就明确未测。
  本批不调用付费模型、不读全量私人会话历史、不把本地 fixture 成功率当作 SWE-Bench 或真实代理成功率。
- 明确冷启动/连续调用、profile 稳定性和缓存适用边界；不能以工具列表体积推断每轮实际上下文。

验收要求保留完整与精简表示的正确性反例，确认更小结果未隐去错误和不完整状态。
执行脚本本身的非零退出必须可见；失败案例不能从统计中静默剔除。

## 多智能体归属与集成

协调者在任何并行编辑前公布接口和精确文件归属。下面是切片边界，新增文件名可按现有布局调整。

| 负责人 | 可独占编辑 | 必须交给协调者 |
|---|---|---|
| 智能体 A / CDX-14A | 独立 profile 模块/头文件及专属回归 fixture | CLI 启动接线、tools.c 等共享 schema 接线补丁建议 |
| 智能体 B / CDX-14B | operations.c 的执行结果投影、必要独立 helper 和专属回归 fixture | 与 A 不重叠的公共 schema/构建接线补丁建议 |
| 智能体 C / CDX-14C | 新 acceptance 脚本、固定小 fixture、该脚本使用说明 | 生产协议与 schema；独立复核 A/B 的实际结果 |
| 主协调智能体 | 共享 CLI/stdio/schema/build wiring，README/ROADMAP/HANDOFF/CHANGELOG，最终集成 | 不允许让多个 worker 同时写共享文件或构建同一输出目录 |

以实际 `collaboration.spawn_agent` 启动三个 worker，不能用三个串行计划代替多智能体。
在调用期间只由协调者运行共享 build；worker 的试验使用各自 `build/` 子目录且记录归属。
接口变化及时通知相关 worker，禁止用覆盖整个文件的方式合并。

## 执行、验证与关闭条件

所有循环、搜索、批处理、等待和重试同时有数量与墙钟边界，支持取消且每六十秒以内给出进展。
生成循环先人工复核比较条件，并用极小输入带超时试跑。长命令记录 PID、创建时间、完整命令行
和父进程；使用 try/finally 或等价路径回收已核实归属的完整子进程树和临时文件。
仅 PowerShell 7；不扫描盘符根、用户目录或 Program Files 寻找工具，不按进程名批量终止。
MSVC 使用仓库已核实的 VS 2026 工具路径；GNU 工具仅在已知 Ubuntu 内有界核实，不改用其他 ABI。

源码集成后串行执行适用的 `eng/build.cmd`、最终 `eng/test.cmd`、Linux 对应门禁，以及新增真实
CLI/MCP/acceptance fixture；按改动覆盖静态分析与可用 sanitizer。长门禁使用有超时、可轮询的已有
终端工作流，避免扩大 MCP 单次期限或重复启动还在运行的测试。一次通过后不无理由反复重跑。

每项完成必须同时有实现、公开合同/参数说明、真实行为测试、当前树门禁、独立测量和同步文档。
用 PASS/FAIL/UNKNOWN/NOT_RUN 分开记录；旧发布测试不替代本次验证。默认 full 兼容性单独检查。
新文档/fixture 如需进入现有包，修改源清单并验证链接，不直接改生成的包或二进制。

该批源代码、测试和文档已获授权。保留其他会话 dirty state；只处理归属明确的文件。
commit/push、发布、安装/替换共享运行时、外部配置写入和新增依赖分别保留自己的授权边界。
不要因为仓库历史发布曾获授权，就把该权限延伸为本批发布操作。

## 后续路线

CDX-14D/E 在首批账本证据之后设计宿主侧事实与循环提醒；没有可验证 hook 生命周期时不修改宿主配置。
CDX-14F 接续 CDX-11B2/CDX-12C 的搜索上下文、错误诊断与原文回看，先选真实高频工具和错误语料。
CDX-14G 接续 CDX-12B 单文档多处编辑，保留 hash/count/编码约束，明确顺序、冲突和部分失败。
这些工作有路线标识，但不自动扩大本次三个实现切片的范围。
