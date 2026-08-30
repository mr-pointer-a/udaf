# ADR-004: 认证模型（PSK → PKI 分阶段）

> **状态**：提议（待评审）
> **日期**：2026-08-26
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §5.3 / §6.2 / §10.3
> **响应需求 TBD**：加密 / 认证 / 授权（§11.2 第 5 项）+ API 形态（§11.2 第 7 项）

---

## 1. 背景

需求 v1.0 对认证的要求：
- §5.3 安全契约：通信必须加密、双向认证；支持 PKI 与对称密钥两种模式
- §6.2 安全硬约束：严禁明文密码；设备身份必须可验证；白名单调度
- §7.2 认证失败处理：证书过期、PSK 错配、白名单为空、CRL 检查等
- §10.3 PSK → PKI 迁移路径：v1.0 PSK、v1.x 双模式共存、v2.0 弃用 PSK
- §10.1 MVP：v1.0 仅 PSK

## 2. 候选方案

### 2.1 v1.0 仅 PSK、v1.x 仅 PKI（直接切换）

| 维度 | 评估 |
|------|------|
| 迁移成本 | 高（设备需要重新配对） |
| 用户体验 | 差（升级时业务中断） |
| 代码复杂度 | 低（无需双模式） |

### 2.2 v1.0 PSK + v1.x 双模式共存（通过配置切换）

| 维度 | 评估 |
|------|------|
| 迁移成本 | 低（配置切换，零停机） |
| 用户体验 | 好 |
| 代码复杂度 | 中（需统一认证抽象层） |

### 2.3 v1.0 仅 PKI（提前引入）

| 维度 | 评估 |
|------|------|
| 迁移成本 | 极高（需要 CA 基础设施） |
| 用户体验 | 差（v1.0 上线即需要 PKI） |
| 代码复杂度 | 高（v1.0 即需要证书管理） |

## 3. 决策

**采用方案 2.2：v1.0 PSK + v1.x 双模式共存**，理由：

1. **满足需求 §10.3 迁移路径**：v1.0 PSK + v1.x 双模式 + v2.0 弃 PSK
2. **统一认证抽象层**：通过 `Authenticator` 接口，PSK 与 PKI 后端可插拔
3. **零停机迁移**：v1.x 升级时配置切换，业务不中断
4. **设备升级路径清晰**：已有 PSK 设备无需重烧固件

### 3.1 认证抽象层

```cpp
namespace udaf::crypto {

// 统一认证接口（v1.0 仅 PSK 实现，v1.x 增加 PKI 实现）
class Authenticator {
public:
    virtual ~Authenticator() = default;

    // 发起方：构造挑战
    virtual Result<Challenge> create_challenge(const PeerIdentity& peer) = 0;

    // 响应方：响应挑战
    virtual Result<Response> respond_to_challenge(const Challenge& challenge) = 0;

    // 验证方：验证响应
    virtual Result<void> verify_response(const Response& response,
                                          const PeerIdentity& expected_peer) = 0;

    // 凭据存储接口
    virtual Result<void> store_credential(const PeerIdentity& peer,
                                           const Credential& cred) = 0;
    virtual Result<Credential> load_credential(const PeerIdentity& peer) = 0;
};

// PSK 实现（v1.0）
class PskAuthenticator : public Authenticator {
    // 基于预共享密钥的 HMAC 挑战-响应
};

// PKI 实现（v1.x）
class PkiAuthenticator : public Authenticator {
    // 基于 X.509 证书链 + 私钥签名的挑战-响应
};

}  // namespace
```

### 3.2 阶段化迁移时间表

| 阶段 | 认证后端 | 配置项 |
|------|---------|--------|
| **v1.0** | PSK（仅） | `auth.mode = psk` |
| **v1.x** | PSK + PKI 双模式 | `auth.mode = psk | pki` |
| **v2.0** | PKI（仅，PSK 弃用） | `auth.mode = pki` |

### 3.3 v1.x 引入 PKI 的工作项

| 工作项 | 说明 |
|--------|------|
| **CA 基础设施** | 部署内部 CA（或使用 Let's Encrypt） |
| **证书签发流程** | 设备首次接入由 CA 颁发 X.509 证书 |
| **证书轮换** | 支持证书自动 / 手动轮换 |
| **CRL 缓存** | 设备端缓存证书撤销列表（满足 §7.2） |
| **协议版本协商** | 握手协议支持版本号（v1.0 PSK + v1.x PKI 共存） |
| **降级防护** | 服务端签名"支持的最低版本"，客户端校验（详见 ADR-007 §2.4） |
| **监控指标** | 证书过期前 30 天告警 |

## 4. 凭据存储

| 平台 | 存储方式 | 加密 |
|------|----------|------|
| **设备端 PSK（v1.0）** | 文件 `/etc/udaf/psk.bin`，权限 600 | PSK 本身是密钥（256 bit 随机），不额外加密 |
| **设备端 私钥（v1.x PKI）** | 文件 `/etc/udaf/privkey.pem`，权限 600 | 可选密码保护 |
| **主机端 PSK 池（v1.0）** | 文件 `/etc/udaf/psk_pool.enc`，权限 600 | **必须加密**（Argon2id 派生主密钥 + AES-256-GCM，详见 ADR-007 §2.5） |
| **主机端 CA 证书 + 设备证书缓存（v1.x）** | 文件 `/etc/udaf/ca/` | 不加密（证书公开） |

**PSK 质量约束**（详见 ADR-007）：
- PSK 熵 ≥ 256 bit（32 字节随机），禁止密码短语直接作为 PSK
- PSK 绑定设备 NodeId（PSK 按 NodeId 索引，支持单设备轮换）
- 主机端使用 HKDF 从 PSK 派生会话密钥（不直接使用 PSK 做 HMAC 密钥）

## 5. 握手性能

| 协议 | 握手延迟（P95） | 备注 |
|------|----------------|------|
| **PSK 握手** | < 2ms | 单次 HMAC 计算 |
| **PKI 握手** | < 50ms | TLS 1.3 完整握手（包含证书链验证） |

需求 §5.7 性能契约已量化此指标。

## 6. 后果

- ✅ v1.0 简单（仅 PSK）
- ✅ v1.x 平滑迁移（双模式共存）
- ✅ v2.0 完全切换 PKI（弃 PSK，提前 6 个月公告）
- ✅ 统一认证抽象层，可扩展未来新机制（如 OAuth）
- ⚠️ 需维护双模式代码（v1.x 期间）
- ⚠️ CA 基础设施需运维

## 7. 未来演进

- **v3.0+**：评估 OAuth 2.0 / WebAuthn 等现代认证机制（如有云端需求）
- **v3.0+**：评估量子安全算法（如 CRYSTALS-Kyber）

## 8. 引用

- [需求 §5.3 安全契约](../01-requirements.md#53-安全契约)
- [需求 §6.2 安全硬约束](../01-requirements.md#62-安全硬约束)
- [需求 §7.2 认证失败处理](../01-requirements.md#72-认证失败)
- [需求 §10.3 PSK → PKI 迁移路径](../01-requirements.md#103-psk--pki-迁移路径)
- [需求 §10.1 MVP 必含](../01-requirements.md#101-v10-mvp-必含)
