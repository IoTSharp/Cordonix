# Windows 便携包

先运行 `eng\test.cmd`，然后在 PowerShell 7 中执行：

```powershell
.\eng\package.ps1
.\eng\test-package.ps1
```

`package.ps1` 验证 `build\bin\cdx.exe --version` 必须与请求的版本完全一致，再生成 Windows x64 便携包。默认版本为 `0.1.0-dev`；需要其他版本时显式传入 `-Version`。构建脚本通过 `CDX_VERSION` 把同一个版本传入 Windows 和 Linux CLI，tag `v0.0.1` 在包内表现为 `0.0.1`。脚本只依赖 PowerShell 7、Git 和已构建的 Cordonix，不依赖个人技能目录或固定 PowerShell 安装路径。

## 输出与证据

默认输出在 `build\packaging`：

- `cordonix-0.1.0-dev-win-x64.zip`，包含可执行文件、Apache-2.0 许可证、README、打包说明、安装/更新说明、可复制 AI 提示词、配置示例和三份元数据。
- `*.manifest.json`，记录 14 个输入文件的大小和 SHA-256，包含 First、行范围、Tail、Raw、目录列举、路径解析、编码验证七份请求示例以及安装/更新说明。`format_version: 1` 是本脚本的格式版本，不引用不存在的外部 schema。
- `*.provenance.json`，记录打包时 checkout 的 Git revision、dirty 状态、输入二进制 SHA-256 和实际版本。`binary_build_revision: UNKNOWN` 明确表示没有证明二进制来自这一 revision。
- `*.spdx.json`，SPDX 2.3 文件清单，包括 payload、manifest 和 provenance 共 16 个文件、`DESCRIBES`/`CONTAINS` 关系、基于排序后的文件 SHA-1 计算的 package verification code，以及绑定内容 SHA-256 的文档 namespace。SBOM 自身列为 verification code 的排除项，避免自引用哈希。
- `*.zip.sha256`，外层 ZIP 的 SHA-256。

ZIP 的 17 个条目使用固定排序、1980-01-01 时间和无压缩存储。同一文件内容、Git 状态及脚本参数可产生相同 ZIP；这不声称重新编译的二进制可复现。`-SourceDateEpoch` 控制元数据日期，默认值为 0。

脚本每次创建独占的临时子目录，成功后只删除本次目录，失败时保留诊断并输出路径。正式 ZIP 和同名外部元数据可被后续运行覆盖。每个输入最多 32 MiB，checkout 状态最多 100 项，整个打包或测试有 120 秒预算；外部进程使用字面参数，记录 PID、时间、程序、参数和父进程，超时清理本次进程树。

## 包门禁与接入

`test-package.ps1` 连续生成两次并比较 ZIP SHA-256，在包含中文和空格的独占目录中解压，核验清单、大小、许可证、SPDX 文件哈希及关系、verification code、外部元数据一致性，再运行包内 `--version` 和 `--request tool_info`，断言准确版本、`platform=windows`、`runtime=C11`。完整源码门禁仍是 `eng\test.cmd`；SPDX 官方 schema 验证、签名、安装器和正式发布是独立门禁。

使用时解压到选择的目录，将配置示例中的 `command` 改为 `cdx.exe` 的真实绝对路径。此流程不会自动修改 PATH、Codex 配置或现用运行时。签名与正式发布保持 `NOT_RUN`。

## Linux x64 包

在 Ubuntu 等带有 GCC、GNU tar、gzip 和 coreutils 的环境中，先执行：

```sh
CDX_VERSION=0.0.1 sh eng/test-linux.sh
CDX_VERSION=0.0.1 sh eng/package-linux.sh 0.0.1
sha256sum --check build/packaging/cordonix-0.0.1-linux-x64.tar.gz.sha256
```

`package-linux.sh` 生成 `cordonix-<version>-linux-x64.tar.gz`、同名 `.sha256`、manifest、provenance 和 SPDX 元数据。归档内的 `cdx` 是 Linux x64 动态链接程序，安装前应在目标机运行 `./cdx --version` 和 `./cdx --request examples/request.json`，再按 [安装与更新说明](install-update.md) 配置 Codex MCP。Linux 包的 ACL、扩展属性和签名仍是独立门禁。

## Tagged release workflow

推送符合 `vMAJOR.MINOR.PATCH` 的 tag（例如 `v0.0.1`）会触发 `.github/workflows/release.yml`。工作流在独立的 Windows 和 Linux runner 上执行源码门禁、生成两类包和 SHA-256 资产，随后创建 GitHub Release。若已创建 `IoTSharp/Cordonix` Gitee 仓库并配置 `GITEE_TOKEN`，工作流还会同步 `main` 和 tag，并通过有限的 API 调用上传相同资产；缺少仓库或 secret 时发布 job 会失败并保留明确诊断。
