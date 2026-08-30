# UDAF 第三方依赖清单

> **版本**：v0.1
> **日期**：2026-08-28
> **依据**：[`docs/05-test-plan.md §5.11.1`](../docs/05-test-plan.md) + [`docs/adr/ADR-009-dependency-management.md`](../docs/adr/ADR-009-dependency-management.md)

本文件区分**运行时依赖**（程序运行时 dlopen / 静态链接入最终二进制）、**编译时依赖**（构建时需要但运行时不需要）、**构建工具**（编译器、静态分析、覆盖率等）。

---

## 一. 运行时依赖（最终二进制需要）

设备端、主机端二进制发布时需要，部署到目标机器必须安装或静态链接。

### 1.1 C++ 标准库

| 库 | 版本 | 说明 |
|---|---|---|
| libstdc++ | GCC 13+ | C++20 标准库；glibcxx 28+ ABI |

### 1.2 第三方动态/静态库

| 库 | apt 包 | 版本 | 链接方式 | 设备端必需 | 主机端必需 | 用途 |
|---|---|---|---|---|---|---|
| **libzmq** | `libzmq3-dev` | ≥ 4.3 | 静态（嵌入式）/ 动态（主机） | ✅ | ✅ | ZMQ 消息中间件（inproc/ipc/tcp 三层传输） |
| **OpenSSL** | `libssl-dev` | ≥ 3.0 | 静态（嵌入式）/ 动态（主机） | ✅ | ✅ | TLS 1.3 + HMAC-SHA256 + AES-GCM + HKDF |
| **yaml-cpp** | `libyaml-cpp-dev` | ≥ 0.8.0 | 静态 | ✅ | ✅ | 拓扑 / 配置文件解析 |
| **spdlog** | `libspdlog-dev` | ≥ 1.12 | 静态（嵌入式）/ 动态（主机） | ✅ | ✅ | 日志框架 |
| **fmt** | `libfmt-dev` | ≥ 9.0 | header-only（fmt 9.1+） 或 静态 | ✅ | ✅ | 格式化输出（spdlog 后端） |
| **protobuf-lite** | `libprotobuf-dev` | v3.x lite runtime | 静态 | ✅ | ✅ | 节点消息契约序列化（ADR-002） |

> **链接策略**（[ADR-009 §3.4](../docs/adr/ADR-009-dependency-management.md)）：
> - 主机端：动态链接，减小二进制体积；部署时通过 `dpkg` 或 `udaf` deb 包安装
> - 设备端：强制 `VCPKG_LIBRARY_LINKAGE=static` + `VCPKG_CRT_LINKAGE=static` + `cmake -DBUILD_SHARED_LIBS=OFF`

### 1.3 系统基础库（默认链接）

| 库 | 用途 |
|---|---|
| glibc / musl | libc |
| libpthread | 线程 |
| libdl | 动态加载（动态构建） |
| librt | 时钟 / timerfd |
| libresolv | DNS 解析 |

### 1.4 设备端额外系统服务

| 服务 | 用途 |
|---|---|
| systemd 或 init | 进程守护 |
| udev | 设备节点 |
| avahi-daemon（可选） | mDNS 发现 |

---

## 二. 编译时依赖（构建机器需要，运行时不链接）

构建机器（开发机 / CI runner）需要安装，但运行时二进制不直接依赖这些头文件 / 工具。

| 库/工具 | apt 包 | 版本 | 用途 | 链接到二进制 |
|---|---|---|---|---|
| **GoogleTest** | `libgtest-dev` | ≥ 1.14 | 单元测试 | ❌ 仅测试可执行文件 |
| **Google Mock** | `libgmock-dev` | ≥ 1.14 | mock 对象 | ❌ 仅测试 |
| **Google Benchmark** | `libbenchmark-dev` | ≥ 1.8 | 性能基准（29 项） | ❌ 仅 benchmark 可执行文件 |
| **protobuf-compiler (protoc)** | `protobuf-compiler` | v3.x | 编译 `.proto` → `.pb.cc/.pb.h` | ❌ 仅生成阶段 |

