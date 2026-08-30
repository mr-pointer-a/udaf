# 详细设计（Phase 4）

> **版本**：v0.7
> **状态**：草稿
> **日期**：2026-08-27
> **前置**：[`docs/03-detailed-design.md`](03-detailed-design.md) v2.1 + [`docs/02-architecture.md`](02-architecture.md) v2.8

---

## 1. 概述

### 1.1 文档目的

本阶段在阶段3（概要设计）的基础上，完成以下输出：

1. **每个模块的类内部设计**：成员变量、方法的算法实现细节、状态机详细转换
2. **数据结构详细设计**：关键数据结构的字段布局、内存布局、对齐策略
3. **错误处理详细策略**：各模块错误的恢复策略、日志级别、告警条件
4. **线程模型详细设计**：各模块的线程划分、锁策略、无锁数据结构使用
5. **接口详细设计**：方法的前置条件、后置条件、异常安全性

### 1.2 与阶段3的边界

阶段3已输出：
- 头文件清单 + 路径映射（已完成）
- 公共API签名（已完成）
- 测试用例清单（已完成）
- 跨模块调用链（已完成）
- 错误码枚举值（已完成）

阶段4输出：
- 私有成员变量详细设计
- 方法实现的核心算法
- 状态机详细转换条件
- 线程安全实现细节
- 资源管理策略（RAII细节）

---

## 2. 核心模块详细设计

### 2.1 udaf::core 模块

#### 2.1.1 Result<T> 模板类

```cpp
template <typename T>
class [[nodiscard]] Result {
public:
    // 构造函数（使用标签消除歧义）
    constexpr Result() noexcept;
    template <typename U = T>
    constexpr Result(std::in_place_type_t<U>, U value) noexcept;
    constexpr Result(ErrorCode code) noexcept;

    Result(T value, ErrorCode code) = delete;  // 禁用歧义构造函数，使用 Result::ok(value) 或 Result::err(code)

    // 拷贝/移动
    Result(const Result&) = default;
    Result& operator=(const Result&) = default;
    Result(Result&&) noexcept;
    Result& operator=(Result&&) noexcept;

    // 工厂方法
    template <typename U = T>
    static constexpr Result ok(U value) noexcept;
    static constexpr Result err(ErrorCode code) noexcept;
    static Result uninitialized() noexcept;

    // void特化工厂
    static Result<void> ok() noexcept;

    // 状态查询
    constexpr bool is_ok() const noexcept;
    constexpr bool is_err() const noexcept;
    constexpr bool is_uninitialized() const noexcept;

    // 值访问
    constexpr T& value() &;
    constexpr const T& value() const&;
    constexpr T&& value() &&;
    constexpr ErrorCode error() const noexcept;
    constexpr T value_or(T default_val) const noexcept;

    // 链式调用（通用可调用）
    template <typename F>
    auto and_then(F&& fn) const noexcept -> std::invoke_result_t<F, T>;
    template <typename F>
    auto map(F&& fn) const noexcept -> std::invoke_result_t<F, T>;
    Result<T>& on_error(const std::function<void(ErrorCode)>& handler) noexcept;
    template <typename F>
    auto or_else(F&& fn) const noexcept -> std::invoke_result_t<F, ErrorCode>;

private:
    union {
        T value_;
    };
    ErrorCode error_;
    State state_;
};

// Result<void> 特化
template <>
class [[nodiscard]] Result<void> {
public:
    constexpr Result() noexcept;
    constexpr Result(ErrorCode code) noexcept;
    static Result<void> ok() noexcept;
    static Result<void> err(ErrorCode code) noexcept;

    constexpr bool is_ok() const noexcept;
    constexpr bool is_err() const noexcept;
    constexpr bool is_uninitialized() const noexcept;
    constexpr ErrorCode error() const noexcept;

    template <typename F>
    auto and_then(F&& fn) const noexcept -> std::invoke_result_t<F>;
    template <typename F>
    auto map(F&& fn) const noexcept -> std::invoke_result_t<F>;
    Result<void>& on_error(const std::function<void(ErrorCode)>& handler) noexcept;

private:
    ErrorCode error_;
    State state_;
};
```

**成员变量详细设计**：

| 成员变量 | 类型 | 说明 |
|---------|------|------|
| `value_` | `T`（union内） | 存储成功值，仅在 kOk 状态时有效 |
| `error_` | `ErrorCode` | 存储错误码 |
| `state_` | `State`（内部枚举） | 三态：kOk / kErr / kUninitialized |

**实现要点**：
- 使用 `union` 存储 value_ 避免默认构造时的初始化开销
- `Result<void>` 特化避免 `T value_` 的空类型问题
- 移除 `Result(T value, ErrorCode code)` 歧义构造函数，改用工厂方法
- `and_then`/`map` 改为通用可调用模板，支持 lambda/函数指针
- `and_then()` / `map()` 完美转发，保持引用语义
- `state_` 使用 `uint8_t` 紧凑存储，避免枚举大小问题
- 禁止异常抛出（遵守 CLAUDE.md §3.5）

