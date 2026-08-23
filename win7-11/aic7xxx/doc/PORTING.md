# AIC-7xxx Windows StorPort Miniport Driver

## Origin

This driver is a **Windows StorPort miniport adaptation** of the Linux
`aic7xxx` SCSI host adapter driver originally written by:

- **Justin T. Gibbs** — core driver architecture and sequencer firmware
- **Adaptec Inc.** — hardware documentation and maintenance

The Linux source was distributed under a BSD/GPL dual license. The original
BSD copyright and license terms are preserved in all Linux-origin source
files (`aic7xxx_core.c`, `aic7xxx.h`, `aic7xxx_pci.c`, `aic7xxx_pci.h`,
`aic7xxx_seq.h`, `aic7xxx_reg.h`, `aic7xxx_93cx6.c`, `aic7xxx_93cx6.h`,
`../common/aicasm/aicasm_insformat.h`).

The Windows adaptation layer (OSM — OS Module) and StorPort glue code are
licensed under the MIT License, copyright (c) 2026 Andrei Kazialetski.

## What This Driver Is

A Windows kernel-mode SCSI miniport driver for Adaptec AIC-78xx family
SCSI host bus adapters (Fast / Ultra / Ultra2 LVD / Ultra160 LVD), built
on the StorPort I/O model:

- **AHA-2902/2910/2930C/CU** — Fast SCSI, AIC-785x/786x
- **AHA-2940/3940/398X** — Fast Wide SCSI PCI, AIC-787x
- **AHA-2940U/UW, 3940U/UW** — Ultra SCSI, AIC-788x
- **AHA-2940UW Dual, 3940AU** — Ultra Wide dual channel, AIC-7895
- **AHA-2940U2W/B, 2930U2, 3950U2B/D** — Ultra2 LVD, AIC-7890/7896
- **AHA-29160/N/C/B, 19160B, 2915-30LP** — Ultra160 LVD, AIC-7892
- **AHA-3960D** — Ultra160 LVD dual channel, AIC-7899

Target OS: Windows 7 through Windows 11 (x86-64).

Note: the sibling `aic79xx` driver (Ultra320, AIC-7901/7902) is a separate
port sharing the same architecture. See `../../aic79xx/doc/`.

## What Changed (Linux → Windows)

The adaptation follows a **two-layer design**. The Linux core
(`aic7xxx_core.c`, ~7,900 lines) is compiled **unmodified**. All OS
differences are absorbed by the OSM header (`aic7xxx_osm.h`) and StorPort
glue file (`aic7xxx_storport.c`).

### Preserved from Linux

- Sequencer firmware (downloaded to AIC-78xx on-chip processor)
- PCI configuration parsing and chip detection
- SEEPROM contents parsing (`aic7xxx_93cx6` serial EEPROM access)
- SCSI bus negotiation, tag queuing, error recovery
- Core data structures (`ahc_softc`, `scb`, `hardware_scb`)
- All chip register definitions and sequencer opcodes

### Replaced for Windows

| Linux concept | Windows (StorPort) equivalent |
|---|---|
| `struct scsi_cmnd` / `struct scsi_device` | Shadow structs embedded in `scb_platform_data` |
| `scsi_attach()` / `scsi_detach()` | `HwFindAdapter` / `HwAdapterControl` |
| `scsi_done()` | `StorPortNotification(NextRequest)` + `StorPortNotification(RequestComplete)` |
| `scsi_report_bus_status()` | `StorPortNotification(ResetBus)` |
| `ahc_platform_freeze_devq()` | `StorPortPauseDevice()` |
| `ahc_platform_set_tags()` | No-op (StorPort manages tags) |
| `ahc_freeze_simq()` / `ahc_release_simq()` | No-ops (single-SRB model) |
| DMA alloc (contigmalloc) | `StorPortGetUncachedExtension` bump allocator |
| MMIO (bus_space) | `READ_REGISTER_UCHAR` / `WRITE_REGISTER_UCHAR` |
| PCI config (pci_read_config) | `StorPortGetBusData` / `StorPortSetBusDataByOffset` |
| Locking (spinlock) | `KeAcquireSpinLockRaiseToDpc` |
| `printk` | `DbgPrint` |
| `memcpy` / `memset` | `StorPortMoveMemory` / inline byte loop |
| `kzalloc` / `kmalloc` | `ExAllocatePoolWithTag(NonPagedPool)` |
| `delay()` | `StorPortStallExecution()` |
| `bus_dmamap_sync` | No-op (uncached memory) |

### What Was Removed