> **注意**：GoogleTest / Google Benchmark / protoc **不会**链接到 `libudaf_core.so` 等运行时库；它们仅出现在 `test_*` / `udaf_bench_*` 可执行文件中。

---

## 三. 仅编译时工具（开发机 / CI runner 需要）

构建过程中使用，但不会出现在最终二进制。

### 3.1 编译器与构建工具

| 工具 | apt 包 | 版本 | 用途 |
|---|---|---|---|
| **GCC** | `g++` | ≥ 13（≥ 12 可用，推荐 13） | C++20 主编译器 |
| **Clang** | `clang` | ≥ 15（推荐 18） | libFuzzer 后端 + 备用编译 |
| **CMake** | `cmake` | ≥ 3.20（推荐 3.28） | 构建系统 |
| **ccache** | `ccache` | ≥ 4.0 | 编译缓存（CI 必装） |

### 3.2 静态分析

| 工具 | apt 包 | 版本 | 用途 |
|---|---|---|---|
| **clang-tidy** | `clang-tidy` | 与 clang 同版本 | 静态分析（`static-analysis` job） |
| **cppcheck** | `cppcheck` | ≥ 2.10 | 补充静态分析 |

### 3.3 测试与覆盖率

| 工具 | apt 包 | 版本 | 用途 |
|---|---|---|---|
| **lcov** | `lcov` | ≥ 1.16 | 覆盖率收集 + HTML 报告 |
| **genhtml** | lcov 自带 | - | 生成 HTML 报告 |
| **valgrind**（可选） | `valgrind` | ≥ 3.20 | 内存错误检测（备选 ASan） |

### 3.4 模糊测试

| 工具 | 来源 | 用途 |
|---|---|---|
| **libFuzzer** | Clang 内置（≥ 15） | 模糊测试引擎 |
| **SanitizerCoverage** | Clang 内置 | libFuzzer 配套 |
| **QEMU user-mode** | `qemu-user-static` | aarch64 二进制本地模拟（`fuzz` job） |

### 3.5 交叉编译工具链（嵌入式设备端）

| 工具 | 来源 | 目标 |
|---|---|---|
| **aarch64-linux-gnu-g++** | `gcc-aarch64-linux-gnu` | ARM64 设备 |
| **arm-linux-gnueabihf-g++** | `gcc-arm-linux-gnueabihf` | ARMv7 设备 |
| **riscv64-linux-gnu-g++** | `gcc-riscv64-linux-gnu` | RISC-V 设备 |

---

## 四. 部署工具（生成 deb 包时需要）

| 工具 | apt 包 | 用途 |
|---|---|---|
| **dpkg-dev** | `dpkg-dev` | 打包元数据 |
| **debhelper** | `debhelper` | Debian 打包助手 |
| **fakeroot** | `fakeroot` | 非 root 打包 |
| **lintian** | `lintian`（可选） | deb 包质量检查 |

---

## 五. 一行安装命令汇总

### 5.1 完整开发机（apt-based）

```bash
sudo apt-get update && sudo apt-get install -y \
  # 运行时依赖（构建时也通过 -dev 提供）
  libzmq3-dev libssl-dev libyaml-cpp-dev libfmt-dev libspdlog-dev libprotobuf-dev \
  # 编译时测试/基准工具
  libgtest-dev libgmock-dev libbenchmark-dev protobuf-compiler \
  # 编译工具
  cmake g++ clang ccache \
  # 静态分析
  clang-tidy cppcheck \
  # 覆盖率
  lcov \
  # 模糊测试（可选）
  qemu-user-static \
  # 交叉编译工具链（可选）
  gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf gcc-riscv64-linux-gnu \
  # 打包工具
  dpkg-dev debhelper fakeroot
```

### 5.2 仅运行时（部署到设备端）

```bash
# Ubuntu/Debian 设备端最小依赖
sudo apt-get install -y libzmq5 libssl3 libfmt9 libspdlog1.12 libyaml-cpp0.8 libprotobuf-lite32t64
```