#### 2.1.2 ErrorCode 枚举

> **权威源**：[`docs/adr/ADR-011-error-codes.md`](adr/ADR-011-error-codes.md) §2.3 定义完整枚举（61 条），本节不再重复。
>
> **命名约定**：`SCREAMING_SNAKE_CASE`，命名空间 `udaf::core`，基础类型 `uint32_t`。
>
> **本模块使用的 ErrorCode**：`NET_NOT_CONNECTED` / `NET_SEND_FAILED` / `PROTOCOL_TRUNCATED_BUFFER` / `SERIALIZE_TYPE_MISMATCH` / `SERIALIZE_VERSION_MISMATCH` / `TOPOLOGY_TRANSACTION_ALREADY_DONE`

### 2.2 udaf::ability_a::registry 模块

#### 2.2.1 ServiceRegistry 类

```cpp
class ServiceRegistry {
public:
    struct Config {
        uint32_t heartbeat_interval_ms_;
        uint32_t cleanup_interval_ms_;
        uint32_t max_service_age_ms_;
    };

    // 工厂方法
    static Result<std::unique_ptr<ServiceRegistry>> create(Config config) noexcept;

    // 服务注册
    Result<void> register_service(const ServiceDescriptor& descriptor) noexcept;
    Result<void> unregister_service(const std::string& service_id) noexcept;
    Result<void> update_service(const ServiceDescriptor& descriptor) noexcept;

    // 服务查询
    Result<std::vector<ServiceDescriptor>> get_services() const noexcept;
    Result<ServiceDescriptor> get_service(const std::string& service_id) const noexcept;

    // 订阅
    Result<std::unique_ptr<SubscriptionHandle>>
    subscribe(const std::string& service_id,
              ServiceCallback callback) noexcept;

    // 白名单
    Result<void> update_whitelist(const PeerWhitelist& whitelist) noexcept;

private:
    Config config_;
    std::unordered_map<std::string, ServiceEntry> services_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<SubscriptionHandle>>> subscriptions_;
    mutable std::shared_mutex mutex_;
    std::atomic<uint64_t> sequence_;
};
```

**成员变量详细设计**：

| 成员变量 | 类型 | 说明 |
|---------|------|------|
| `config_` | `Config` | 配置参数，包含心跳间隔、清理间隔等 |
| `services_` | `unordered_map` | 服务ID到服务条目的映射 |
| `subscriptions_` | `unordered_map` | 服务ID到订阅句柄列表的映射 |
| `mutex_` | `shared_mutex` | 读写锁，允许多读单写 |
| `sequence_` | `atomic<uint64_t>` | 版本号，用于订阅变化检测 |

**核心算法**：

1. **服务注册**：
   - 检查服务ID唯一性（读锁）
   - 插入新服务（写锁）
   - 触发订阅回调（无锁）
   - 递增sequence_

2. **服务订阅**：
   - 返回RAII SubscriptionHandle
   - Handle析构时自动取消订阅
   - 订阅列表线程安全

#### 2.2.2 SubscriptionHandle RAII 句柄

```cpp
class SubscriptionHandle {
public:
    SubscriptionHandle() noexcept = default;
    SubscriptionHandle(const SubscriptionHandle&) = delete;
    SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;
    SubscriptionHandle(SubscriptionHandle&& other) noexcept;
    SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept;
    ~SubscriptionHandle() noexcept;

    void release() noexcept;

private:
    friend class ServiceRegistry;
    SubscriptionHandle(std::shared_ptr<RegistryCore> core,
                       std::string service_id) noexcept;

    std::shared_ptr<RegistryCore> core_;
    std::string service_id_;
    bool released_;
};
```

**RAII 语义**：
- 移动构造/赋值后原对象无效
- 析构函数自动调用 `release()`
- `release()` 标记撤销订阅（原子操作）

### 2.3 udaf::ability_b::topology 模块

#### 2.3.1 Topology 类

```cpp
class Topology {
public:
    struct Config {
        bool enable_auto_save_;
        std::chrono::milliseconds auto_save_interval_;
    };

    static Result<std::unique_ptr<Topology>> create(Config config) noexcept;

    // 图操作
    Result<void> add_node(const NodeSpec& spec) noexcept;
    Result<void> remove_node(const std::string& node_id) noexcept;
    Result<void> connect(const PortRef& src, const PortRef& dst) noexcept;
    Result<void> disconnect(const PortRef& src, const PortRef& dst) noexcept;

    // 查询
    Result<std::vector<NodeSpec>> all_nodes() const noexcept;
    Result<std::vector<EdgeSpec>> all_edges() const noexcept;
    Result<bool> has_cycle() const noexcept;

    // 事务（详见 TopologyTransaction 类定义）
    TopologyTransaction begin_transaction() noexcept;
    Result<void> commit(TopologyTransaction&& tx) noexcept;
    Result<void> rollback(TopologyTransaction& tx) noexcept;

    // 快照与恢复
    Result<void> load_from_yaml(std::string_view path) noexcept;
    Result<void> replay_from_wal() noexcept;

private:
    friend class TopologyTransaction;
    Topology(Wal& wal) noexcept;

    Config config_;
    Wal& wal_;
    std::unordered_map<std::string, NodeSpec> nodes_;
    std::unordered_map<std::string, std::unordered_set<std::string>> edges_;
    mutable std::shared_mutex mutex_;
    std::atomic<uint64_t> version_;
};
```

