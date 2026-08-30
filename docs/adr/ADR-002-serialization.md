# ADR-002: 序列化格式选型

> **状态**：提议（待评审）
> **日期**：2026-08-26
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §5.6
> **响应需求 TBD**：第三方库选型（§11.2 第 1 项）+ 节点消息契约格式（§11.2 第 8 项）

---

## 1. 背景

UDAF 节点消息契约需要在以下场景序列化：
1. **IPC 边界**（同主机不同进程）：序列化消息头
2. **TCP 边界**（跨主机）：完整序列化
3. **配置文件 / 拓扑 YAML**：声明式编排
4. **审计日志**：操作员/时间戳/设备/动作/参数

需求 §5.6 定义了语义契约：
- 投递语义：至少一次（业务侧需幂等）
- 默认 4KB，最大 1MB
- 至少一次投递（业务侧幂等）

需求 §5.7 性能契约：
- 跨主机节点调度延迟 < 5ms (P95)
- 设备端内存 < 16MB（满载）

## 2. 候选方案

### 2.1 Protocol Buffers（protobuf）

| 维度 | 评估 |
|------|------|
| 类型支持 | 强类型（.proto schema） |
| 跨语言 | 主流语言全覆盖（C++ / Python / Rust / Go / Java） |
| 体积 | 中等（带字段 tag） |
| 性能 | 编码 ~1-5μs（4KB 消息） |
| Schema 演进 | 良好（field tag 机制） |
| 嵌入式 footprint | full runtime ~500KB；lite runtime ~200KB |
| 维护活跃度 | 活跃（Google 维护） |
| 许可证 | BSD-3 |

**优势**：
- 工业级标准
- 跨语言工具链成熟
- Schema 演进机制完善
- 与 ref/msgc 经验一致

**劣势**：
- 嵌入式 footprint 偏大（lite runtime 仍 ~200KB）
- 不支持零拷贝（编码需要遍历字段）
- 反射能力弱

### 2.2 FlatBuffers

| 维度 | 评估 |
|------|------|
| 类型支持 | 强类型（.fbs schema） |
| 跨语言 | C++ / C# / Go / Java / JS / Python 等 |
| 体积 | 紧凑（类似 protobuf） |
| 性能 | 零拷贝读取（无需反序列化） |
| Schema 演进 | 良好（前向兼容） |
| 嵌入式 footprint | ~100KB |
| 维护活跃度 | 活跃（Google 维护） |
| 许可证 | Apache-2.0 |

**优势**：
- 零拷贝读取（性能好）
- Footprint 小（适合嵌入式）
- 跨语言支持

**劣势**：
- 写时不能直接修改（需构造新对象）
- 工具链相对不成熟
- 团队经验少

### 2.3 Cap'n Proto

| 维度 | 评估 |
|------|------|
| 类型支持 | 强类型（.capnp schema） |
| 跨语言 | C++ / Python / Rust / Go 等 |
| 体积 | 紧凑（与 FlatBuffers 相当） |
| 性能 | 零拷贝 |
| Schema 演进 | 极好（无版本号，依赖协议设计） |
| 嵌入式 footprint | ~50KB |
| 维护活跃度 | 中（个人维护） |
| 许可证 | MIT |

**优势**：
- 零拷贝 + 紧凑
- 极致 footprint（适合嵌入式）
- RPC 支持

**劣势**：
- 个人维护（长期风险）
- 文档不如 protobuf 完善
- 团队经验少

### 2.4 自研二进制协议

| 维度 | 评估 |
|------|------|
| 类型支持 | 自定 |
| 跨语言 | 仅 C++ |
| 体积 | 最优 |
| 性能 | 取决于实现 |
| Schema 演进 | 自定 |
| 嵌入式 footprint | 最小 |

**优势**：
- 完全控制
- 与 ref/device_framework DFRM 经验一致

**劣势**：
- 跨语言支持差
- 长期维护成本
- 调试困难

## 3. 决策

**采用 Protocol Buffers 作为主要序列化格式**，理由：

1. **成熟度**：工业级标准，多语言工具链完善
2. **跨语言**：C++ / Python / Rust 全覆盖（满足 v1.x Python 绑定需求）
3. **Schema 演进**：field tag 机制支持前向/后向兼容
4. **团队经验**：ref/msgc 已使用 protobuf，迁移成本低
5. **Footprint 可控**：使用 lite runtime + 优化，控制在 ~200KB

## 4. 使用策略

| 场景 | 序列化方式 |
|------|----------|
| **inproc** | 不序列化（直接 `shared_ptr<const T>`） |
| **IPC** | 仅序列化消息头（数据走 ZMQ `ipc://` Unix domain socket，详见 ADR-001） |
| **TCP** | 完整 protobuf 序列化 |
| **配置文件** | YAML |
| **审计日志** | 自定义紧凑二进制（protobuf lite runtime） |

## 5. 缓解措施（针对 protobuf 的劣势）

| 劣势 | 缓解措施 |
|------|----------|
| Footprint ~200KB | 使用 lite runtime（去掉反射 / descriptor）；按需裁剪 |
| 不支持零拷贝 | inproc / IPC 路径不序列化，绕过此问题 |
| 反射能力弱 | 通过 `udaf::MessageSchema<T>` 自定义类型系统补充 |

## 6. 未来演进

- **v1.x**：评估 FlatBuffers 用于嵌入式场景（如 C 节点协议）
- **v2.0+**：如 protobuf lite runtime 仍超预算，迁移到 FlatBuffers

## 7. 后果

- ✅ 跨语言节点消息契约标准化
- ✅ 工具链成熟，团队学习成本低
- ⚠️ lite runtime ~200KB 占用设备端 8MB 预算的 2.5%
- ⚠️ 嵌入式场景仍需观察性能（必要时切换 FlatBuffers）

## 8. 引用

- [Protocol Buffers 官方文档](https://protobuf.dev/)
- [FlatBuffers 官方文档](https://google.github.io/flatbuffers/)
- [需求 §5.6 消息中间件语义契约](../01-requirements.md#56-消息中间件语义契约)
- [架构 §10 节点消息契约](../02-architecture.md#10-节点消息契约)