> 嵌入式 buildroot/busybox 系统无 apt：请使用 `scripts/cross_compile_deps.sh` 手动交叉编译。

---

## 六. 嵌入式交叉编译脚本（buildroot/busybox 系统）

### 6.1 适用场景

- 目标系统无包管理器（buildroot / busybox / OpenWrt / Yocto 精简版）
- 必须从源码交叉编译所有运行时依赖
- 静态链接到 `libudaf.a`

### 6.2 脚本入口

- `scripts/cross_compile_deps.sh` — 总入口，调用各子脚本
- `scripts/cross_compile/<lib>.sh` — 单个库的交叉编译脚本（每个库一个）
- `cmake/toolchains/linux-aarch64-static.cmake` — aarch64 静态链接 toolchain
- `cmake/toolchains/linux-armv7-static.cmake` — armv7 静态链接 toolchain

### 6.3 待交叉编译的库清单

按用户约束，**仅运行时依赖**需交叉编译（编译时工具如 googletest / benchmark / protoc 在主机端构建设备端 binary 时已用主机工具链完成，无需交叉）：

| # | 库 | 版本 | 用途 | 源码大小 | 静态库目标 |
|---|---|---|---|---|---|
| 1 | **libzmq** | 4.3.5 | ZMQ | ~3 MB | `libzmq.a` |
| 2 | **OpenSSL** | 3.0.13 | TLS/加密 | ~15 MB | `libssl.a` + `libcrypto.a` |
| 3 | **yaml-cpp** | 0.8.0 | 配置解析 | ~200 KB | `libyaml-cpp.a` |
| 4 | **fmt** | 9.1.0 | 格式化（header-only 可选静态） | ~1 MB | `libfmt.a`（可选） |
| 5 | **spdlog** | 1.12.0 | 日志 | ~500 KB | `libspdlog.a` |
| 6 | **protobuf** | 3.21.12 lite | 序列化 | ~5 MB | `libprotobuf-lite.a` |

> **Googletest / Google Benchmark / protoc** 不在交叉编译清单中（它们是开发机 / CI runner 的编译时工具）。

### 6.4 总编译脚本接口

```bash
# 用法：scripts/cross_compile_deps.sh <target_arch> [<install_prefix>]
#   target_arch: aarch64 | armv7 | riscv64
#   install_prefix: 默认 /opt/udaf/cross/<arch>

scripts/cross_compile_deps.sh aarch64
scripts/cross_compile_deps.sh aarch64 /opt/udaf/cross/aarch64-static
```

### 6.5 CMake 引用

```bash
# 使用交叉编译 toolchain
cmake -B build-device-aarch64 -S . \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64-static.cmake \
  -DCMAKE_INSTALL_PREFIX=/opt/udaf/cross/aarch64-static \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DVCPKG_LIBRARY_LINKAGE=static
```

---

## 七. 版本锁定

CI 通过 `apt-cache madison` 在每次构建前校验版本：

| 库 | 最低版本 | 推荐版本 |
|---|---|---|
| libzmq | 4.3.0 | 4.3.5 |
| OpenSSL | 3.0.0 | 3.0.13 |
| yaml-cpp | 0.8.0 | 0.8.0 |
| fmt | 9.0.0 | 9.1.0 |
| spdlog | 1.10.0 | 1.12.0 |
| protobuf | 3.21.0 | 3.21.12 |

---

## 八. 已知兼容性风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| GCC 14 + lcov 2.0 不兼容 | 覆盖率采集失败 | 锁定 GCC 13 用于 coverage job，或换 clang + llvm-cov |
| OpenSSL 3.0 → 3.1 ABI 破坏 | 升级后二进制崩溃 | 锁定 3.0.x；升级前 CI 全量回归 |
| protobuf 3.20 → 3.21 wire format 变更 | 反序列化失败 | 锁定 3.21.x；wire format 稳定 |
| libzmq 4.3 → 4.4 API 微调 | 编译失败 | 锁定 4.3.x |

---

## 九. 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-08-28 | 初稿；区分运行时 / 编译时 / 工具；新增嵌入式交叉编译脚本入口 |