**成员变量详细设计**：

| 成员变量 | 类型 | 说明 |
|---------|------|------|
| `config_` | `Config` | 配置参数 |
| `wal_` | `Wal&` | WAL 引用，事务提交时持久化 |
| `nodes_` | `unordered_map` | 节点ID到节点规格的映射 |
| `edges_` | `unordered_map` | 邻接表表示的有向图 |
| `mutex_` | `shared_mutex` | 读写锁 |
| `version_` | `atomic<uint64_t>` | 图版本号，用于变化检测 |

**TopolopyTransaction 类**：

```cpp
class TopologyTransaction {
public:
    ~TopologyTransaction() noexcept;

    TopologyTransaction(TopologyTransaction&&) noexcept;
    TopologyTransaction& operator=(TopologyTransaction&&) noexcept;

    TopologyTransaction(const TopologyTransaction&) = delete;
    TopologyTransaction& operator=(const TopologyTransaction&) = delete;

    // 注册正向操作（commit时执行）
    void add_forward_action(std::function<void()> action) noexcept;

    // 注册回滚操作（rollback时逆序执行）
    void add_rollback_action(std::function<void()> action) noexcept;

private:
    friend class Topology;
    TopologyTransaction(Topology& topo) noexcept;

    Topology& topo_;
    std::unique_lock<std::shared_mutex> lock_;
    bool committed_;
    bool rolled_back_;
    std::vector<std::function<void()>> forward_actions_;
    std::vector<std::function<void()>> rollback_actions_;
};
```

**事务实现要点**：
- `Topology::begin_transaction()` 创建 `TopologyTransaction` 并获取写锁
- `Topology::commit(TopologyTransaction&& tx)` 接受右值引用，防止重复提交（评审 M-12）
- `commit()` 批量执行 `forward_actions_` 中的正向操作，成功后释放锁、递增版本号
- `rollback()` 逆序执行 `rollback_actions_` 中的逆操作，释放锁
- 析构函数：未提交且未回滚时自动 rollback，保证异常安全
- `add_forward_action` / `add_rollback_action` 由 `Topology::add_node` / `connect` 等方法内部调用，用户无需手动注册

### 2.4 udaf::ability_b::node 模块

#### 2.4.1 Node 抽象基类

```cpp
class Node {
public:
    struct PortInfo {
        std::string name_;
        std::string type_;
        PortDirection direction_;  // kInput / kOutput
    };

    struct Config {
        std::string node_id_;
        std::string config_yaml_;
    };

    virtual ~Node() = default;

    // 节点元信息
    virtual std::string_view name() const noexcept = 0;
    virtual const std::vector<PortInfo>& inputs() const noexcept = 0;
    virtual const std::vector<PortInfo>& outputs() const noexcept = 0;

    // 生命周期
    virtual Result<void> init(const Config& config) noexcept = 0;
    virtual Result<void> start() noexcept = 0;
    virtual Result<void> stop() noexcept = 0;
    virtual Result<void> reload(const std::string& config_yaml) noexcept = 0;

    // 端口操作
    template <typename T>
    Result<void> send(const std::string& port_name, const T& data) noexcept;
    template <typename T>
    Result<void> recv(const std::string& port_name, T& data) noexcept;

    // 状态查询
    virtual LifecycleState state() const noexcept = 0;

protected:
    Node() = default;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    std::vector<InputPortBase*> input_ports_;
    std::vector<OutputPortBase*> output_ports_;
    LifecycleState state_;
    mutable std::mutex state_mutex_;
};
```

**状态机详细设计**：

```
         ┌─────────────────────────────────────────────────────────┐
         │                                                         │
         ▼                                                         │
    ┌─────────┐   init()    ┌───────────┐   start()    ┌───────┐ │
    │  IDLE   │ ──────────▶ │ INITIALIZED│ ───────────▶ │ RUNNING│ │
    └─────────┘              └───────────┘              └───┬───┘ │
         ▲                          │                        │     │
         │                          │ stop()                 │stop()│
         │                          ▼                        ▼     │
         │                   ┌───────────┐              ┌────────┐ │
         │                   │  STOPPING │◀─────────────│ RELOADING│
         │                   └───────────┘   reload()    └────────┘
         │                          │                        │
         └──────────────────────────┴────────────────────────┘
                              stop()
```

