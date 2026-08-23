# AIC7xxx (U160) StorPort Miniport Driver - MinGW Cross-Build
# Builds aic7xxx.sys on Linux using MinGW cross-compiler
#
# Usage:
#   make          - full build (compile + link -> aic7xxx.sys)
#   make syntax   - syntax check only (fast, no .o files)
#   make compile  - compile .o files only
#   make link     - link .o files into aic7xxx.sys
#   make clean    - remove build artifacts
#
# Copyright (c) 2026 Andrei Kazialetski. MIT License.

CC = x86_64-w64-mingw32-gcc
OBJDUMP = x86_64-w64-mingw32-objdump
DLLTOOL = x86_64-w64-mingw32-dlltool
WINDRES = x86_64-w64-mingw32-windres

# Kernel-mode flags: no CRT, no stack protection, freestanding
CFLAGS = -std=gnu99 -ffreestanding -fno-builtin -fno-stack-protector -w
CFLAGS += -D__GNUWIN32__ -D_AMD64_=1 -D__x86_64__
LDFLAGS = -nostdlib -Wl,--no-undefined -shared
LDFLAGS += -Wl,--entry,DriverEntry
LDFLAGS += -Wl,--subsystem,native
LDFLAGS += -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,0
LDFLAGS += -Wl,--kill-at
# Strip the COFF symbol/string table the linker appends (~17KB); nothing
# reads it at runtime. Build with DEBUG=1 to keep it for WinDbg/objdump.
# DEBUG=1 also enables per-I/O trace logging (gated by AIC_DBGPRINT macro).
ifeq ($(DEBUG),)
LDFLAGS += -s
else
CFLAGS += -DAIC_DBGPRINT_ENABLED
endif

# Include paths: compat/ before DDK to resolve SCSI_WMI_REQUEST_BLOCK bug
# common/ provides compat/, queue.h, and def files shared by both drivers
INCLUDES = -I. -I../common -I../common/compat -I/usr/share/mingw-w64/include/ddk

SOURCES = aic7xxx_storport.c aic7xxx_core.c aic7xxx_pci.c aic7xxx_93cx6.c
OBJECTS = $(SOURCES:.c=.o)
RESOBJ = aic7xxx_res.o
TARGET = aic7xxx.sys

.PHONY: all syntax compile link clean

all: $(TARGET)

# Syntax check only (fast, no .o files produced)
syntax:
	@echo "=== Syntax checking all sources ==="
	@for f in $(SOURCES); do \
		echo "  $$f ..."; \
		$(CC) $(CFLAGS) $(INCLUDES) -fsyntax-only $$f 2>&1 | \
			grep 'error:' | head -5; \
	done
	@echo "=== Done ==="

# Compile to .o files
compile: $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Version resource
$(RESOBJ): aic7xxx.rc aic7xxx_win.h
	$(WINDRES) -I. -I../common $< -O coff -o $@

# Import libraries for kernel-mode DDK symbols
libstorport.a: ../common/ddk_storport.def
	$(DLLTOOL) -d ../common/ddk_storport.def -l libstorport.a -k

libntoskrnl.a: ../common/ddk_ntoskrnl.def
	$(DLLTOOL) -d ../common/ddk_ntoskrnl.def -l libntoskrnl.a -k

# Link .o files into .sys driver binary
link: $(TARGET)

$(TARGET): $(OBJECTS) $(RESOBJ) libstorport.a libntoskrnl.a
	$(CC) $(LDFLAGS) $(OBJECTS) $(RESOBJ) -L. -lstorport -lntoskrnl -o $@
	@# Patch PE header: clear IMAGE_FILE_DLL bit so file(1) reports
	@# "executable (native)" not "executable (DLL) (native)"
	@python3 -c "import struct; f=open('$@','r+b'); f.seek(0x3C); pe=struct.unpack('<I',f.read(4))[0]; f.seek(pe+22); c=struct.unpack('<H',f.read(2))[0]; f.seek(pe+22); f.write(struct.pack('<H',c&~0x2000)); f.close()" 2>/dev/null || true
	@echo ""
	@echo "=== Build complete: $(TARGET) ==="
	@ls -la $(TARGET)
	@file $(TARGET)
	@echo ""
	@echo "Imports:"
	@$(OBJDUMP) -x $@ | grep -A 20 'Import Table' | grep 'DLL Name\|Member-Name'

clean:
	rm -f $(OBJECTS) $(RESOBJ) $(TARGET) libstorport.a libntoskrnl.a ddk_exports.a
