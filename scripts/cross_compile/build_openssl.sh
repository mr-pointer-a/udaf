#!/usr/bin/env bash
# build_openssl.sh - 交叉编译 OpenSSL 3.0.x（TLS 1.3 + HMAC + AES-GCM + HKDF）
#
# 来源: https://www.openssl.org/source/
# 用途: UDAF 加密层（PSK/PKI/TLS）
# 依赖: 无（仅 libc）
# 产物: ${INSTALL_PREFIX}/lib/libssl.a + ${INSTALL_PREFIX}/lib/libcrypto.a

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib_install.sh
source "${SCRIPT_DIR}/../lib_install.sh"

: "${LIB_VERSION:?LIB_VERSION 未设置}"
: "${TARGET_ARCH:?TARGET_ARCH 未设置}"
: "${INSTALL_PREFIX:?INSTALL_PREFIX 未设置}"
: "${WORK_ROOT:?WORK_ROOT 未设置}"
log_info "OpenSSL ${LIB_VERSION} for ${TARGET_ARCH} → ${INSTALL_PREFIX}"

# 1. 目标架构映射（OpenSSL 的 perl 配置字符串）
case "${TARGET_ARCH}" in
    aarch64)   OPENSSL_TARGET="linux-aarch64"      ;;
    armv7)     OPENSSL_TARGET="linux-armv4"        ;;
    riscv64)   OPENSSL_TARGET="linux-generic64"    ;;  # 需验证
    *)
        log_error "OpenSSL 不支持的目标: ${TARGET_ARCH}"
        exit 2
        ;;
esac

SRC_DIR="${WORK_ROOT}/openssl-${LIB_VERSION}"
ARCHIVE="openssl-${LIB_VERSION}.tar.gz"
URL="https://www.openssl.org/source/openssl-${LIB_VERSION}.tar.gz"

# 2. 下载
cd "${WORK_ROOT}"
if [[ ! -f "${ARCHIVE}" ]]; then
    log_info "下载 ${URL}"
    curl -fL --retry 3 -o "${ARCHIVE}" "${URL}"
fi

# 3. 解压
if [[ ! -d "${SRC_DIR}" ]]; then
    log_info "解压 ${ARCHIVE}"
    tar xzf "${ARCHIVE}"
fi

cd "${SRC_DIR}"

# 4. 配置（perl Configure）
mkdir -p "${INSTALL_PREFIX}"

./Configure \
    "${OPENSSL_TARGET}" \
    --prefix="${INSTALL_PREFIX}" \
    --libdir=lib \
    --release \
    no-shared \
    no-tests \
    no-ui-console \
    no-asm \
    enable-static-engine \
    -DOPENSSL_NO_SECURE_MEMORY

# 5. 编译
make -j"$(nproc)"

# 6. 安装
make install_sw install_ssldirs  # 仅安装库与目录，不安装文档

log_success "OpenSSL 已安装:"
log_success "  libssl.a     → ${INSTALL_PREFIX}/lib/libssl.a"
log_success "  libcrypto.a  → ${INSTALL_PREFIX}/lib/libcrypto.a"