**生命周期状态**：

| 状态 | 说明 | 允许的转换 |
|------|------|-----------|
| `kIdle` | 初始状态 | → `kInitialized` (init) |
| `kInitialized` | 已初始化 | → `kRunning` (start) / → `kStopping` (stop) |
| `kRunning` | 运行中 | → `kReloading` (reload) / → `kStopping` (stop) |
| `kReloading` | 热更新中 | → `kRunning` (reload完成) / → `kStopping` (stop) |
| `kStopping` | 停止中 | → `kIdle` (stop完成) |

### 2.5 udaf::ability_b::transport 模块

#### 2.5.1 Channel<T> 模板类（PIMPL实现）

```cpp
template <typename T>
class Channel {
public:
    Channel() noexcept;
    ~Channel();

    Channel(Channel&&) noexcept;
    Channel& operator=(Channel&&) noexcept;

    // 禁用拷贝
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // 发送接收
    Result<SendResult> send(const T& data) noexcept;
    Result<RecvStatus> recv(T& data, std::chrono::milliseconds timeout) noexcept;

    // 连接管理
    Result<void> connect(const std::string& address) noexcept;
    Result<void> bind(const std::string& address) noexcept;
    void close() noexcept;

    // 属性
    TransportType type() const noexcept;
    bool is_connected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

template <typename T>
class Channel<T>::Impl {
public:
    void* socket_;                           // ZMQ socket
    std::shared_ptr<void> context_;          // ZMQ context（共享_ptr管理生命周期）
    TransportType type_;
    std::atomic<bool> connected_;
    std::vector<uint8_t> recv_buffer_;
    std::vector<uint8_t> send_buffer_;
    std::mutex socket_mutex_;                // 保护socket并发访问
};

**PIMPL 实现要点**：
- `void*` 持有 ZMQ socket，通过前置声明隐藏实现
- `context_` 使用 `shared_ptr<void>` 管理共享 ZMQ context，引用计数归零时析构
- 模板实例化在 `.cpp` 中完成，减少编译依赖
- `socket_` 析构时显式调用 `zmq_close(socket_)` 关闭 socket
- `socket_mutex_` 保护多线程并发 send/recv 操作

#### 2.5.2 发送流程

```cpp
template <typename T>
Result<SendResult> Channel<T>::send(const T& data) noexcept {
    if (!connected_.load(std::memory_order_acquire)) {
        return Result<SendResult>::err(ErrorCode::NET_NOT_CONNECTED);
    }

    // 序列化（在锁外执行，避免持锁期间分配内存）
    Serializer<T> serializer;
    auto encode_result = serializer.encode(data);
    if (encode_result.is_err()) {
        return Result<SendResult>::err(encode_result.error());
    }

    // 发送（加锁保护socket并发访问）
    std::lock_guard<std::mutex> lock(pimpl_->socket_mutex_);
    int flags = ZMQ_DONTWAIT;
    int n = zmq_send(pimpl_->socket_, encode_result.value().data(),
                     encode_result.value().size(), flags);

    if (n < 0) {
        int err = zmq_errno();
        if (err == EAGAIN) {
            return Result<SendResult>::ok(SendResult::kWouldBlock);
        }
        // ETERM: context已终止 EFSM: socket状态错误 EINTR: 信号中断
        if (err == ETERM || err == EFSM || err == EINTR) {
            connected_.store(false, std::memory_order_release);
        }
        return Result<SendResult>::err(ErrorCode::NET_SEND_FAILED);
    }

    return Result<SendResult>::ok(SendResult::kSuccess);
}
```

### 2.6 udaf::ability_b::port 模块

#### 2.6.1 InputPort<T> 类

```cpp
template <typename T>
class InputPort {
public:
    InputPort(std::string name, std::string type) noexcept;
    ~InputPort() = default;

    InputPort(InputPort&&) noexcept = default;
    InputPort& operator=(InputPort&&) noexcept = default;

    // 禁用拷贝
    InputPort(const InputPort&) = delete;
    InputPort& operator=(const InputPort&) = delete;

    // 端口信息
    const PortInfo& info() const noexcept { return info_; }
    static constexpr PortDirection direction() noexcept { return PortDirection::kInput; }

    // 接收
    Result<void> try_recv(T& data) noexcept;
    Result<void> recv(T& data, std::chrono::milliseconds timeout) noexcept;

    // 队列管理
    size_t queue_size() const noexcept;
    void set_queue_size(size_t max_size) noexcept;

private:
    PortInfo info_;
    std::queue<T, std::deque<T, STLAllocator<T>>> queue_;
    size_t max_queue_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};
