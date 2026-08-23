#!/bin/bash
# Copyright (c) 2026 Andrei Kazialetski. MIT License.
#
# toolchain/build.sh — Build aic7xxx.sys and/or aic79xx.sys (wrapper)
#
# Usage:
#   ./build.sh [driver]              Full build (clean + compile + link)
#   ./build.sh [driver] quick        Build without cleaning (faster iteration)
#   ./build.sh [driver] docker       Build inside Docker container
#   ./build.sh [driver] check        Verify build environment
#   ./build.sh [driver] verify       Build + verify PE properties
#   ./build.sh all                   Build both drivers
#
# Drivers: aic7xxx (Fast/Ultra/Ultra2/U160), aic79xx (Ultra320). Default: all.
#
set -euo pipefail

TOOLCHAIN_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$TOOLCHAIN_DIR/.." && pwd)"
WIN_DIR="$REPO_ROOT/win7-11"

DRIVER="all"
ACTION="full"

if [ $# -ge 1 ]; then
    case "$1" in
        aic7xxx|aic79xx|all)
            DRIVER="$1"
            # Optional action as second argument
            if [ $# -ge 2 ]; then
                case "$2" in
                    full|quick|docker|check|verify) ACTION="$2";;
                    *) echo "Unknown action: $2"; exit 1;;
                esac
            fi
            ;;
        full|quick|docker|check|verify)
            ACTION="$1"
            # Optional driver as second argument
            if [ $# -ge 2 ]; then
                case "$2" in
                    aic7xxx|aic79xx|all) DRIVER="$2";;
                    *) echo "Unknown driver: $2"; exit 1;;
                esac
            fi
            ;;
        *)
            echo "Usage: $0 [driver] [full|quick|docker|check|verify]"
            echo "  driver: aic7xxx | aic79xx | all (default)"
            exit 1
            ;;
    esac
fi

build_one() {
    local drv="$1"
    local dir="$WIN_DIR/$drv"
    if [ ! -f "$dir/Makefile" ]; then
        echo "ERROR: no Makefile in $dir"
        exit 1
    fi

    cd "$dir"

    case "$ACTION" in
        full)
            echo "=== Full build: $drv ==="
            make clean && make ${DEBUG:+DEBUG=1}
            echo ""
            echo "Output: $dir/$drv.sys"
            ls -la "$drv.sys"
            file "$drv.sys"
            ;;

        quick)
            echo "=== Quick build (no clean): $drv ==="
            make ${DEBUG:+DEBUG=1}
            echo ""
            echo "Output: $dir/$drv.sys"
            ls -la "$drv.sys"
            file "$drv.sys"
            ;;

        docker)
            echo "=== Docker build: $drv ==="
            docker build -t aic7xxx-build "$TOOLCHAIN_DIR"
            docker run --rm -v "$REPO_ROOT":/src -w "/src/win7-11/$drv" aic7xxx-build make clean && make ${DEBUG:+DEBUG=1}
            echo ""
            echo "Output: $dir/$drv.sys"
            ls -la "$drv.sys"
            file "$drv.sys"
            ;;

        check)
            "$TOOLCHAIN_DIR/setup.sh" --check
            ;;

        verify)
            echo "=== Build + Verify: $drv ==="
            make clean && make ${DEBUG:+DEBUG=1}
            echo ""
            echo "--- PE header check ---"
            python3 -c "
import struct, sys
f = open('$drv.sys', 'rb')
f.seek(0x3C)
pe = struct.unpack('<I', f.read(4))[0]
f.seek(pe + 22)
chars = struct.unpack('<H', f.read(2))[0]
# PE32+ Subsystem: PE sig(4) + COFF(20) + OptionalHeader offset(68) = pe + 92
f.seek(pe + 92)
subsys = struct.unpack('<H', f.read(2))[0]
f.seek(pe + 24)
entry = struct.unpack('<I', f.read(4))[0]
f.close()
dll = bool(chars & 0x2000)
exec_ = bool(chars & 0x0002)
print(f'  PE base:          0x{pe:x}')
print(f'  Entry point:      0x{entry:x}')
print(f'  Subsystem:        {subsys} (1=native)')

if subsys != 1:
    print(f'  WARNING: subsystem should be 1 (native), got {subsys}')
    sys.exit(1)
if dll:
    print(f'  WARNING: IMAGE_FILE_DLL flag set — should be clear')
    sys.exit(1)
if not exec_:
    print(f'  WARNING: IMAGE_FILE_EXECUTABLE_IMAGE not set')
    sys.exit(1)
print('  All checks passed')
"
            echo ""
            echo "--- Import check ---"
            x86_64-w64-mingw32-objdump -p "$drv.sys" | grep -A 20 'Import Table' | grep 'DLL Name\|Member-Name'
            echo ""
            echo "--- Summary ---"
            file "$drv.sys"
            ls -la "$drv.sys"
            ;;
    esac
}

if [ "$ACTION" = "check" ]; then
    "$TOOLCHAIN_DIR/setup.sh" --check
    exit 0
fi

if [ "$DRIVER" = "all" ]; then
    build_one aic79xx
    echo ""
    build_one aic7xxx
else
    build_one "$DRIVER"
fi