- **Target mode** (`AHC_TARGET_MODE` not defined — ~700 lines excluded)
- **Linux SCSI midlayer** integration — replaced by StorPort notifications
- **Module/insmod** infrastructure — single monolithic `.sys` driver
- **sysfs / proc** entries — replaced by StorPort WMI

### U160-Specific Adaptation Notes

Differences from the sibling U320 port that matter when reading this code:

- `hardware_scb.shared_data` embeds the CDB directly as `shared_data.cdb`
  (the U320 uses a separate `.idata` sub-union with task management field)
- Sense buffers live in `ahc->scb_data->sense[]`, accessed via
  `ahc_get_sense_buf()`; there is no per-SCB `sense_data` member and no
  `SCB_PKT_SENSE` flag
- Scatter/gather entries are `struct ahc_dma_seg` (32-bit addr + packed
  len/high-address bits in byte 3); `ahc_sg_setup()` implements this format
- No `bus_description` or `scb_timer` members on `ahc_softc`

## Build

Cross-compiled on Linux using MinGW for Windows PE32+ native x64.

```bash
# From repository root:
./toolchain/setup.sh        # install MinGW cross-compiler (one-time)

# Build this driver:
make clean && make          # Produces aic7xxx.sys

# Or use the wrapper (also builds the sibling aic79xx driver):
./toolchain/build.sh verify # Build + PE verification
```

See `../../../toolchain/README.md` for details.

### Debug builds and trace logging

By default, `make` produces a release build with per-I/O trace logging compiled out (zero overhead). To keep linker symbols and enable trace output for diagnostics, build with `DEBUG=1`:

```bash
make clean && make DEBUG=1
```

Or via the wrapper:

```bash
DEBUG=1 ./toolchain/build.sh verify
```

The `DEBUG` flag now controls both linker symbol retention and trace-logging verbosity. Release builds omit the format strings and DbgPrint calls entirely; debug builds include them, routing to kernel debug output (visible in DebugView with **Capture → Kernel** enabled). Initialization and error messages (adapter found, HwFindAdapter complete, PCI failures) are always included in both builds.

## Testing on Windows (x64)

The build output is unsigned and ships without a catalog. Windows treats
catalog-less packages differently depending on Code Integrity state:

- **Test signing enabled** (`bcdedit /set testsigning on`): Driver Store
  import rejects the package outright (`0xE000022F`), no override dialog,
  device falls back to a NULL driver. Test signing alone is *not* enough to
  install these builds.
- **Default enforcement**: Device Manager → Update driver → Browse →
  Let me pick → Have Disk offers *"Install this driver software anyway"*;
  accepting it stages the package as Unsigned. If the signer certificate is
  already in Trusted Publishers (e.g. after ticking *"Always trust"*), no
  dialog appears at all.
- **Disable driver signature enforcement** boot option (Shift + Restart →
  Startup Settings → 7): same result, per-session only.

For repeated testing across reboots, produce a real test-signed catalog:

```bat
:: 1. Build the catalog from the folder holding INF + SYS
inf2cat /driver:C:\drv\aic7xxx /os:10_x64

:: 2. Create and export a self-signed code-signing certificate
powershell -c "$c = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=AIC Dev Test' -CertStoreLocation Cert:\CurrentUser\My; Export-Certificate -Cert $c -FilePath C:\drv\aic.cer"

:: 3. Trust the certificate at machine level
certutil -addstore Root C:\drv\aic.cer
certutil -addstore TrustedPublisher C:\drv\aic.cer

:: 4. Sign driver and catalog
signtool sign /fd sha256 /n "AIC Dev Test" C:\drv\aic7xxx\aic7xxx.sys
signtool sign /fd sha256 /n "AIC Dev Test" C:\drv\aic7xxx\aic7xxx.cat

:: 5. Install (requires test signing enabled)
pnputil /add-driver C:\drv\aic7xxx\aic7xxx.inf /install
```

`inf2cat` and `signtool` ship with the WDK (`signtool` also with the
standalone Windows SDK). `inf2cat` doubles as an INF linter — it reports
structural defects by line.

When replacing `a .sys` after recompilation, bump `DriverVer` in the INF or
remove the previously staged package first
(`pnputil /delete-driver oemNN.inf /uninstall`) — otherwise the Driver Store
keeps serving the cached copy.

Full cleanup when a device gets wedged:

```bat
pnputil /enum-drivers                        :: find oemNN.inf for this package
pnputil /delete-driver oemNN.inf /uninstall /force
sc delete aic7xxx                            :: remove stale service
:: Device Manager: uninstall device ("attempt to remove the driver"),
:: then Action -> Scan for hardware changes
```

Install diagnostics land in `C:\Windows\INF\setupapi.dev.log`; search from
the bottom for the latest `>>> [Device Install` block.

## File Inventory

