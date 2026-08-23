#!/bin/bash
# Copyright (c) 2026 Andrei Kazialetski. MIT License.
#
# toolchain/setup.sh — Install MinGW cross-compiler, DDK headers and the
# aicasm prerequisites (bison, flex, libdb) needed to build both drivers
#
# Usage:
#   ./setup.sh              Install everything
#   ./setup.sh --check      Verify environment without installing
#
# Supported: Ubuntu 20.04+, Debian 11+
#
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

ok()   { echo -e "${GREEN}[OK]${NC}   $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; }

TOOLCHAIN_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$TOOLCHAIN_DIR/.." && pwd)"
BUILD_DIR_AIC7XXX="$REPO_ROOT/win7-11/aic7xxx"
BUILD_DIR_AIC79XX="$REPO_ROOT/win7-11/aic79xx"
COMMON_DIR="$REPO_ROOT/win7-11/common"
DDK_HEADERS_DIR="/usr/share/mingw-w64/include/ddk"

# ---------------------------------------------------------------------------
# Dependency list
# ---------------------------------------------------------------------------
PACKAGES=(
    gcc-mingw-w64-x86-64
    binutils-mingw-w64-x86-64
    mingw-w64-x86-64-dev
    python3
    bison
    flex
    libdb-dev
)

# ---------------------------------------------------------------------------
# Check
# ---------------------------------------------------------------------------
check_env() {
    local rc=0

    echo "=== Checking build environment ==="
    echo ""

    # gcc
    if command -v x86_64-w64-mingw32-gcc &>/dev/null; then
        local ver
        ver=$(x86_64-w64-mingw32-gcc --version | head -1)
        ok "x86_64-w64-mingw32-gcc: $ver"
    else
        fail "x86_64-w64-mingw32-gcc not found"
        rc=1
    fi

    # dlltool
    if command -v x86_64-w64-mingw32-dlltool &>/dev/null; then
        ok "x86_64-w64-mingw32-dlltool: found"
    else
        fail "x86_64-w64-mingw32-dlltool not found"
        rc=1
    fi

    # objdump
    if command -v x86_64-w64-mingw32-objdump &>/dev/null; then
        ok "x86_64-w64-mingw32-objdump: found"
    else
        fail "x86_64-w64-mingw32-objdump not found"
        rc=1
    fi

    # python3
    if command -v python3 &>/dev/null; then
        ok "python3: $(python3 --version 2>&1)"
    else
        fail "python3 not found"
        rc=1
    fi

    # aicasm (sequencer assembler) toolchain
    if command -v bison &>/dev/null && command -v flex &>/dev/null; then
        ok "bison: $(bison --version | head -1), flex: $(flex --version | head -1)"
    else
        fail "bison/flex not found (needed for aicasm)"
        rc=1
    fi
    if [ -f "/usr/include/db_185.h" ] || [ -f "/usr/include/db5.3/db_185.h" ] \
       || [ -f "/usr/include/db4/db_185.h" ]; then
        ok "Berkeley DB 1.85 compat header: found"
    else
        fail "db_185.h not found (install libdb-dev, needed for aicasm)"
        rc=1
    fi

    # DDK headers
    if [ -f "$DDK_HEADERS_DIR/ntddk.h" ] && [ -f "$DDK_HEADERS_DIR/storport.h" ]; then
        local count
        count=$(ls "$DDK_HEADERS_DIR"/*.h 2>/dev/null | wc -l)
        ok "DDK headers: $count files in $DDK_HEADERS_DIR"
    else
        fail "DDK headers not found in $DDK_HEADERS_DIR"
        rc=1
    fi

    # compat headers (shared, in common/)
    if [ -f "$COMMON_DIR/compat/srb.h" ] && [ -f "$COMMON_DIR/compat/storport.h" ]; then
        ok "compat headers: srb.h, storport.h ($COMMON_DIR/compat)"
    else
        fail "compat headers missing in $COMMON_DIR/compat/"
        rc=1
    fi

    # driver Makefiles
    for d in "$BUILD_DIR_AIC79XX" "$BUILD_DIR_AIC7XXX"; do
        if [ -f "$d/Makefile" ]; then
            ok "Makefile: found ($d)"
        else
            fail "Makefile missing in $d/"
            rc=1
        fi
    done

    echo ""
    if [ $rc -eq 0 ]; then
        ok "Environment ready. Build: cd $BUILD_DIR_AIC79XX && make  |  cd $BUILD_DIR_AIC7XXX && make"
    else
        fail "Missing dependencies. Run: $0 (without --check)"
    fi
    return $rc
}

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install_toolchain() {
    echo "=== Installing MinGW cross-compilation toolchain ==="
    echo ""

    # Check for root/sudo
    if [ "$(id -u)" -eq 0 ]; then
        SUDO=""
    elif command -v sudo &>/dev/null; then
        SUDO="sudo"
    else
        fail "Need root or sudo to install packages"
        exit 1
    fi

    # Detect package manager
    if command -v apt-get &>/dev/null; then
        PKG_MGR="apt"
    elif command -v dnf &>/dev/null; then
        PKG_MGR="dnf"
    elif command -v pacman &>/dev/null; then
        PKG_MGR="pacman"
    else
        fail "Unsupported package manager. Install manually:"
        echo "  ${PACKAGES[*]}"
        exit 1
    fi

    echo "Detected package manager: $PKG_MGR"
    echo "Packages to install: ${PACKAGES[*]}"
    echo ""

    case $PKG_MGR in
        apt)
            $SUDO apt-get update -qq
            $SUDO apt-get install -y -qq "${PACKAGES[@]}"
            ;;
        dnf)
            # Fedora/CentOS names
            $SUDO dnf install -y \
                mingw64-gcc \
                mingw64-binutils \
                mingw64-winpthreads-static \
                python3
            ;;
        pacman)
            # Arch names
            $SUDO pacman -S --noconfirm \
                mingw-w64-gcc \
                mingw-w64-binutils \
                python
            ;;
    esac

    echo ""

    # Verify
    check_env
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
case "${1:-}" in
    --check|-c)
        check_env
        ;;
    --help|-h)
        echo "Usage: $0 [--check]"
        echo ""
        echo "  (no args)  Install MinGW cross-compiler + DDK headers"
        echo "  --check    Verify environment without installing"
        ;;
    *)
        install_toolchain
        ;;
esac
