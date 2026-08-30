# CLAUDE.md

> UDAF 项目的 AI 协作入口。
> 本文件**只列关键约束**，详细规范见各设计文档。

---

## 1. 项目一句话定位

> 基于 `ref/` 中已有代码的设计经验，从零实现的新项目 UDAF——围绕**三大并列能力**展开：
> - **能力 A**：多协议设备/服务发现（双向、定期，全网状）
> - **能力 B**：分布式数据流框架（dora 范式 + ZMQ 类中间件）
> - **能力 C**：设备 ↔ PC 通信（A + B 共同作为基础）

详细需求见 [`docs/01-requirements.md`](docs/01-requirements.md)。

---

## 2. 命名约定（关键）

| 对象 | 约定 | 示例 |
|------|------|------|
| **文档名称** | `NN-<英文名称>.md`（NN 为两位序号） | `01-requirements.md`、`02-architecture.md` |
| **命名空间** | `udaf::`（顶层），子命名空间按能力划分 | `udaf::ability_a::discovery` |
| **类 / 结构体** | `PascalCase` | `ServiceRegistry` |
| **函数 / 方法** | `snake_case` | `void parse_message()` |
| **成员变量** | `snake_case_`（尾下划线） | `uint32_t seq_;` |
| **局部变量 / 参数** | `snake_case` | `uint32_t payload_len` |
| **常量 / 枚举值** | `kPascalCase` 或 `SCREAMING_SNAKE` | `kMaxPayloadSize` |
| **宏** | `UDAF_SCREAMING_SNAKE` | `UDAF_ASSERT(x)` |
| **C++ 头文件** | `snake_case.hpp` | `service_registry.hpp` |
| **C++ 实现文件** | `snake_case.cpp` | `service_registry.cpp` |
| **C 头文件** | `snake_case.h`（如需 C 接口） | `udaf_c.h` |
| **C 实现文件** | `snake_case.c`（如需 C 接口） | `udaf_c.c` |
| **数据流节点** | `snake_case_node.{hpp,cpp}` | `cmd_exec_node.cpp` |

---

## 3. 不可违反的关键约束