### Source files (compiled)

| File | Origin | License | Lines | Description |
|---|---|---|---|---|
| `aic7xxx_core.c` | Linux | BSD/GPL | ~7,900 | aic78xx SCSI core (unmodified) |
| `aic7xxx_storport.c` | Windows | MIT | ~700 | StorPort entry points, DMA, PCI, debug |
| `aic7xxx_pci.c` | Linux | BSD/GPL | ~2,400 | PCI identity table, chip setup |
| `aic7xxx_93cx6.c` | Linux | BSD/GPL | ~300 | Serial EEPROM read algorithm |

### Header files

| File | Origin | License | Description |
|---|---|---|---|
| `aic7xxx.h` | Linux | BSD/GPL | Core data structures, ahc_softc |
| `aic7xxx_reg.h` | Linux | BSD/GPL | Register definitions (auto-generated) |
| `aic7xxx_seq.h` | Linux | BSD/GPL | Sequencer firmware image (auto-generated) |
| `aic7xxx_pci.h` | Linux | BSD/GPL | PCI device ID defines |
| `aic7xxx_inline.h` | Linux | BSD/GPL | Inline functions |
| `aic7xxx_osm.h` | Windows | MIT | OSM shim: types, macros, stubs |
| `aic7xxx_win.h` | Windows | MIT | AHC7XXX_DEVICE_EXTENSION |
| `aic7xxx_93cx6.h` | Linux | BSD/GPL | Serial EEPROM definitions |
| `../common/aicasm/aicasm_insformat.h` | Linux | BSD/GPL | Sequencer instruction formats |
| `../common/queue.h` | FreeBSD | BSD | BSD queue macros |
| `../common/ahc_osm_common.h` | Windows | MIT | Shared OSM types/macros |

### Firmware sources (input to aicasm, see `toolchain/README.md`)

| File | Origin | License | Description |
|---|---|---|---|
| `aic7xxx.seq` | Linux | BSD/GPL | Fast..U160 sequencer program source |
| `aic7xxx.reg` | Linux | BSD/GPL | Register/symbol definitions for the sequencer |

### Build files

| File | Description |
|---|---|
| `Makefile` | MinGW cross-build |
| `../common/ddk_storport.def` | Import library: StorPort functions |
| `../common/ddk_ntoskrnl.def` | Import library: NTOSKRNL functions |
| `aic7xxx.inf` | INF: device matching for the full aic78xx family |
| `aic7xxx.rc` | Version resource (compiled via windres into aic7xxx.sys) |

## Licensing Summary

```
aic7xxx_core.c        BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic7xxx.h             BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic7xxx_inline.h      BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic7xxx_pci.c         BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic7xxx_pci.h         BSD-3-Clause  (Adaptec Inc.)
aic7xxx_seq.h         BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic7xxx_reg.h         BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic7xxx_93cx6.c/.h    BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aicasm_insformat.h    BSD-3-Clause  (Justin T. Gibbs)
aic7xxx.seq/.reg      BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
scsi_message.h        BSD-3-Clause  (Justin T. Gibbs)
queue.h               BSD-2-Clause  (University of California)
aic7xxx_storport.c    MIT           (c) 2026 Andrei Kazialetski
aic7xxx_osm.h         MIT           (c) 2026 Andrei Kazialetski
aic7xxx_win.h         MIT           (c) 2026 Andrei Kazialetski
Makefile              MIT           (c) 2026 Andrei Kazialetski
aic7xxx.inf           MIT           (c) 2026 Andrei Kazialetski
ddk_*.def             MIT           (c) 2026 Andrei Kazialetski
```

The BSD-3-Clause license in the Linux-origin files permits redistribution
under MIT terms for the adaptation layer. The core files retain their
original copyright and license headers unchanged.

## Windows Port Bug Fixes (2026-08-23)

### Parts 1-3: CAM Status, Freeze Bypass, and Extended Bypass

**Summary**: The same four bugs fixed in aic79xx were present in aic7xxx:

1. **Part 1 (CAM status mapping)**: `ahc_done()` was missing critical cases, causing successful completions to misreport as errors. Fixed by adding `CAM_REQ_INPROG`, `CAM_REQUEUE_REQ`, and four additional status cases.

2. **Part 2 (SELTO pause bypass)**: `ahc_platform_freeze_devq()` was pausing empty-target discovery for 30 seconds each. Fixed by bypassing the pause when freeze reason is `CAM_SEL_TIMEOUT`.

3. **Part 3 (extended freeze bypass)**: Volume mount was still stalling on routine CHECK CONDITION and negotiation retries. Fixed by extending the bypass to also skip the pause for `CAM_SCSI_STATUS_ERROR` and `CAM_REQUEUE_REQ`.

