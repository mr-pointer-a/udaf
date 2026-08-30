#!/usr/bin/env bash
# lib_install.sh - install_deps.sh 的公共函数库

# ANSI 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info()    { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_success() { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

print_header() {
    echo ""
    echo "=========================================="
    echo "  $*"
    echo "=========================================="
}

apt_update() {
    log_info "更新 apt 索引..."
    sudo apt-get update -qq
}

install_pkgs() {
    local pkgs=("$@")
    log_info "安装: ${pkgs[*]}"
    sudo apt-get install -y -qq --no-install-recommends "${pkgs[@]}"
}

ask_yn() {
    local prompt="$1"
    local default="${2:-n}"
    local answer

    if [[ "${default}" == "y" ]]; then
        prompt="${prompt} [Y/n]"
    else
        prompt="${prompt} [y/N]"
    fi

    read -r -p "${prompt}: " answer
    answer="${answer:-${default}}"

    case "${answer}" in
        y|Y|yes|YES) return 0 ;;
        *) return 1 ;;
    esac
}

# 检查非 root 用户是否可写 cache 目录
check_sudo() {
    if ! command -v sudo > /dev/null 2>&1; then
        log_error "需要 sudo 但未安装"
        exit 1
    fi
}