```

**队列实现要点**：
- 使用 `std::deque` 而非 `std::queue` 默认的 `std::deque`
- 指定 `STLAllocator<T>` 支持自定义内存分配器
- 有界队列，满时 `try_recv` 返回 `kQueueFull`

#### 2.6.2 OutputPort<T> 类

```cpp
template <typename T>
class OutputPort {
public:
    OutputPort(std::string name, std::string type) noexcept;
    ~OutputPort() = default;

    OutputPort(OutputPort&&) noexcept = default;
    OutputPort& operator=(OutputPort&&) noexcept = default;

    // 禁用拷贝
    OutputPort(const OutputPort&) = delete;
    OutputPort& operator=(const OutputPort&) = delete;

    // 端口信息
    const PortInfo& info() const noexcept { return info_; }
    static constexpr PortDirection direction() noexcept { return PortDirection::kOutput; }

    // 发送
    Result<void> try_send(const T& data) noexcept;
    Result<void> send(const T& data, std::chrono::milliseconds timeout) noexcept;

private:
    PortInfo info_;
    std::vector<std::weak_ptr<InputPort<T>>> subscribers_;
    mutable std::mutex mutex_;
};
```

### 2.7 udaf::ability_b::serialization 模块

#### 2.7.1 Serializer<T> 类

```cpp
template <typename T>
class Serializer {
public:
    Serializer() noexcept = default;
    ~Serializer() = default;

    // 序列化
    Result<std::vector<uint8_t>> encode(const T& value) const noexcept;
    Result<std::shared_ptr<const T>> decode(
        std::span<const uint8_t> data) noexcept;

    // 类型校验
    bool accepts_type(std::string_view type_name) const noexcept;
    static std::string_view type_name_static() noexcept;

private:
    SerializerBase* base_;
};

template <typename T>
class SerializerBase {
public:
    virtual ~SerializerBase() = default;
    virtual Result<std::vector<uint8_t>> do_encode(const T& value) = 0;
    virtual Result<std::shared_ptr<const T>> do_decode(
        std::span<const uint8_t> data) = 0;
    virtual bool accepts_type(std::string_view type_name) const = 0;
};
```

**decode 流程**：

```cpp
template <typename T>
Result<std::shared_ptr<const T>> Serializer<T>::decode(
    std::span<const uint8_t> data) noexcept {
    // 1. 类型校验
    if (!base_->accepts_type(type_name_static())) {
        return Result<std::shared_ptr<const T>>::err(
            ErrorCode::SERIALIZE_TYPE_MISMATCH);
    }

    // 2. schema_version 校验
    if (data.size() < sizeof(uint32_t)) {
        return Result<std::shared_ptr<const T>>::err(
            ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
    }

    // 使用 memcpy 避免违反严格别名规则
    uint32_t schema_version;
    std::memcpy(&schema_version, data.data(), sizeof(uint32_t));
    // 字节序转换（网络字节序转主机字节序）
    schema_version = ntohl(schema_version);
    if (schema_version != SCHEMA_VERSION) {
        return Result<std::shared_ptr<const T>>::err(
            ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }

    // 3. 解码
    return base_->do_decode(data);
}
```

### 2.8 udaf::platform::fs 模块

#### 2.8.1 UniqueFd RAII 句柄

```cpp
class UniqueFd {
public:
    UniqueFd() noexcept : fd_(-1) {}
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    ~UniqueFd() {
        if (fd_ >= 0) {
            // Linux上close()被信号打断时仍关闭fd，但为移植性使用TEMP_FAILURE_RETRY
            int ret;
            do { ret = ::close(fd_); } while (ret < 0 && errno == EINTR);
        }
    }

    // 禁用拷贝
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    // 移动
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                int ret;
                do { ret = ::close(fd_); } while (ret < 0 && errno == EINTR);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    int release() noexcept { int fd = fd_; fd_ = -1; return fd; }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            int ret;
            do { ret = ::close(fd_); } while (ret < 0 && errno == EINTR);
        }
        fd_ = fd;
    }
    bool is_valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return is_valid(); }

private:
    int fd_;
};
```

**Rule of Five 实现**：
- 禁用拷贝构造/赋值
- 移动构造/赋值转移所有权
- 析构函数关闭fd

#### 2.8.2 Wal（日志）类

```cpp
class Wal {
public:
    struct Entry {
        uint32_t schema_version_;           // 4字节
        uint64_t sequence_;                 // 8字节
        uint64_t timestamp_ns_;             // 8字节
        std::string action_;                // 字符串（堆分配）
        std::vector<uint8_t> payload_;      // 向量（堆分配）
    };
    // 内存布局：20字节头部 + payload_ 8字节对齐

    struct Config {
        std::string path_;
        uint64_t max_size_bytes_;
        uint32_t max_entries_;
    };

    static Result<std::unique_ptr<Wal>> create(Config config) noexcept;

    Result<void> append(const std::string& action,
                        std::span<const uint8_t> payload) noexcept;
    Result<void> replay(WalCallback callback) noexcept;
    Result<void> truncate(uint64_t keep_sequence) noexcept;

