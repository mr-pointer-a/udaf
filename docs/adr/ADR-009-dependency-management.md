# ADR-009: 依赖管理方案

> **状态**：已批准
> **日期**：2026-08-26（提议）
> **批准日期**：2026-09-01（实现阶段完成，446/446 测试通过，0 编译警告）
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §11.2 第 1 项
> **响应需求 TBD**：第三方库选型（§11.2 第 1 项）+ 不允许引入依赖（CLAUDE.md §3.3）

---

## 1. 背景

UDAF 设备端 C++ 代码需集成以下第三方依赖：
- **传输**：libzmq（ADR-001）
- **序列化**：protobuf lite（ADR-002）
- **加密**：mbedTLS（ADR-004 / ADR-007）
- **配置**：yaml-cpp（CLAUDE.md §3.3）
- **日志**：spdlog（CLAUDE.md §3.3）
- **可观测性**：OpenTelemetry C++（ADR-008）
- **测试**：gtest（CLAUDE.md §3.3）

需求 §11.2 第 1 项明确要求"评估依赖管理方案"，CLAUDE.md §3.3 列出禁止项：Rust 运行时、dora-rs 二进制、GPL 协议库、未维护 > 18 个月的开源项目。

## 2. 候选方案

### 2.1 vcpkg（微软 + 社区）

| 维度 | 评估 |
|------|------|
| 模式 | 集中式 manifest + 缓存 |
| CMake 集成 | 原生 `find_package`（toolchain 文件） |
| 跨平台 | Linux / macOS / Windows 全覆盖；x86_64 / aarch64 |
| 依赖锁定 | baseline + versions 字段 |
| 二进制缓存 | 内置（CI 加速） |
| 嵌入式支持 | 优秀（triplet 控制所有构建参数） |
| 学习成本 | 低 |
| 许可证 | MIT |

**优势**：
- CMake 集成最丝滑
- 嵌入式 cross-compile 体验好（`arm-linux-gnueabihf.cmake` triplet）
- CI 二进制缓存节省时间
- 与项目自身 CMake 结构兼容

**劣势**：
- 大型项目首次构建较慢
- baseline 升级需手动控制

### 2.2 Conan 2.x

| 维度 | 评估 |
|------|------|
| 模式 | 集中式 recipe + 缓存 |
| CMake 集成 | 通过 `conanfile.txt` + generator |
| 跨平台 | 全平台 |
| 依赖锁定 | `conan.lock` |
| 二进制缓存 | 内置 |
| 嵌入式支持 | 通过 profile + settings 定制 |
| 学习成本 | 中（conanfile.py 语法） |
| 许可证 | MIT |

**优势**：
- 二进制缓存成熟
- 中心仓库 conan-center 覆盖广
- 商业项目采用多

**劣势**：
- 与 CMake 集成需 generator 间接层
- 嵌入式 cross-compile profile 较复杂

### 2.3 CMake FetchContent

| 维度 | 评估 |
|------|------|
| 模式 | 源码下载 + 内联构建 |
| CMake 集成 | 原生 |
| 跨平台 | 取决于各依赖 |
| 依赖锁定 | Git tag / commit hash |
| 二进制缓存 | 无（每次重新构建） |
| 嵌入式支持 | 取决于各依赖 |
| 学习成本 | 低 |
| 许可证 | N/A（CMake 内置） |

**优势**：
- 零额外依赖（仅需 CMake）
- 简单直接

**劣势**：
- 无二进制缓存（CI 时间长）
- 依赖管理粗粒度（git tag 不稳定）
- protobuf 等大型库构建时间长

### 2.4 系统包管理器（apt / yum）

| 维度 | 评估 |
|------|------|
| 模式 | OS 包管理器安装 |
| CMake 集成 | `find_package` 假设系统已装 |
| 跨平台 | 不可移植（Debian / RHEL / Alpine 差异） |
| 依赖锁定 | 不可能（系统包版本不可控） |
| 二进制缓存 | 系统级 |
| 嵌入式支持 | 差（嵌入式设备无 apt） |
| 学习成本 | 低 |
| 许可证 | 各包 |

**优势**：
- 部署简单（apt install）

**劣势**：
- 版本不可控（不同发行版 ABI 差异）
- 嵌入式目标板通常无 apt
- 不满足"可重复构建"

## 3. 决策

**采用方案 2.1：vcpkg 作为主要依赖管理器**，理由：

1. **CMake 集成最佳**：通过 toolchain 文件直接桥接到现有 `find_package`，无需侵入项目 CMake 结构
2. **嵌入式 cross-compile 友好**：`triplet` 文件控制所有编译参数（`CMAKE_C_COMPILER`、`CMAKE_CXX_COMPILER`、`CMAKE_SYSTEM_NAME`、`CMAKE_FIND_ROOT_PATH` 等）
3. **二进制缓存**：CI 上传预编译依赖，开发者无需重复构建 protobuf
4. **依赖锁定**：`vcpkg.json` 的 `version` / `baseline` 字段 + `vcpkg-configuration.json` 锁定 registry
5. **离线模式**：支持 `--no-network` + 本地 ports overlay

### 3.1 vcpkg 配置结构