All changes applied to `aic7xxx_storport.c` (Platform hook) and verified on real hardware (AHA-29160 and sibling devices).

### Part 4: Freeze/Status Ordering — NOT NEEDED for aic7xxx

**Key difference**: The aic7xxx driver's `ahc_handle_scsi_status()` in `aic7xxx_core.c:1038-1040` already orders the transaction status classification BEFORE calling `ahc_freeze_devq()` — the opposite of aic79xx's bug. As a result, the Part 3 bypass worked correctly out-of-the-box on aic7xxx, without needing the core reordering fix.

This is likely due to a code divergence — the two files were synced from the same Linux baseline at different times, or one was hand-edited. Either way, aic7xxx's correct ordering means it never suffered the ~150-second mount stall that aic79xx did.

**Critical invariant** (same as aic79xx): If `aic7xxx_core.c` is ever re-synced from a newer Linux baseline, preserve the ordering: `ahc_set_transaction_status()` must be called BEFORE `ahc_freeze_devq()` in all paths that freeze the devq, or the Part 3 bypass will silently stop working and the mount stall will return.

### Verification

All three parts were validated on real hardware (Fast/Ultra/U160 aic78xx devices, Windows 11 x64):
- Bus enumeration and device detection working correctly
- Volume mount and I/O functioning without stalls

### Part 5: Distribution Packaging — INF, Signing Workflow, Architecture Documentation

**Summary**: Completed driver packaging and test-signing infrastructure for Windows distribution.

**Deliverables**:
- **INF files** (`aic7xxx.inf`): Device/driver matching, already comprehensive and correct (no changes needed)
- **DriverVer bump**: Updated to 08/23/2026, version 1.0.0.0 to reflect Parts 1-3 fixes
- **PowerShell signing script** (`toolchain/windows/sign-test-driver.ps1`): Automates test-certificate generation and self-signing
- **PORTING.md architecture notes**: Documented Parts 1-3 fixes and critical invariants
- **TESTING.md**: Updated to point to the new signing workflow

**Impact**: Developers can now build, self-sign, and test drivers without manual Windows certificate management steps.

### Part 6: Debug/Trace Logging Gating — Release vs. Debug Builds

**Problem**: Development included extensive per-I/O `DbgPrint` trace logging to capture kernel.log for diagnosis. These traces are invaluable during development but inappropriate for release builds.

**Solution**: Gate ~7 high-frequency per-I/O trace calls behind an `AIC_DBGPRINT` macro, controlled by the existing `DEBUG=1` Makefile variable:
- **Release builds** (default `make`): Trace logging compiled out entirely (zero code, zero overhead)
- **Debug builds** (`make DEBUG=1`): Trace logging enabled, symbols retained for WinDbg/objdump

**Implementation**:
- Added `AIC_DBGPRINT` macro to driver OSM header (`aic7xxx_osm.h`) and shared header (`ahc_osm_common.h`)
- Wrapped ~7 trace calls: SI#, non-EXECUTE fn, ISR#, DONE#, ahc_print_path (2 calls)
- Left ~7 one-time init/error messages ungated (always present in both builds)
- Updated Makefile to define `AIC_DBGPRINT_ENABLED` when `DEBUG=1`
- Updated `build.sh` to forward DEBUG environment variable: `DEBUG=1 ./toolchain/build.sh`

**Binary size comparison** (verified on real hardware):

| Build | MinGW Release | MinGW Debug | VS2026 Release | VS2026 Debug |
|-------|---------------|-------------|----------------|--------------|
| Size | 120 KB | 133 KB | 91 KB | 135 KB |

**Verification** (2026-08-23):
- Release build (both toolchains): zero trace strings, init/error messages present ✓
- Debug build (both toolchains): all trace strings included, symbols retained ✓
- Functionality identical across both toolchains ✓

## Summary of All 6 Parts (aic7xxx)

| Part | Issue | Fix | Impact |
|------|-------|-----|--------|
| 1 | Completions misreported as errors | CAM status mapping + residual + autosense | Devices detectable |
| 2 | 8+ minute discovery stall | SELTO pause bypass | ~7.7s scan time |
| 3 | ~150s mount stall | Extended bypass for CHECK CONDITION + CAM_REQUEUE_REQ | ~8.9s mount time |
| 4 | — | NOT NEEDED (correct ordering in original code) | No changes required |
| 5 | Manual signing, no distribution | PowerShell script + INF/docs | Repeatable test builds |
| 6 | Debug logging overhead in release | AIC_DBGPRINT macro gating | Smaller release builds |

**Result**: Fast, stable, production-ready driver with optimized release builds.
