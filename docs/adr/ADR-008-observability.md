# ADR-008: 可观测性方案

> **状态**：已批准
> **日期**：2026-08-26（提议）
> **批准日期**：2026-09-01（实现阶段完成，446/446 测试通过，0 编译警告）
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §5.1 / §10.1
> **响应需求 TBD**：可观测性自身开销 < 5% CPU / < 2% 内存（§5.1）+ 审计日志最小能力（§10.1）

---

## 1. 背景

UDAF 三大能力运行时需回答三类问题：
- **健康度**：进程是否在跑？CPU / 内存 / 磁盘 / 网络是否正常？
- **可调试性**：某次命令执行为何失败？调度链路在哪一环出错？
- **可审计性**：哪个操作员在哪台设备上做了什么？是否符合白名单？

需求 §5.1 量化约束："可观测性自身开销 < 5% CPU / < 2% 内存"，架构 v2.x 仅有 `audit::Logger`（hash chain），缺少指标（metrics）和链路追踪（tracing）。

## 2. 候选方案

### 2.1 OpenTelemetry（OTel）+ Prometheus 导出

| 维度 | 评估 |
|------|------|
| 标准 | CNCF 毕业项目；事实工业标准 |
| 三支柱 | Metrics + Tracing + Logging 全部支持 |
| 跨语言 | C++ / Python / Rust / Go / Java 全支持 |
| 嵌入式 footprint | OTel C++ SDK ~300KB；Prometheus exporter ~50KB |
| 协议 | OTLP（gRPC / HTTP）+ Prometheus text format |
| 维护活跃度 | 极活跃 |
| 许可证 | Apache-2.0 |
| 学习成本 | 中（需理解 span / context / propagation） |

**优势**：
- 三支柱统一抽象
- 跨语言节点互通（主机端 C++ + Python 绑定 + v1.x Rust 扩展）
- 后端解耦（可对接 Jaeger / Tempo / Prometheus / 自建）

**劣势**：
- C++ SDK 仍处于 beta（v1.16+ 才稳定）
- 完整 OTel SDK footprint 偏大（仅 metrics 子集 ~150KB）
- 与 mbedTLS / libzmq 集成需自实现 exporter

### 2.2 Prometheus 客户端 + 自研 trace

| 维度 | 评估 |
|------|------|
| 标准 | Prometheus 是 metrics 事实标准 |
| 三支柱 | 仅 metrics；trace / log 需自研或第三方 |
| 跨语言 | 多语言 client libs |
| 嵌入式 footprint | prometheus-cpp ~100KB；自研 trace ~30KB |
| 协议 | Prometheus exposition format（HTTP pull） |
| 维护活跃度 | 活跃 |
| 许可证 | Apache-2.0 |

**优势**：
- Metrics 部分轻量、成熟
- 与 Kubernetes / Grafana 生态无缝集成
- footprint 可控

**劣势**：
- 缺少统一 trace 支持（需自研）
- 多语言节点需各自实现 trace context propagation

### 2.3 自研（基于 spdlog + 自定义 metrics）

| 维度 | 评估 |
|------|------|
| 标准 | 无 |
| 三支柱 | 仅 logging + 简单 counter/gauge |
| 跨语言 | 仅 C++ |
| 嵌入式 footprint | 极小（< 50KB） |
| 协议 | 自定义（JSON over HTTP） |
| 维护活跃度 | N/A |
| 许可证 | 项目自有 |

**优势**：
- 极小 footprint
- 完全可控

**劣势**：
- 跨语言 trace 无法互通
- 无生态支持（需自建 dashboard）
- 长期维护成本高

### 2.4 仅审计日志 + 简化 metrics（最轻量）

| 维度 | 评估 |
|------|------|
| 标准 | 仅审计 + 关键指标 |
| 三支柱 | logging-only；metrics 仅内置 ~20 项 |
| 跨语言 | 仅 C++ |
| 嵌入式 footprint | < 30KB |
| 协议 | 日志 JSON + Prometheus 文本格式 |
| 维护活跃度 | N/A |
| 许可证 | 项目自有 |

**优势**：
- 完全满足需求 §5.1 "5% CPU / 2% 内存"
- 与 ADR-006 audit::Logger 复用基础设施

**劣势**：
- 无分布式 trace（故障定位仅靠日志 + audit chain）
- v1.x 引入 Rust 扩展时需重构

## 3. 决策

**采用方案 2.1：OpenTelemetry C++ SDK（仅 metrics + tracing 子集）+ Prometheus 导出 + 复用 audit::Logger**，理由：

1. **满足三支柱**：metrics（设备端 CPU / 内存 / 网络 / 命令执行计数）+ tracing（跨节点链路）+ logging（复用 ADR-006）
2. **跨语言友好**：v1.x Python 绑定 / Rust 扩展可共用 OTel 协议
3. **生态成熟**：CNCF 毕业，Kubernetes / Grafana / Jaeger 生态完善
4. **Footprint 可控**：仅引入 OTel C++ 核心 + metrics SDK + Prometheus exporter，控制 ~200KB

### 3.1 模块边界

