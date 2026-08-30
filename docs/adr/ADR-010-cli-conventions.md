# ADR-010: CLI 工具集与输出约定

> **状态**：提议（待评审）
> **日期**：2026-08-26
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §10.1 / §9.4
> **响应需求 TBD**：API 形态（§11.2 第 7 项）+ CLI 工具集（架构 §9.5）

---

## 1. 背景

UDAF 主机端需提供 CLI 工具集供运维人员使用。架构 v2.3 §9.5 列出 9 个 CLI（`udaf_discover` / `udaf_run` / `udaf_push` 等），但缺少：
1. **命名空间划分**（哪些命令在哪个可执行文件）
2. **参数解析框架**（getopt vs argparse vs cxxopts）
3. **输出格式规范**（human / json / yaml 三态如何统一）
4. **退出码体系**（退出码如何与错误码映射）
5. **help / man page 规范**
6. **shell 自动补全**（bash / zsh）

需求 §9.4 要求"CLI 体验与 Git / Docker 工具对齐"（隐含约束），§10.1 要求"CLI 在 MVP 范围内"。

## 2. 候选方案

### 2.1 多可执行文件 + 单二进制多子命令（git / docker 风格）

| 维度 | 评估 |
|------|------|
| 形态 | `udaf` 单二进制 + 子命令（`udaf discover` / `udaf run` / `udaf push`） |
| 学习成本 | 低（与 git / docker 一致） |
| 参数解析 | 自封装或 cxxopts |
| 退出码 | 自定义 |
| man page | 单 man page（`udaf(1)`） + 子命令 section |
| shell 自动补全 | 单一入口（`udaf completion bash > /etc/bash_completion.d/udaf`） |

**优势**：
- 与 git / docker 一致（业界标准）
- 单 man page 入口统一
- shell 自动补全单点维护

**劣势**：
- 可执行文件改名（如 `udaf_discover` → `udaf discover`）破坏向后兼容

### 2.2 多可执行文件（每个命令一个二进制）

| 维度 | 评估 |
|------|------|
| 形态 | `udaf_discover` / `udaf_run` / `udaf_push` 等独立二进制 |
| 学习成本 | 低（命令即程序名） |
| 参数解析 | 各工具单独解析 |
| 退出码 | 各自定义 |
| man page | 多个 man page（`udaf_discover(1)` 等） |
| shell 自动补全 | 需维护多个 completion 文件 |

**优势**：
- 单文件可独立升级（理论）

**劣势**：
- 多个 man page + completion 文件维护成本高
- 与业界主流（git / docker / kubectl）偏离

### 2.3 单二进制动态加载（plugin）

| 维度 | 评估 |
|------|------|
| 形态 | `udaf` 核心 + plugin（.so / .dylib） |
| 学习成本 | 中 |
| 参数解析 | 框架较复杂 |
| 退出码 | 自定义 |

**优势**：
- 第三方可扩展

**劣势**：
- v1.0 不需要 plugin 机制（过度设计）
- 增加部署复杂度

## 3. 决策

**采用方案 2.1：单二进制 `udaf` + 子命令风格**（git / docker / kubectl 范式），理由：

1. **业界标准**：与 git / docker / kubectl / podman 一致，学习成本最低
2. **统一文档**：单 man page 入口 + 子命令 section，用户查询简单
3. **shell 自动补全单点维护**：避免多文件同步
4. **可演进**：未来加子命令无需改包结构

### 3.1 子命令清单（架构 §9.5 改造）

| 命令 | 别名 | 能力 | 说明 |
|------|------|------|------|
| `udaf discover` | `ls` | A | 扫描局域网设备（默认 human，--format=json/yaml） |
| `udaf run` | `exec` | C | 在设备上执行命令（流式输出） |
| `udaf push` | - | C | 文件上传到设备 |
| `udaf pull` | - | C | 文件下载到本地 |
| `udaf topology` | `topo` | B | 显示当前数据流图 |
| `udaf node` | - | B | 节点管理（list / start / stop / status / restart） |
| `udaf trust` | - | 安全 | 白名单管理（add / remove / list / verify） |
| `udaf psk` | - | 安全 | PSK 管理（generate / inject / rotate / list） |
| `udaf auth` | - | 安全 | 认证状态查询 / 强制重认证 |
| `udaf migrate` | - | 工具 | ref/ → UDAF 数据迁移（一次性子命令） |
| `udaf config` | - | 工具 | 配置文件查看 / 校验 / 编辑 |
| `udaf version` | - | 元 | 版本信息（含 API 版本 + 库版本 + Git commit） |
| `udaf completion` | - | 元 | 生成 shell 自动补全脚本 |
| `udaf help` | `?` | 元 | 帮助 |