    // WAL 属性
    uint64_t current_sequence() const noexcept;
    uint64_t size_bytes() const noexcept;

private:
    Wal(Config config, UniqueFd fd) noexcept;

    Config config_;
    UniqueFd fd_;
    uint64_t current_sequence_;
    std::atomic<uint64_t> size_bytes_;
    mutable std::mutex mutex_;
};
```

**Rule of Five**：

```cpp
// 显式禁用拷贝
Wal(const Wal&) = delete;
Wal& operator=(const Wal&) = delete;

// 移动语义
Wal(Wal&&) noexcept;
Wal& operator=(Wal&&) noexcept;

// noexcept 保证
~Wal() noexcept = default;
```

### 2.9 udaf::ability_a::discovery 模块

#### 2.9.1 Advertiser 类

```cpp
class Advertiser {
public:
    struct Config {
        std::string service_id_;
        std::string service_type_;
        uint16_t port_;
        std::chrono::milliseconds advertise_interval_;
        std::vector<std::string> protocols_;
    };

    static Result<std::unique_ptr<Advertiser>> create(Config config) noexcept;

    Result<void> start() noexcept;
    Result<void> stop() noexcept;

    void update_payload(const std::string& key, const std::string& value) noexcept;

private:
    Config config_;
    std::unique_ptr<Transport> transport_;
    std::atomic<bool> running_;
    std::unordered_map<std::string, std::string> payload_;
    mutable std::mutex payload_mutex_;
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    std::thread broadcast_thread_;  // 替代匿名线程，stop()时join
};
```

**广播算法**：

```cpp
Result<void> Advertiser::start() noexcept {
    running_.store(true, std::memory_order_release);

    // 定期广播线程 - 使用成员变量管理生命周期
    broadcast_thread_ = std::thread([this]() {
        while (running_.load(std::memory_order_acquire)) {
            // payload_ 读取需要加锁（使用unique_lock保护）
            std::unique_lock lock(payload_mutex_);
            auto payload = build_advertisement_payload();
            lock.unlock();

            auto result = transport_->send_to_broadcast(payload);

            if (result.is_err()) {
                // 日志记录错误，不终止广播
                log_error("Advertise broadcast failed: {}", result.error());
            }

            // 使用条件变量等待，可被stop()立即唤醒
            std::unique_lock<std::mutex> cv_lock(cv_mutex_);
            cv_.wait_for(cv_lock, config_.advertise_interval_,
                [this]() { return !running_.load(std::memory_order_acquire); });
        }
    });

    return Result<void>::ok();
}

Result<void> Advertiser::stop() noexcept {
    running_.store(false, std::memory_order_release);
    cv_.notify_all();  // 唤醒等待中的线程
    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();  // 等待线程结束
    }
    return Result<void>::ok();
}
```

#### 2.9.2 Scanner 类

```cpp
class Scanner {
public:
    struct Config {
        std::chrono::milliseconds scan_interval_;
        std::vector<std::string> protocols_;
        std::chrono::milliseconds response_timeout_;
    };

    static Result<std::unique_ptr<Scanner>> create(Config config) noexcept;

    Result<void> start() noexcept;
    Result<void> stop() noexcept;

    // 订阅发现事件
    Result<std::unique_ptr<SubscriptionHandle>>
    subscribe(DiscoveryCallback callback) noexcept;

    // 查询
    Result<std::vector<ServiceDescriptor>> get_discovered_services() const noexcept;

private:
    void on_advertisement_received(const AdvertisementPayload& adv) noexcept;

    Config config_;
    std::unique_ptr<Transport> transport_;
    std::atomic<bool> running_;
    std::unordered_map<std::string, ServiceDescriptor> discovered_;
    std::chrono::steady_clock::time_point last_scan_;
    mutable std::shared_mutex discovered_mutex_;
};
```

**去重与超时**：

```cpp
void Scanner::on_advertisement_received(
    const AdvertisementPayload& adv) noexcept {
    std::unique_lock lock(discovered_mutex_);

    auto it = discovered_.find(adv.service_id());
    if (it != discovered_.end()) {
        // 更新现有条目
        it->second = adv;
        it->second.set_last_seen(
            std::chrono::steady_clock::now());
    } else {
        // 新增条目
        discovered_.emplace(adv.service_id(), adv);
    }
}
```

### 2.10 udaf::crypto 模块

#### 2.10.1 TlsContext 类

```cpp
class TlsContext {
public:
    struct Config {
        std::string cert_file_;
        std::string key_file_;
        std::string ca_file_;
        bool verify_peer_;
        std::string ciphers_;
    };

    static Result<std::unique_ptr<TlsContext>> create(Config config) noexcept;

    Result<void> configure_session(void* zmq_socket) noexcept;
    Result<void> handshake(void* zmq_socket) noexcept;

