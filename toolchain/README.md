# toolchain — Build Environment

Cross-compiles the Windows StorPort miniport drivers on Linux using MinGW:

- `win7-11/aic79xx/` → **aic79xx.sys** (Ultra320, AIC-7901/7902)
- `win7-11/aic7xxx/` → **aic7xxx.sys** (Fast/Ultra/Ultra2/Ultra160, AIC-78xx)

Target: Windows 7–11, x86-64 native kernel driver (PE32+, subsystem 1).

## Setup

```bash
./toolchain/setup.sh          # install gcc-mingw-w64-x86-64 + verify environment
./toolchain/setup.sh --check  # check only
```

Requires DDK headers at `/usr/share/mingw-w64/include/ddk` (ships with the
Debian/Ubuntu `mingw-w64-x86-64-dev` package). The repo's own compat layer
(`win7-11/common/compat/storport.h`, `srb.h`) shadows the system headers for
the pieces MinGW's DDK copy lacks.

## Building

Use the wrapper from anywhere:

```bash
./toolchain/build.sh                    # full clean+build of BOTH drivers
./toolchain/build.sh all                # same thing
./toolchain/build.sh aic79xx            # one driver
./toolchain/build.sh aic7xxx quick      # incremental rebuild, no clean
./toolchain/build.sh verify             # build both + PE header/import checks
./toolchain/build.sh check              # environment check only

# Argument order is flexible:
./toolchain/build.sh quick aic79xx
./toolchain/build.sh aic7xxx verify
```

Or drive make directly per driver directory (`make clean && make`).

## Docker

```bash
docker build -t aic7xxx-build toolchain/
docker run --rm -v "$(pwd)":/src aic7xxx-build
```

Builds both drivers inside Ubuntu 24.04 with the pinned package set.

## PE verification

`build.sh verify` checks each `.sys`:

- Subsystem 1 (native) — kernel drivers must not be a console/GUI image
- `IMAGE_FILE_DLL` flag clear — a .sys is not a DLL despite being linked `-shared`
- `IMAGE_FILE_EXECUTABLE_IMAGE` set
- Import table lists only `NTOSKRNL.exe` and `storport.sys`

## Symbol stripping

By default the linker strips the COFF symbol/string table it would
otherwise append (`-s`, ~17–18 KB per driver). The Windows loader never
reads it; only host-side tools (`objdump -t`, WinDbg symbol resolution)
do. Build with symbols kept:

```bash
make DEBUG=1        # or: ./toolchain/build.sh aic79xx full && make DEBUG=1
```

`.reloc` (base relocations) and `.idata` (imports) are always retained —
both are required to load a kernel driver.

## Sequencer assembler (aicasm)

The driver `.sys` files use the pre-generated firmware headers
(`aic79xx_seq.h`, `aic7xxx_seq.h`, ...) shipped in each driver directory,
so building the drivers does **not** require aicasm. The assembler is
included so those headers can be regenerated from `.seq`/`.reg` sources
when the sequencer firmware changes.

`win7-11/common/aicasm/` is a verbatim copy of Linux
`drivers/scsi/aic7xxx/aicasm` (GPL-2.0, Justin T. Gibbs / Adaptec),
except `aicasm_insformat.h`, which is adapted for freestanding builds:
upstream's `#include <asm/byteorder.h>` is replaced by a definition of
`__LITTLE_ENDIAN` derived from compiler-predefined macros, so the same
file serves both this host tool and the Windows driver cores.

Build (host tool; needs `bison`, `flex`, `libdb-dev`):

```bash
cd win7-11/common/aicasm
make OUTDIR=.
```

On Ubuntu 24.04 upstream's `db_185.h` probe misses `/usr/include/db5.3/`;
pre-create the header if make complains:

```bash
echo '#include <db5.3/db_185.h>' > aicdb.h
```

Firmware sources live in each driver directory (`aic79xx.seq`/`aic79xx.reg`,
`aic7xxx.seq`/`aic7xxx.reg`) plus the shared `common/scsi_message.h`.
Regenerate from `win7-11/common/aicasm`:

```bash
# U320 (AIC-79xx)
./aicasm -I ../../aic79xx -I .. \
    -r ../../aic79xx/aic79xx_reg.h -o ../../aic79xx/aic79xx_seq.h \
    ../../aic79xx/aic79xx.seq

# Fast..U160 (AIC-78xx)
./aicasm -I ../../aic7xxx -I .. \
    -r ../../aic7xxx/aic7xxx_reg.h -o ../../aic7xxx/aic7xxx_seq.h \
    ../../aic7xxx/aic7xxx.seq
```

Add `-p <name>_reg_print.c -i <driver>_osm.h` for the pretty-printed
register dump files (`*_reg_print.c`) the Linux build optionally emits.
Generated files (`aicasm`, `aicdb.h`, parser outputs) are gitignored.

Validated: this assembler produces byte-identical `_seq.h`/`_reg.h`
output to the one built from the pristine upstream tree, for both
families, and the shipped headers match its output except for the
local include guards and copyright banners added for the Windows
build.

## Layout notes

Shared build inputs live in `win7-11/common/` and are referenced by both
driver Makefiles via `-I../common -I../common/compat`:

| File | Purpose |
|---|---|
| `common/compat/storport.h` | StorPort declarations MinGW DDK lacks |
| `common/compat/srb.h` | Legacy SCSI_REQUEST_BLOCK definitions |
| `common/ddk_storport.def` | Import lib spec for storport.sys |
| `common/ddk_ntoskrnl.def` | Import lib spec for ntoskrnl.exe |
| `common/queue.h` | BSD queue macros (SLIST/TAILQ/LIST) |
| `common/scsi_message.h` | SCSI message constants (used by .seq firmware sources) |
| `common/aicasm/` | Sequencer assembler + instruction formats (shared by both cores) |