**向后兼容（v1.0 → v1.x）**：
- v1.0 也保留旧名（`udaf_discover` 等）作为薄 wrapper（symlink 或软链接）
- v1.x 公告弃用旧名，v2.0 删除

### 3.2 参数解析

**选择 cxxopts**（header-only C++17 库）：
- 比 getopt 表达力强（POSIX / GNU 风格自动支持）
- 比 CLI11 轻量（CLI11 体积大）
- 比 boost::program_options 简单

```cpp
#include <cxxopts.hpp>

cxxopts::Options opts("udaf", "UDAF CLI - Unified Device & Application Framework");
opts.add_options()
    ("v,version", "Show version")
    ("h,help", "Show help")
    ("f,format", "Output format (human|json|yaml)", cxxopts::value<std::string>()->default_value("human"))
    ("t,timeout", "Operation timeout (seconds)", cxxopts::value<int>()->default_value("30"))
    ("d,debug", "Enable debug logging");

auto subcmd = argv[1];
if (subcmd == "discover") {
    auto sub_opts = ...;  // 子命令独立选项
}
```

### 3.3 输出格式规范

**三态格式 `--format=human|json|yaml`**：

```cpp
namespace udaf::cli {

enum class OutputFormat { HUMAN, JSON, YAML };

class Output {
public:
    Output(OutputFormat fmt, std::ostream& os = std::cout);
    
    // 通用字段
    void field(std::string_view key, std::string_view value);
    void field(std::string_view key, int64_t value);
    void field(std::string_view key, std::chrono::milliseconds value);
    
    // human: 表格
    void table(const std::vector<std::vector<std::string>>& rows,
               const std::vector<std::string>& headers);
    // json / yaml: 数组对象
    void list(const std::vector<std::unordered_map<std::string, std::string>>& items);
    
    // 错误输出（始终 stderr）
    void error(ErrorCode code, std::string_view message);
};
}
```

**human 格式示例（`udaf discover`）**：

```
NODE ID                              HOSTNAME              IP                STATUS    LAST SEEN
aaaa-bbbb-cccc-dddd                   host-workstation-01   192.168.1.10      ONLINE    2s ago
eeee-ffff-gggg-hhhh                   host-server-02        192.168.1.20      OFFLINE   3m ago
```

**json 格式示例**：

```json
{
  "devices": [
    {
      "node_id": "aaaa-bbbb-cccc-dddd",
      "hostname": "host-workstation-01",
      "ip_v4": "192.168.1.10",
      "status": "ONLINE",
      "last_seen_ns": 1234567890
    }
  ],
  "total": 1
}
```

**yaml 格式示例**：

```yaml
devices:
  - node_id: aaaa-bbbb-cccc-dddd
    hostname: host-workstation-01
    ip_v4: 192.168.1.10
    status: ONLINE
    last_seen_ns: 1234567890
total: 1
```

**流式输出（`udaf run` / `udaf push`）**：
- `udaf run` 使用 `--stream`（默认）：stdout / stderr 实时输出，exit code 单独
- 关闭流式：`--no-stream`，输出汇总到 json

### 3.4 退出码体系

| 退出码 | 常量 | 含义 | 对应错误码范围 |
|--------|------|------|----------------|
| 0 | `UDAF_EXIT_OK` | 成功 | `ErrorCode::OK` |
| 1 | `UDAF_EXIT_GENERAL` | 通用错误 | 任意 ErrorCode（非特定分类） |
| 2 | `UDAF_EXIT_INVALID_ARG` | 参数 / 配置文件错误 | `CONFIG_*` / `INVALID_ARG` |
| 3 | `UDAF_EXIT_NETWORK` | 网络错误 | `NET_*` |
| 4 | `UDAF_EXIT_AUTH` | 认证 / 授权错误 | `CRYPTO_*` / `BIZ_AUTH_UNTRUSTED` |
| 5 | `UDAF_EXIT_RESOURCE` | 资源耗尽 | `RES_*` |
| 6 | `UDAF_EXIT_BUSINESS` | 业务错误（命令执行失败 / 文件不存在等） | `BIZ_*` |
| 7 | `UDAF_EXIT_PROTOCOL` | 协议错误 | `PROTOCOL_*` |
| 8 | `UDAF_EXIT_SERIALIZE` | 序列化错误 | `SERIALIZE_*` |
| 9 | `UDAF_EXIT_TOPOLOGY` | 拓扑 / 发现错误 | `TOPOLOGY_*` / `DISCOVERY_*` |
| 10 | `UDAF_EXIT_NODE` | 节点生命周期错误 | `NODE_*` |
| 64 | `UDAF_EXIT_USAGE` | CLI 用法错误（`--help` / 缺参数） | — |
| 130 | `UDAF_EXIT_INTERRUPTED` | SIGINT（Ctrl+C） | — |

