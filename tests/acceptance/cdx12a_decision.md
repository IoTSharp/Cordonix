# CDX-12A 前置决策与最小实验证据

复核日期：2026-11-06，Asia/Shanghai。本文只冻结有限后台任务的决策和验收边界，不实现后台任务，也不改变 `execute_program` 的现有合同。

## 结论

CDX-12A 的前置决策完成，功能实现 **deferred**。当前证据支持继续使用现有的有限执行合同：需要超过 MCP 客户端等待期限的分析，交给可以保持连接并轮询的 terminal 生命周期 owner；terminal 可以调用 `cdx --request <absolute-request.json>`，仍由 Cordonix 对该次有限执行负责。客户端 transport timeout 后不能把结果当成失败，也不能自动重试；先用原始 PID、输出文件和进程快照核对副作用。

当前 MCP stdio 会话只允许一个 active request。`execute_program` 在返回前完成子进程和管道清理，不能通过增加 `timeout_ms` 或在它下面启动一个 detached 子进程来获得后台任务。实现一个后台任务需要新的任务状态、结果保留和生命周期 owner；在这些边界没有真实需求证据前，不扩展协议或重写进程 core。

## 当前事实与实测

源码和既有边界记录给出以下事实：

- `src/cdx-cli/main.c` 只有一个 `active_request`。工具 worker 完成后发送结果；收到 `notifications/cancelled` 时设置当前 worker 的取消事件。会话关闭时 `finish_active(..., 1)` 等待最多 10 秒，否则以失败退出。
- `src/cdx-core/process_win32.c` 的 `cdx_execute` 在同步返回前总是执行 `cdx_cleanup` 和 `cdx_close`。Windows Job 使用 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`；超时、取消和正常结束都保留 `cleanup_complete`，并报告 active 进程数量。
- `docs/execution-boundaries.md` 已记录：子进程期限和 MCP 客户端期限独立；transport timeout 没有结果时完成状态和副作用为 UNKNOWN；持久 terminal 才能承接可能超过客户端期限的有限分析；不能把 detached 分析放到 `execute_program` 下面。

本次用当前 `build/bin/cdx.exe` 通过真实 `--request` 入口做了两个本地、无写副作用的观察。目标程序是 Windows 原生 `ping.exe`，请求文件保存在被忽略的 `build/three-stage-20261105/coordinator/`：

| 请求 | 观察 | 证据 |
|---|---|---|
| `cdx12a-complete-cp936-request.json` | `status=completed`、子进程 `exit_code=0`、`cleanup_complete=true`、active `1→0`、子进程耗时约 1140 ms；完整输出按 CP936 解码 | Cordonix `execute_program` 返回的 `stdout`，外层耗时约 1282 ms |
| `cdx12a-timeout-cp936-request.json` | `status=timed_out`、`process_status=timed_out`、`cleanup_complete=true`、active `2→0`、子进程耗时约 547 ms；已产生的输出仍保留 | Cordonix `execute_program` 返回的 `stdout`，外层耗时约 703 ms |
| `cdx12a-timeout-request.json` | 同一超时请求指定 UTF-8 解码时返回 `status=encoding_error`，但内层 `process_status=timed_out`、`cleanup_complete=true` 且保留 `stdout_base64` | 证明解码失败和执行结果分开，读取证据不需要重复执行 |

这些观察证明有限执行和清理合同，不证明后台任务、跨客户端重连、结果 TTL 或持久输出流；后四项保持 **NOT_RUN**。

## 与官方 Tasks 扩展的比较

2026-11-06 只读复核了官方 [MCP Tasks 扩展仓库](https://github.com/modelcontextprotocol/ext-tasks)以及其 [TypeScript 入门](https://modelcontextprotocol.github.io/ext-tasks/typescript/getting-started.html)和 [receiver 文档](https://modelcontextprotocol.github.io/ext-tasks/typescript/receiver.html)。官方文档当前仍将扩展标为 experimental。其基本生命周期是：

1. 客户端在支持 task 的请求中要求 task；服务端返回 `task` 元数据（`taskId`、状态、创建/更新时间、TTL 和建议轮询间隔），而不是立即返回工具结果。
2. 客户端用 `tasks/get` 轮询 `working`、`input_required`、`completed`、`failed` 或 `cancelled`，完成后用 `tasks/result` 取得原工具结果。
3. 服务端可以用 `tasks/cancel` 请求取消；取消必须与实际进程清理和最终状态关联，不能把“已请求取消”当成“进程已退出”。
4. 服务端需要声明 task capability 和具体请求的支持范围；结果保留期限、输出大小和服务端重启后的状态都属于实现合同。

这与当前 Cordonix 的单 active worker、同步结果和会话关闭即清理存在结构性差异。直接把 Tasks 字段加进现有 JSON 会产生“已返回 task 但没有独立 owner/registry”的伪后台任务，因此暂不实现。

## 方案比较

| 方案 | 正确性与往返 | 资源/维护成本 | 决策 |
|---|---|---|---|
| 保持现状，有限任务交给 terminal owner | 已有 PID、输出、超时、取消和 Job 清理；对当前客户端零协议变化 | 最低；没有额外结果存储和并发状态 | 当前采用 |
| 最小 Tasks 垂直切片，只覆盖 `execute_program` | 可跨请求轮询并在 TTL 内取回结果，但必须新增 task registry、持久 owner、输出分块和重连语义 | 中等；要改 MCP worker 生命周期和关闭路径，仍需证明客户端支持 | 条件满足后再做 |
| 通用后台任务/长期服务管理器 | 可覆盖多工具和长期运行，但把有限任务、交互 PTY、浏览器和服务生命周期混在一起 | 最高；并发、恢复、权限和结果保留范围远超当前证据 | 拒绝，另立项目 |

## 若重新启动实现，最小合同

只有重新评估条件满足时，先做一个只覆盖 `execute_program` 的垂直切片：

- 原有同步 `execute_program` 保持兼容；新路径一次只派发一个进程，并记录创建时间、PID、输入 hash 和唯一 task ID。
- `tasks/get` 返回状态、`statusMessage`、创建/更新时间、TTL、已保留输出字节和 `cleanup_complete`；输出按单调序号或字节 offset 读取，设置单任务和全局上限。
- `tasks/result` 只在终态返回原工具结果，保留 `process_status`、exit code、原始输出/解码状态、截断状态和 `remote_outcome`；不要为压缩而丢失原始字节。
- `tasks/cancel` 先记录取消请求，再等待 Job/进程和读管道清理；只有观察到清理完成才报告 `cancelled`，否则保留 `unknown`/`timed_out` 证据。
- 客户端断开不自动重试。task registry 的 owner 必须明确；服务端关闭时取消或标记未确认任务，不能把进程遗留成 detached 后台服务。

## 重新评估触发条件与验收

重新评估至少需要以下一项真实需求和全部生命周期资料：

- 至少三个独立的有限分析任务因客户端期限短于子进程期限而无法用 terminal owner 完成，并保留原始请求、持续时间、输出和副作用核对记录。
- 任务必须跨客户端断开/重连继续，且现有 terminal 生命周期无法满足；需要明确重连窗口、TTL、最大输出和失败后的恢复语义。
- 目标 MCP 客户端实际接受 task capability、`tasks/get`、`tasks/result` 和 `tasks/cancel`；只读规范或 schema 存在不算客户端兼容证据。

通过门槛后，验收必须覆盖：一次且仅一次派发；状态和增量输出顺序；正常结束、非零退出、输出上限和执行期限；取消前/中/后；客户端断开与重连；服务端 EOF/关闭；PID、Job、管道清理；原始输出可恢复；任何 transport timeout 后的 UNKNOWN 处理。没有持久任务 owner、真实客户端和重连 fixture 的测试全部标 **NOT_RUN**，不能用当前同步执行通过替代。