```
udaf/
├── cmake/
│   ├── toolchains/
│   │   ├── linux-amd64.cmake          # 主机端开发 triplet
│   │   ├── linux-aarch64.cmake        # 设备端目标 triplet
│   │   └── linux-amd64-static.cmake   # 静态链接 variant
│   └── vcpkg-configuration.json       # 锁定 registry
├── vcpkg.json                          # 项目依赖清单
└── ports/                              # 本地 ports 覆盖（如有定制 patch）
    └── mbedtls/
        └── udaf-disable-tests.patch
```

### 3.2 vcpkg.json（依赖清单）

```json
{
  "name": "udaf",
  "version-string": "1.0.0",
  "dependencies": [
    "zeromq",
    "protobuf",
    "mbedtls",
    {
      "name": "yaml-cpp",
      "version>=": "0.8.0"
    },
    "spdlog",
    {
      "name": "opentelemetry-cpp",
      "features": ["prometheus-exporter"]
    },
    {
      "name": "gtest",
      "version>=": "1.14.0"
    }
  ],
  "builtin-baseline": "9e4b37c5b5f5e8b3e5f5e8b3e5f5e8b3e5f5e8b3"
}
```

### 3.3 嵌入式 triplet（linux-aarch64）

```cmake
# cmake/toolchains/linux-aarch64.cmake
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CMAKE_CONFIG_TYPE Release)

# 交叉编译器路径（可被环境变量覆盖）
if(DEFINED ENV{CROSS_PREFIX})
    set(CROSS_PREFIX $ENV{CROSS_PREFIX})
else()
    set(CROSS_PREFIX /opt/aarch64-linux-gnu)
endif()

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/aarch64-cross.cmake")
```

```cmake
# cmake/toolchains/aarch64-cross.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${CROSS_PREFIX}/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}/bin/aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH ${CROSS_PREFIX} ${CROSS_PREFIX}/../aarch64-linux-gnu/libc)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 静态链接所有第三方库（设备端无动态库）
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CRT_LINKAGE static)
```

### 3.4 CI 集成

```yaml
# .github/workflows/build.yml (示意)
- name: Setup vcpkg
  uses: lukka/run-vcpkg@v11
  with:
    vcpkgGitCommitId: ${{ env.VCPKG_BASELINE }}
- name: Build (host)
  run: |
    cmake -B build-host -S . \
      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=linux-amd64
    cmake --build build-host
- name: Build (device cross-compile)
  run: |
    cmake -B build-device -S . \
      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=linux-aarch64 \
      -DCMAKE_PREFIX_PATH=/opt/aarch64-linux-gnu
    cmake --build build-device
```

### 3.5 离线 / 受限环境支持

- **CI**：vcpkg 二进制缓存上传到内部 OSS（`VCPKG_BINARY_SOURCES`）
- **开发者**：首次 `vcpkg install` 后所有依赖本地缓存
- **设备产线烧录**：预下载 `vcpkg export` 产物（tar），离线解压后 `--no-network` 模式使用

## 4. 依赖锁定与升级

| 维度 | 策略 |
|------|------|
| **版本锁定** | `vcpkg.json` 的 `builtin-baseline` 字段锁 registry commit |
| **升级流程** | 升级 baseline → 本地 build → CI 全量回归 → 合并 PR |
| **安全公告** | 订阅 vcpkg + 各上游 advisory；CVE 出现后评估升级窗口 |
| **GPL 检测** | CI 步骤 `vcpkg depend-info` 后扫描 license；任何 GPL 依赖 fail build |
| **未维护检测** | 季度评审：超过 18 个月无 commit 的依赖标记弃用 |

## 5. 与现有工具链集成

| 工具 | 集成方式 |
|------|---------|
| **CMake** | toolchain 文件 + `find_package` 自动工作 |
| **ccache** | `VCPKG_CCACHE=ccache` 环境变量 |
| **clang-tidy** | `CMAKE_CXX_CLANG_TIDY=clang-tidy` |
| **sanitizer** | triplet 内设 `VCPKG_C_FLAGS=-fsanitize=address` |
| **IDE (CLion / VSCode)** | 自动识别 vcpkg toolchain；CMake 配置无需额外插件 |

## 6. 后果

- ✅ CMake 集成丝滑（toolchain 文件桥接）
- ✅ 嵌入式 cross-compile 友好（triplet）
- ✅ 二进制缓存加速 CI
- ✅ 依赖锁定 + GPL 检测保证合规
- ⚠️ 首次 `vcpkg install` 较慢（5-15 分钟，含编译）
- ⚠️ baseline 升级需谨慎（protobuf 等大库 ABI 变化）

## 7. 未来演进

- **v1.x**：评估 OSS 缓存 + 多机共享（减少 CI 构建时间）
- **v2.0+**：评估 Conan 2.x 作为替代（如 vcpkg 项目活跃度下降）

## 8. 引用

- [vcpkg 官方文档](https://learn.microsoft.com/vcpkg/)
- [vcpkg Triplet 规范](https://learn.microsoft.com/vcpkg/users/triplets)
- [CMake FetchContent 文档](https://cmake.org/cmake/help/latest/module/FetchContent.html)
- [CLAUDE.md §3.3 不允许引入的依赖](../../CLAUDE.md#3-不可违反的关键约束)
- [需求 §11.2 第 1 项 TBD](../01-requirements.md)
- [ADR-001 消息中间件选型](ADR-001-message-broker.md)
- [ADR-002 序列化格式选型](ADR-002-serialization.md)
- [ADR-008 可观测性方案](ADR-008-observability.md)