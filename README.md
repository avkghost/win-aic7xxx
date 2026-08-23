# Adaptec AIC7xxx SCSI Host Bus Adapter Driver

Cross-platform driver implementation for Adaptec AIC7xxx and AIC79xx SCSI HBA controllers, with native Windows 7–11 kernel driver builds using MinGW cross-compilation on Linux.

## Overview

The AIC7xxx and AIC79xx are high-performance SCSI HBA controllers:

- **AIC7xxx** — Fast/Ultra/Ultra2/Ultra160 SCSI (AIC-78xx family)
- **AIC79xx** — Ultra320 SCSI (AIC-7901/7902)

This repository maintains:
- **Windows drivers** — Native PE32+ kernel drivers (aic7xxx.sys, aic79xx.sys) for Windows 7–11 x86-64
- **Build toolchain** — Cross-compilation infrastructure via MinGW on Linux
- **Firmware** — Sequencer assembly sources and pre-built firmware headers
- **Shared abstractions** — Compatibility layer for StorPort/DDK APIs

## Quick Start

### Prerequisites

```bash
# Install MinGW cross-compiler and DDK headers
./toolchain/setup.sh

# Verify environment
./toolchain/setup.sh --check
```

### Building Drivers

```bash
# Build both drivers (full clean + rebuild)
./toolchain/build.sh

# Build single driver
./toolchain/build.sh aic7xxx

# Incremental rebuild
./toolchain/build.sh aic7xxx quick

# Verify drivers (PE headers, imports, subsystem)
./toolchain/build.sh verify
```

Output drivers: `win7-11/aic7xxx/aic7xxx.sys`, `win7-11/aic79xx/aic79xx.sys`

### Docker Build

```bash
docker build -t aic7xxx-build toolchain/
docker run --rm -v "$(pwd)":/src aic7xxx-build
```

Builds drivers in Ubuntu 24.04 with pinned dependencies.

## Directory Structure

```
.
├── toolchain/               # Build infrastructure
│   ├── setup.sh            # Environment setup
│   ├── build.sh            # Build wrapper (main entry point)
│   ├── Dockerfile          # Docker build container
│   └── README.md           # Detailed build documentation
│
├── win7-11/                # Windows drivers
│   ├── aic7xxx/            # AIC-78xx (Fast/Ultra/Ultra2/U160) driver
│   │   ├── Makefile
│   │   ├── aic7xxx.vcxproj # Visual Studio project
│   │   ├── aic7xxx.sys     # Built kernel driver (binary)
│   │   ├── aic7xxx.inf     # Driver installation profile
│   │   ├── *.c, *.h        # Driver sources
│   │   └── aic7xxx.seq     # Sequencer firmware source
│   │
│   ├── aic79xx/            # AIC-7901/7902 (U320) driver
│   │   ├── Makefile
│   │   ├── aic79xx.sys
│   │   ├── *.c, *.h
│   │   └── aic79xx.seq
│   │
│   └── common/             # Shared code and tools
│       ├── compat/         # DDK compatibility layer
│       │   ├── storport.h  # StorPort declarations MinGW DDK lacks
│       │   └── srb.h       # Legacy SCSI_REQUEST_BLOCK defs
│       ├── queue.h         # BSD queue macros
│       ├── scsi_message.h  # SCSI message constants
│       ├── ddk_*.def       # Import library specifications
│       └── aicasm/         # Sequencer assembler (host tool)
│           ├── aicasm_gram.y
│           ├── aicasm_scan.l
│           └── Makefile
│
├── .gitignore              # Build artifacts, generated files
└── README.md               # This file
```

## Building Firmware (aicasm)

The sequencer firmware is pre-built into headers (`aic7xxx_seq.h`, `aic79xx_seq.h`). Rebuild only if firmware sources (`.seq`, `.reg`) change:

```bash
cd win7-11/common/aicasm
make OUTDIR=.

# Then regenerate driver headers:
./aicasm -I ../../aic7xxx -I .. \
    -r ../../aic7xxx/aic7xxx_reg.h \
    -o ../../aic7xxx/aic7xxx_seq.h \
    ../../aic7xxx/aic7xxx.seq
```

Requires: `bison`, `flex`, `libdb-dev` (host tools).

## License

**Windows adaptation** (StorPort layer, Makefiles, DDK compatibility):
- MIT License © 2026 Andrei Kazialetski

**Linux-origin files** (core driver, PCI, sequencer):
- BSD/GPL dual license (original Adaptec/Justin T. Gibbs)
- See individual files and `doc/PORTING.md` for details

## References

- **Toolchain**: See `toolchain/README.md` for detailed build, Docker, and aicasm instructions
- **Windows Testing**: See `win7-11/aic7xxx/TESTING.md`
- **Porting Notes**: See `doc/PORTING.md` for Linux→Windows adaptation details

## Contact

Maintainer: Andrei Kazialetski (andrei.kazialetski@gmail.com)