**与 shell 约定一致**：64+ 遵循 BSD sysexits.h（`EX_USAGE=64`、`EX_DATAERR=65` 等），1-10 为 UDAF 业务退出码。

**详细错误查询**：
```bash
$ udaf run aaaa-bbbb-cccc-dddd "ls /nonexistent" 2>&1
ERROR: BIZ_FILE_NOT_FOUND (0x4002): file '/nonexistent' does not exist
$ echo $?
6
# 详细分类（通过 exit code 无法表达）：
$ udaf --verbose run ...
```

### 3.5 help / man page 规范

**`--help` 输出格式**（与 git / docker 对齐）：

```
$ udaf run --help
Usage: udaf run [options] <device-id> <command> [args...]

Execute a command on a remote device.

Arguments:
  <device-id>              Target device node ID (UUID)
  <command>                Shell command to execute
  [args]                   Command arguments

Options:
  -t, --timeout <sec>      Command timeout (default: 30)
  -f, --format <fmt>       Output format: human|json|yaml (default: human)
      --stream             Stream stdout/stderr in real-time (default: true)
      --no-stream          Disable streaming, collect output
  -u, --user <name>        Override device user (audit log)
      --env <K=V>          Set environment variable (repeatable)
  -h, --help               Show this help
      --audit-comment <s>  Attach comment to audit log

Examples:
  udaf run aaaa-bbbb ls /tmp
  udaf run aaaa-bbbb "ps aux | grep nginx" --timeout 10

Exit Codes:
  0   success
  3   network error
  4   auth error
  6   business error (command failed)

See 'man udaf-run' for full documentation.
```

**man page**：通过 `help2man` 从 `--help` 自动生成（CI 步骤），存放在 `docs/man/`，CMake `install` 复制到 `/usr/share/man/man1/`。

### 3.6 shell 自动补全

`udaf completion` 子命令生成 bash / zsh / fish 补全脚本：

```bash
# bash
$ udaf completion bash > /etc/bash_completion.d/udaf
$ source /etc/bash_completion.d/udaf
$ udaf <TAB><TAB>
auth       completion  config    discover    help    node
psk        pull        push      run         topology trust    version
$ udaf discover --<TAB><TAB>
--debug     --format=   --help     --timeout=  --verbose
$ udaf run aaaa<TAB>
aaaa-bbbb-cccc-dddd  device-workstation
aaaa-cccc-eeee-ffff  server-rack-03
```

**实现**：基于 cxxopts 的 `parse_positional` + 子命令列表，动态生成 `_udaf()` bash 函数。

### 3.7 全局选项（所有子命令通用）

| 选项 | 含义 |
|------|------|
| `-h, --help` | 显示子命令帮助 |
| `-f, --format <fmt>` | 输出格式（human / json / yaml） |
| `-v, --verbose` | 增加日志详细度（-v = INFO, -vv = DEBUG） |
| `-q, --quiet` | 减少日志详细度（仅 ERROR） |
| `--config <path>` | 指定配置文件（默认 `/etc/udaf/client.yaml`） |
| `--log-file <path>` | 日志文件（默认 `/var/log/udaf/cli.log`） |

**全局环境变量**（与全局选项对应）：
- `UDAF_FORMAT=json`
- `UDAF_VERBOSE=1`
- `UDAF_CONFIG=/path/to/config.yaml`

## 4. 命令清单详情

### 4.1 `udaf discover`

```bash
udaf discover [--timeout=5] [--format=human|json|yaml] [--refresh]
```

| 选项 | 说明 |
|------|------|
| `--timeout` | 扫描超时（默认 5s） |
| `--refresh` | 强制刷新（忽略缓存） |
| `--service <name>` | 仅列出提供指定服务的设备 |
| `--offline` | 同时列出已离线设备（默认隐藏） |