    // PSK 握手
    Result<std::vector<uint8_t>> do_psk_handshake(
        std::span<const uint8_t> client_msg) noexcept;

    // 属性
    bool is_initialized() const noexcept { return initialized_; }

private:
    Config config_;
    void* ctx_;  // SSL_CTX*
    bool initialized_;
    std::atomic<uint64_t> session_count_;
};
```

### 2.11 udaf::ability_c::nodes 模块

#### 2.11.1 CmdExecNode 类

```cpp
class CmdExecNode : public Node {
public:
    CmdExecNode() noexcept;
    ~CmdExecNode() override = default;

    // Node 接口
    std::string_view name() const noexcept override;
    const std::vector<PortInfo>& inputs() const noexcept override;
    const std::vector<PortInfo>& outputs() const noexcept override;

    Result<void> init(const Config& config) noexcept override;
    Result<void> start() noexcept override;
    Result<void> stop() noexcept override;
    Result<void> reload(const std::string& config_yaml) noexcept override;

    // 内部端口
    InputPort<CmdRequest>& cmd_input() noexcept { return cmd_input_; }
    OutputPort<CmdResult>& result_output() noexcept { return result_output_; }

private:
    void process_cmd_request() noexcept;

    // 缓存的端口信息
    static const std::vector<PortInfo> kInputs_;
    static const std::vector<PortInfo> kOutputs_;

    InputPort<CmdRequest> cmd_input_;
    OutputPort<CmdResult> result_output_;
    std::unique_ptr<ProcessExecutor> executor_;
    std::atomic<bool> running_;
};
```

**端口信息缓存**：

```cpp
const std::vector<Node::PortInfo> CmdExecNode::kInputs_ = {
    {"cmd_in", "CmdRequest", PortDirection::kInput},
};