1. **严禁明文传输用户名密码**（业务硬约束）
2. **greenfield 不兼容 ref/**（不保留 device_framework / msgc / rootfs_app 旧 API）
3. **文档不重复**：CLAUDE.md 只列关键约束；详细规范见 `docs/` 下设计文档
4. **需求阶段不写入技术选型**（库名 / 协议字节 / 传输后端的具体命名）
5. **禁止异常 + 裸 new/delete + `using namespace`**（C++ 规范）
6. **不引入 dora-rs Rust 运行时**（仅借鉴设计理念）
7. **跨主机节点调度必须白名单**（设备端防恶意调度）
8. **定期发现频率限制**（防 O(N²) 广播风暴）

---

## 4. 项目流程

UDAF 五阶段瀑布流程，**严格顺序、逐阶段评审**：

```
阶段 1：需求设计      → docs/01-requirements.md
阶段 2：架构设计      → docs/02-architecture.md
阶段 3：概要设计      → docs/03-detailed-design.md
阶段 4：详细设计      → docs/04-module-design.md
阶段 5：测试方案      → docs/05-test-plan.md
实现阶段（设计完成后）→ udaf/src/
```

**进入下一阶段前必须满足**：本阶段文档通过用户评审 + 验收清单全部勾选 + 用户明确同意。

**禁止**：跳过阶段、未评审就提交、同时推进多阶段。

---

## 5. AI 协作规范

### 5.1 工作循环

```
1. 用户提出任务
2. AI 评估任务复杂度
3. 复杂任务 → EnterPlanMode → 输出方案 → 用户评审
4. 实施 → 自检 → 用户验收
```

### 5.2 项目阶段内的工作循环

```
1. 阅读 docs/NN-<阶段名>.md
2. AskUserQuestion 列出待澄清问题
3. 用户回答后输出本阶段交付物
4. 用户评审 → 修订 → 用户同意 → 进入下一阶段
```

### 5.3 禁止行为

- ❌ 跳过阶段直接动手
- ❌ 未评审就 commit / push
- ❌ 修改 `ref/`（只读历史材料）
- ❌ 删除 `docs/` 中的设计文档
- ❌ 引入禁止特性（裸 new/delete / 异常 / using namespace）
- ❌ 引入 dora-rs Rust 运行时
- ❌ 在 refactor 中保留 ref/ 旧项目 API 兼容层
- ❌ 让 A 依赖 B（避免循环依赖）

---

## 6. 文档规范（最小集）

| 项 | 规范 |
|----|------|
| **位置** | `docs/` |
| **命名** | `NN-<英文名称>.md` |
| **一致性** | docs 下文档间术语、命名、引用、章节结构保持一致（详见 §6.1） |
| **章节顺序** | 背景 → 现状 → 目标 → 详细设计 → 验收 → 附录 |
| **图表** | 优先 mermaid |
| **语言** | 简体中文 |
| **变更记录** | 每份文档末尾维护版本表 |

### 6.1 docs 下文档一致性要求

文档之间必须保持以下一致性（重命名、术语、引用、结构）：

1. **命名一致**：所有文档遵循 `NN-<英文名称>.md` 命名（见 §2）。**禁止**混用中文名（如 `01-需求设计.md`）
2. **引用一致**：文档间互相引用时，链接必须指向当前真实存在的文件路径。**重命名文件后必须同步更新所有引用**
3. **术语一致**：同一概念在不同文档中使用相同表述（如"能力 A/B/C"不能时而中文时而英文；"host"与"主机"二选一）
4. **章节结构一致**：同一系列的设计文档（如 01/02/03/04/05）章节结构对齐（如都有"验收标准"和"附录"）
5. **TBD 标记一致**：未决项使用统一格式（如 `**TBD 阶段 2**`）
6. **变更记录一致**：每份文档末尾维护版本表，版本号递增、变更说明简洁

---

## 7. Git 规范（最小集）

- 中文提交信息，格式：`类型: 简要说明`（如 `feat: 添加设备发现服务`）
- 类型：`feat` / `fix` / `refactor` / `docs` / `test` / `build` / `ci` / `chore`
- **不**未经用户同意执行 `git commit` / `git push`
- **禁止** `push --force`（除非用户明确同意）
- UDAF 是新仓库，主分支 `main`，tag 遵循 semver

---

## 8. 快速链接

| 类别 | 路径 |
|------|------|
| 阶段 1 需求 | [`docs/01-requirements.md`](docs/01-requirements.md) |
| 阶段 2 架构 | [`docs/02-architecture.md`](docs/02-architecture.md) |
| 阶段 3 概要 | [`docs/03-detailed-design.md`](docs/03-detailed-design.md) |
| 阶段 4 详细 | [`docs/04-module-design.md`](docs/04-module-design.md) |
| 阶段 5 测试 | [`docs/05-test-plan.md`](docs/05-test-plan.md) |
| ADR 总览 | [`docs/adr/`](docs/adr/) |
| 依赖说明 | [`docs/dependencies.md`](docs/dependencies.md) |
| 借鉴的数据流范式 | [dora-rs](https://dora-rs.ai) |

> 历史材料 `ref/` 仅作本地参考，不纳入本仓库。

---

## 9. 版本

| 版本 | 日期 | 变更 |
|------|------|------|
| v0.1 | 2026-08-26 | 初稿 |
| v0.2 | 2026-08-26 | 修正"greenfield 而非重构" + 三大能力并列 + dora 借鉴边界 |
| v0.3 | 2026-08-26 | A 是双向定期发现 + A/B 都是 C 的基础 + B 动态拓扑 |
| v0.4 | 2026-08-26 | **精简版**：去除与设计文档重复的内容，只保留关键约束；新增文档命名约定 `NN-<英文名称>.md` |
| v0.5 | 2026-08-26 | 新增规则：C++ 头文件后缀 `.hpp`，实现 `.cpp`；C 接口 `.h` / `.c` |
| v0.6 | 2026-08-26 | 新增 §6.1 docs 下文档一致性要求（命名/引用/术语/章节结构/TBD/变更记录 6 项） |