### 4.2 `udaf run`

```bash
udaf run <device-id> <command> [--timeout=30] [--stream|--no-stream] [--user=<name>] [--env=<K=V>]
```

| 选项 | 说明 |
|------|------|
| `--timeout` | 命令超时 |
| `--stream` / `--no-stream` | 是否流式输出（默认 stream） |
| `--user` | 覆盖设备侧运行用户（记录审计） |
| `--env` | 设置环境变量（可重复） |
| `--audit-comment` | 添加审计注释 |

**退出码传播**：设备侧命令的 exit code 通过 exit code + json 字段 `exit_code` 同时报告：
- shell 退出码：UDAF CLI 自身错误分类
- json 字段 `exit_code`：设备侧命令的 exit code

### 4.3 `udaf push` / `udaf pull`

```bash
udaf push <device-id> <local-path> <remote-path> [--mode=scp|rsync] [--compress]
udaf pull <device-id> <remote-path> <local-path> [--mode=scp|rsync]
```

| 选项 | 说明 |
|------|------|
| `--mode` | 传输模式（scp 走单连接；rsync 走增量） |
| `--compress` | 启用 zstd 压缩（适合文本文件） |
| `--resume` | 断点续传 |
| `--checksum` | 启用 SHA-256 校验（默认） |

### 4.4 `udaf trust`

```bash
udaf trust add <node-id> [--psk-fingerprint=<fp>] [--cert-fingerprint=<fp>] [--added-by=<name>]
udaf trust remove <node-id>
udaf trust list [--format=human|json|yaml]
udaf trust verify <node-id> <request> <signature>
```

### 4.5 `udaf psk`

```bash
udaf psk generate [--length=32] [--output=<path>]    # 生成 32B 随机 PSK
udaf psk inject <device-id> <psk-file>               # 通过 USB / 串口注入（需 root）
udaf psk rotate <device-id>                          # 触发 PSK 轮换
udaf psk list [--format=human|json]                  # 列出 PSK 池（不显示密钥）
```

### 4.6 `udaf auth`

```bash
udaf auth status                                   # 当前认证状态
udaf auth force-reauth <device-id>                 # 强制重新握手
udaf auth verify <device-id>                       # 主动验证对端
```

## 5. 与现有架构的整合

| 既有 API | CLI 子命令 | 说明 |
|---------|-----------|------|
| `Client::discover` | `udaf discover` | 一一对应 |
| `Client::run_command` | `udaf run` | 一一对应（流式回调 → stdout） |
| `Client::push_file` / `pull_file` | `udaf push` / `pull` | 一一对应 |
| `Client::subscribe_device_changes` | （未提供 CLI） | v1.x 评估 |
| `whitelist_add` 等 | `udaf trust add` | 一一对应 |
| `crypto::rotate_psk` | `udaf psk rotate` | 一一对应 |

CLI 是 C++ API 的"便捷外壳"，所有 CLI 操作都可通过 `libudaf_host.a` + 自定义前端实现。

## 6. 后果

- ✅ CLI 风格与 git / docker 一致，学习成本低
- ✅ 单 man page + shell completion 单点维护
- ✅ 三态输出格式 + 详细退出码覆盖自动化场景
- ✅ 旧名兼容（v1.0 提供 wrapper）
- ⚠️ 旧名 → 新名迁移需公告（v1.x → v2.0）
- ⚠️ man page 需 CI 自动生成（`help2man` 依赖）

## 7. 未来演进

- **v1.x**：评估 `udaf node logs <node-name>` 实时日志查看
- **v1.x**：评估交互式 REPL（`udaf shell`）
- **v2.0+**：评估 TUI 界面（`udaf tui`，基于 FTXUI）

## 8. 引用

- [cxxopts GitHub](https://github.com/jarro2783/cxxopts)
- [help2man 文档](https://www.gnu.org/software/help2man/)
- [BSD sysexits.h](https://man.openbsd.org/sysexits)
- [需求 §9.4 易用性](../01-requirements.md)
- [需求 §10.1 MVP 必含](../01-requirements.md#101-v10-mvp-必含)
- [架构 §9 API 形态](02-architecture.md#9-api-形态)
- [ADR-004 认证模型](ADR-004-auth-model.md)
- [ADR-005 跨主机调度白名单](ADR-005-peer-whitelist.md)