const std::vector<Node::PortInfo> CmdExecNode::kOutputs_ = {
    {"result_out", "CmdResult", PortDirection::kOutput},
};
```

---

## 3. 接口设计详细说明

### 3.1 前置条件与后置条件

#### ServiceRegistry::register_service

**前置条件**：
- `descriptor.service_id` 非空
- `descriptor.service_id` 长度 ≤ 256
- `descriptor.type` 为已知服务类型

**后置条件**：
- 服务成功注册到 `services_`
- 所有订阅该服务的回调被触发
- 返回 `kOk`

**异常安全性**：strong guarantee（操作原子化）

#### Topology::commit

**前置条件**：
- 事务未提交
- 事务未回滚
- 图状态满足所有不变量

**后置条件**：
- 所有修改已应用到拓扑图
- `version_` 递增
- `committed_` 标志置位

**异常安全性**：noexcept（禁止异常）

### 3.2 线程安全约定

| 模块 | 锁策略 | 原因 |
|------|--------|------|
| ServiceRegistry | `shared_mutex` 多读单写 | 读多写少模式 |
| Topology | `shared_mutex` 多读单写 | 读多写少模式 |
| Node | `mutex` + `condition_variable` | 状态转换需要原子 |
| InputPort/OutputPort | `mutex` 保护队列 | 队列操作需要互斥 |
| Scanner discovered_ | `shared_mutex` 多读单写 | 读多写少模式 |

### 3.3 资源管理策略

**RAII 原则**：
1. 所有资源通过 RAII 类管理
2. 禁止裸 `new/delete`
3. 禁止裸文件描述符/指针

**内存分配**：
- 短期对象使用栈分配
- 长期对象使用 `std::unique_ptr` / `std::shared_ptr`
- 容器预分配容量避免多次扩容

---

## 4. 数据结构内存布局

### 4.1 ServiceDescriptor

```cpp
struct ServiceDescriptor {
    std::string service_id_;      // 字符串（堆分配）
    std::string service_type_;    // 字符串（堆分配）
    std::string host_;            // 字符串（堆分配）
    uint16_t port_;               // 2字节
    std::vector<std::string> protocols_;  // 向量（堆分配）
    std::unordered_map<std::string, std::string> metadata_;  // 哈希表（堆分配）
    std::chrono::steady_clock::time_point last_seen_;  // 8字节
};
// 总大小：约 64 字节（不含字符串内容）+ 字符串数据
```

### 4.2 PortInfo

```cpp
struct PortInfo {
    std::string name_;   // 字符串（堆分配）
    std::string type_;   // 字符串（堆分配）
};
// 内存布局：两个指针 + size + capacity（字符串 SSO 优化）
// 小字符串（≤15字节）无堆分配
```

---

## 5. 错误处理详细策略

### 5.1 错误恢复策略

| 错误类型 | 恢复策略 | 日志级别 |
|---------|---------|---------|
| 网络超时 | 重试（指数退避） | WARN |
| 协议解析错误 | 丢弃连接 | ERROR |
| 序列化错误 | 返回错误码 | WARN |
| 资源满 | 背压/限流 | WARN |
| 权限错误 | 拒绝操作 | ERROR |

### 5.2 日志级别约定

| 级别 | 使用场景 |
|------|---------|
| ERROR | 操作失败需人工介入 |
| WARN | 操作失败但可自恢复 |
| INFO | 重要业务事件 |
| DEBUG | 调试信息 |

### 5.3 告警条件

- 连续5次网络发送失败
- 内存使用超过配置的80%
- 队列积压超过1000条
- 节点异常重启超过3次/分钟

---

## 6. 状态机详细转换表

### 6.1 Node 生命周期状态机

| 当前状态 | 事件 | 条件检查 | 目标状态 | 动作 |
|---------|------|---------|---------|------|
| kIdle | init() | config有效 | kInitialized | 初始化成员 |
| kInitialized | start() | - | kRunning | 启动工作线程 |
| kRunning | reload() | 新配置有效 | kReloading | 暂停端口 |
| kRunning | stop() | - | kStopping | 停止工作线程 |
| kReloading | reload完成 | - | kRunning | 恢复端口 |
| kReloading | stop() | - | kStopping | 停止工作线程 |
| kStopping | 停止完成 | - | kIdle | 清理资源 |

---

## 7. 性能优化设计

### 7.1 无锁数据结构使用场景

| 数据结构 | 使用场景 | 实现方式 |
|---------|---------|---------|
| `std::atomic` | 版本号、计数器 | `memory_order` 语义 |
| `std::atomic_flag` | 一次性标志 | spinlock |
| 消息队列 | 端口队列 | 内存屏障保证 |

### 7.2 内存分配优化

1. **小字符串优化（SSO）**：长度 ≤ 15 的字符串不堆分配
2. **容器预分配**：`reserve()` 预分配容量
3. **对象池**：高频分配/释放对象使用对象池（如 Wal::Entry）
4. **内存映射文件**：大文件使用 `mmap`

### 7.3 缓存优化

1. **PortInfo 缓存**：InputPort/OutputPort 缓存 PortInfo 避免重复构造
2. **序列化缓存**：频繁序列化的对象缓存结果
3. **拓扑缓存**：Topology 变更后缓存临接表

---

## 8. 验收标准

- [ ] 每个类的成员变量详细设计完成
- [ ] 关键方法的算法实现明确
- [ ] 状态机转换条件完整
- [ ] 线程安全策略明确
- [ ] 资源管理 RAII 策略明确
- [ ] 错误处理策略完整
- [ ] 内存布局符合性能要求
- [ ] 无裸 new/delete/异常

---

## 9. 附录

### 9.1 TBD（阶段4待明确）

- **TBD-4-1**：ProcessExecutor 的 fork/exec 策略具体实现（是否使用 vfork）
- **TBD-4-2**：Serializer 的 schema 版本协商机制
- **TBD-4-3**：背压策略的具体水线百分比实现

---

### 9.2 变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v0.1 | 2026-08-27 | 初稿 |
| v0.2 | 2026-08-27 | 第一轮review修复：reinterpret_cast改为memcpy；SubscriptionHandle存储改为unique_ptr；Advertiser线程管理改为成员变量+join；ZMQ context改用shared_ptr；Transaction添加写锁保护 |
| v0.3 | 2026-08-27 | 第二轮review修复：shared_lock改为unique_lock；Transaction语义修正（forward_actions_正向/commit执行，rollback_actions_逆向/rollback执行）；Transaction析构加noexcept；移除commit()右值限定；Result<T>增加Result<void>特化、移除歧义构造函数、and_then/map改为通用可调用 |
| v0.4 | 2026-08-27 | 第三轮review修复：Result<T>歧义构造函数显式=delete；Result<void>添加err()工厂方法；Transaction移动构造/赋值修正（防止双重回滚terminate）；Channel::send添加socket_mutex_加锁和ETERM/EFSM/EINTR错误处理；Advertiser::stop使用条件变量立即唤醒；UniqueFd的close/reset添加EINTR处理；注释修正（defer_lock改为立即加锁） |
| v0.5 | 2026-08-27 | ErrorCode 对齐 ADR-011：§2.1.2 删除内联枚举定义改为引用 ADR-011；代码中 ErrorCode 命名从 kPascalCase 改为 SCREAMING_SNAKE（对齐 ADR-011 §2.3）；新增 NET_SEND_FAILED/NET_NOT_CONNECTED/TOPOLOGY_TRANSACTION_ALREADY_DONE 三个错误码 |
| v0.6 | 2026-08-27 | 对齐03（概要设计）权威定义：Transaction→TopologyTransaction（独立类）；commit()→commit(TopologyTransaction&& tx)（右值引用防重复提交）；add_edge/remove_edge→connect/disconnect；Topology 新增 Wal& wal_ 成员；删除 Wal::Entry 重复定义；前置引用03版本号修正 v1.9→v2.0 |