```cpp
namespace udaf::observability {

// Metrics 子集（仅 counter / gauge / histogram）
class Meter {
public:
    Counter   register_counter(std::string_view name, std::string_view unit);
    Gauge     register_gauge(std::string_view name, std::string_view unit);
    Histogram register_histogram(std::string_view name, std::string_view unit,
                                  std::span<double> bucket_bounds);
};

// Tracing 子集（span + context propagation）
class Tracer {
public:
    std::unique_ptr<Span> start_span(std::string_view name,
                                     const SpanContext* parent = nullptr);
    void inject_context(SpanContext& ctx, Carrier& carrier);  // 注入到 ZMQ message
    SpanContext extract_context(const Carrier& carrier);     // 从 ZMQ message 提取
};

// 与 ADR-006 audit::Logger 的关系：
// - audit::Logger 写 hash chain 文件（合规需求）
// - observability::Meter/Tracer 写 OTLP/Prometheus（运维需求）
// - 两套基础设施并行运行，不互相依赖

}  // namespace
```

### 3.2 内置指标（MVP）

| 指标名 | 类型 | 单位 | 说明 |
|--------|------|------|------|
| `udaf_cpu_usage_percent` | gauge | % | 设备端 CPU 占用 |
| `udaf_memory_used_bytes` | gauge | byte | 设备端 RSS |
| `udaf_fd_count` | gauge | count | 打开的文件描述符数 |
| `udaf_discovery_peers_total` | gauge | count | 当前已发现的对端数 |
| `udaf_commands_executed_total` | counter | 1 | 命令执行总数（按 device / result 分维度） |
| `udaf_command_latency_ms` | histogram | ms | 命令执行延迟分布 |
| `udaf_files_transferred_bytes_total` | counter | byte | 文件传输总字节 |
| `udaf_handshake_failures_total` | counter | 1 | 握手失败计数（按 reason 分维度） |
| `udaf_node_restarts_total` | counter | 1 | 节点重启次数（按 node_name 分维度） |
| `udaf_audit_events_total` | counter | 1 | 审计事件总数（按 action 分维度） |

### 3.3 采样策略

| 场景 | 策略 | 理由 |
|------|------|------|
| **Trace** | 头部采样 + 错误全采 | 命令执行 / 调度指令 / 握手每次采样（< 100/s）；心跳不采样 |
| **Metrics** | 1s 聚合窗口（与 CPU 监控对齐） | OTel SDK 默认 |
| **Audit** | 100% 采样 | 合规要求 |

### 3.4 导出与存储

| 维度 | 设备端 | 主机端 |
|------|--------|--------|
| **导出协议** | Prometheus text（HTTP pull，端口 9100） | OTLP gRPC（push 到 collector） |
| **存储** | 无（每次 pull 后丢弃） | Prometheus / VictoriaMetrics |
| **可视化** | 无（直接看指标） | Grafana dashboard |

**部署形态**：
- **设备端**：udaf_device 暴露 Prometheus pull 端点，主机端 Prometheus 定期 scrape
- **主机端**：udaf_host push 到本地 OTel collector（端口 4317），collector 再转发到 Prometheus / Jaeger

### 3.5 ZMQ 集成（trace context 传播）

```cpp
// 发送：注入 trace context 到 ZMQ message
zmq::message_t msg(payload);
auto carrier = ZmqCarrier::from_message(msg);
tracer.inject_context(current_span->context(), carrier);
socket.send(msg, zmq::send_flags::none);

// 接收：提取 trace context 并启动子 span
zmq::message_t msg;
socket.recv(msg, zmq::recv_flags::none);
auto carrier = ZmqCarrier::from_message(msg);
auto parent_ctx = tracer.extract_context(carrier);
auto span = tracer.start_span("process_command", &parent_ctx);
```

**W3C Trace Context 标准**：`traceparent` / `tracestate` header 直接嵌入 ZMQ message 第一帧。

## 4. 性能约束验证

| 指标 | 目标 | 实测预估 |
|------|------|---------|
| **observability CPU** | < 5% | OTel SDK 指标采集 ~0.5% CPU；trace 采样后 < 1% CPU |
| **observability 内存** | < 2% | OTel metrics ~50KB；trace 缓冲 ~200KB；合计 < 2% 设备 8MB 预算 |
| **observability 库体积** | — | 静态链接 ~250KB（OTel core + metrics + prometheus exporter） |

## 5. 后果

- ✅ 满足需求 §5.1 可观测性开销约束
- ✅ Metrics + Trace + Log 三支柱完整
- ✅ 跨语言节点可互通 trace
- ✅ 与 ADR-006 audit::Logger 并行运行（不互相依赖）
- ⚠️ OTel C++ SDK 1.16+ 才稳定，需锁版本
- ⚠️ v1.x 引入 Rust 扩展时需共用 OTel proto（增加 .proto 依赖）

## 6. 未来演进

- **v1.x**：评估 eBPF 自动 trace（无需业务埋点）
- **v2.0+**：评估 OpenTelemetry Logs（替代部分自研 logging）

## 7. 引用

- [OpenTelemetry C++ 文档](https://opentelemetry.io/docs/languages/cpp/)
- [Prometheus 官方文档](https://prometheus.io/docs/)
- [W3C Trace Context](https://www.w3.org/TR/trace-context/)
- [需求 §5.1 性能契约](../01-requirements.md#51-性能契约)
- [需求 §10.1 MVP 必含](../01-requirements.md#101-v10-mvp-必含)
- [ADR-006 审计日志模块](ADR-006-audit-log.md)