# AIC-79xx Windows StorPort Miniport Driver

## Origin

This driver is a **Windows StorPort miniport adaptation** of the Linux
`aic79xx` SCSI host adapter driver originally written by:

- **Justin T. Gibbs** — core driver architecture and sequencer firmware
- **Adaptec Inc.** — hardware documentation and maintenance

The Linux source was distributed under a BSD/GPL dual license. The original
BSD copyright and license terms are preserved in all Linux-origin source
files (`aic79xx_core.c`, `aic79xx.h`, `aic79xx_pci.c`, `aic79xx_pci.h`,
`aic79xx_seq.h`, `aic79xx_reg.h`, `../common/aicasm/aicasm_insformat.h`).

The Windows adaptation layer (OSM — OS Module) and StorPort glue code are
licensed under the MIT License, copyright (c) 2026 Andrei Kazialetski.

## What This Driver Is

A Windows kernel-mode SCSI miniport driver for Adaptec AIC-79xx Ultra320
SCSI host bus adapters, built on the StorPort I/O model:

- **ASC-29320LPE** — single-channel U320, PCIe, AIC-7901
- **ASC-29320A/ALP/LP** — single-channel U320, PCI-X
- **ASC-29320/B** — dual-channel U320, PCI-X
- **ASC-39320/A/D** — dual-channel U320, PCI-X

Target OS: Windows 7 through Windows 11 (x86-64).

## What Changed (Linux → Windows)

The adaptation follows a **two-layer design**. The Linux core
(`aic79xx_core.c`, ~10,700 lines) is compiled **unmodified**. All OS
differences are absorbed by the OSM header (`aic79xx_osm.h`) and StorPort
glue file (`aic79xx_storport.c`).

### Preserved from Linux

- Sequencer firmware (downloaded to AIC-79xx on-chip processor)
- PCI configuration parsing and chip detection
- SCSI bus negotiation, tag queuing, error recovery
- Core data structures (`ahd_softc`, `scb`, `hardware_scb`)
- All chip register definitions and sequencer opcodes

### Replaced for Windows

| Linux concept | Windows (StorPort) equivalent |
|---|---|
| `struct scsi_cmnd` / `struct scsi_device` | Shadow structs embedded in `scb_platform_data` |
| `scsi_attach()` / `scsi_detach()` | `HwFindAdapter` / `HwAdapterControl` |
| `scsi_done()` | `StorPortNotification(NextRequest)` + `StorPortNotification(RequestComplete)` |
| `scsi_report_bus_status()` | `StorPortNotification(ResetBus)` |
| `ahd_platform_freeze_devq()` | `StorPortPauseDevice()` |
| `ahd_platform_set_tags()` | No-op (StorPort manages tags) |
| `ahd_freeze_simq()` / `ahd_release_simq()` | No-ops (single-SRB model) |
| DMA alloc (contigmalloc) | `StorPortGetUncachedExtension` bump allocator |
| MMIO (bus_space) | `READ_REGISTER_UCHAR` / `WRITE_REGISTER_UCHAR` |
| PCI config (pci_read_config) | `StorPortGetBusData` / `StorPortSetBusDataByOffset` |
| Locking (spinlock) | `KeAcquireSpinLockRaiseToDpc` |
| `printk` | `DbgPrint` |
| `panic` | `DbgPrint` + `__debugbreak()` |
| `memcpy` / `memset` | `StorPortMoveMemory` / inline byte loop |
| `kzalloc` / `kmalloc` | `ExAllocatePoolWithTag(NonPagedPool)` |
| `delay()` | `StorPortStallExecution()` |
| `bus_dmamap_sync` | No-op (uncached memory) |

### What Was Removed

- **Target mode** (`AHD_TARGET_MODE` not defined — ~700 lines excluded)
- **Linux SCSI midlayer** integration — replaced by StorPort notifications
- **Module/insmod** infrastructure — single monolithic `.sys` driver
- **sysfs / proc** entries — replaced by StorPort WMI

Note: the sibling `aic7xxx` driver (Fast/Ultra/Ultra2/U160, AIC-78xx family)
is a separate port sharing the same architecture. See `../aic7xxx/doc/`.

## Build

Cross-compiled on Linux using MinGW for Windows PE32+ native x64.

```bash
# From repository root:
./toolchain/setup.sh        # install MinGW cross-compiler (one-time)

# Build this driver:
make clean && make          # Produces aic79xx.sys

# Or use the wrapper (also builds the sibling aic7xxx driver):
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
inf2cat /driver:C:\drv\aic79xx /os:10_x64

:: 2. Create and export a self-signed code-signing certificate
powershell -c "$c = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=AIC Dev Test' -CertStoreLocation Cert:\CurrentUser\My; Export-Certificate -Cert $c -FilePath C:\drv\aic.cer"

:: 3. Trust the certificate at machine level
certutil -addstore Root C:\drv\aic.cer
certutil -addstore TrustedPublisher C:\drv\aic.cer

:: 4. Sign driver and catalog
signtool sign /fd sha256 /n "AIC Dev Test" C:\drv\aic79xx\aic79xx.sys
signtool sign /fd sha256 /n "AIC Dev Test" C:\drv\aic79xx\aic79xx.cat

:: 5. Install (requires test signing enabled)
pnputil /add-driver C:\drv\aic79xx\aic79xx.inf /install
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
sc delete aic79xx                            :: remove stale service
:: Device Manager: uninstall device ("attempt to remove the driver"),
:: then Action -> Scan for hardware changes
```

Install diagnostics land in `C:\Windows\INF\setupapi.dev.log`; search from
the bottom for the latest `>>> [Device Install` block.

## File Inventory

### Source files (compiled)

| File | Origin | License | Lines | Description |
|---|---|---|---|---|
| `aic79xx_core.c` | Linux | BSD/GPL | ~10,700 | U320 SCSI core (unmodified) |
| `aic79xx_storport.c` | Windows | MIT | ~870 | StorPort entry points, DMA, PCI, debug |
| `aic79xx_pci.c` | Linux | BSD/GPL | ~1,000 | PCI identity table, chip setup |

### Header files

| File | Origin | License | Description |
|---|---|---|---|
| `aic79xx.h` | Linux | BSD/GPL | Core data structures, ahd_softc |
| `aic79xx_reg.h` | Linux | BSD/GPL | Register definitions (auto-generated) |
| `aic79xx_seq.h` | Linux | BSD/GPL | Sequencer firmware image (auto-generated) |
| `aic79xx_pci.h` | Linux | BSD/GPL | PCI device ID defines |
| `aic79xx_inline.h` | Windows | MIT | Forward declarations, inlines |
| `aic79xx_osm.h` | Windows | MIT | OSM shim: types, macros, stubs |
| `aic79xx_win.h` | Windows | MIT | AIC79XX_DEVICE_EXTENSION |
| `../common/aicasm/aicasm_insformat.h` | Linux | BSD/GPL | Sequencer instruction formats |
| `../common/queue.h` | FreeBSD | BSD | BSD queue macros |
| `../common/ahc_osm_common.h` | Windows | MIT | Shared OSM types/macros |

### Firmware sources (input to aicasm, see `toolchain/README.md`)

| File | Origin | License | Description |
|---|---|---|---|
| `aic79xx.seq` | Linux | BSD/GPL | U320 sequencer program source |
| `aic79xx.reg` | Linux | BSD/GPL | Register/symbol definitions for the sequencer |

### Build files

| File | Description |
|---|---|
| `Makefile` | MinGW cross-build |
| `../common/ddk_storport.def` | Import library: StorPort functions |
| `../common/ddk_ntoskrnl.def` | Import library: NTOSKRNL functions |
| `aic79xx.inf` | INF: device matching for all AIC-79xx variants |
| `aic79xx.rc` | Version resource (compiled via windres into aic79xx.sys) |

## Licensing Summary

```
aic79xx_core.c        BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic79xx.h             BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic79xx_pci.c         BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic79xx_pci.h         BSD-3-Clause  (Adaptec Inc.)
aic79xx_seq.h         BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aic79xx_reg.h         BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
aicasm_insformat.h    BSD-3-Clause  (Justin T. Gibbs)
aic79xx.seq/.reg      BSD-3-Clause  (Justin T. Gibbs / Adaptec Inc.)
scsi_message.h        BSD-3-Clause  (Justin T. Gibbs)
queue.h               BSD-2-Clause  (University of California)
aic79xx_storport.c    MIT           (c) 2026 Andrei Kazialetski
aic79xx_osm.h         MIT           (c) 2026 Andrei Kazialetski
aic79xx_win.h         MIT           (c) 2026 Andrei Kazialetski
aic79xx_inline.h      MIT           (c) 2026 Andrei Kazialetski
Makefile              MIT           (c) 2026 Andrei Kazialetski
aic79xx.inf           MIT           (c) 2026 Andrei Kazialetski
ddk_*.def             MIT           (c) 2026 Andrei Kazialetski
```

The BSD-3-Clause license in the Linux-origin files permits redistribution
under MIT terms for the adaptation layer. The core files retain their
original copyright and license headers unchanged.

## Windows Port Bug Fixes (2026-08-23)

### Part 1: CAM Status Mapping and Completion Reporting

**Problem**: The Windows wrapper's `ahd_done()` function (`aic79xx_storport.c:771-817`) was missing critical cases in its CAM-to-SRB status switch statement, causing all successfully-completed SCSI commands to misreport as `SRB_STATUS_ERROR` instead of `SRB_STATUS_SUCCESS`. Root cause: the core driver (`aic79xx_core.c`, shared with Linux/FreeBSD) leaves a completed SCB's `cam_status` untouched at its default value `CAM_REQ_INPROG` (0x00) when completion is clean with no residual — a convention that works fine in Linux but was not recognized by the Windows wrapper.

**Additional issues found and fixed**:
- `CAM_REQUEUE_REQ` case missing — transient conditions (queue-full, negotiation retry) were incorrectly reported as hard errors
- Four status codes (`CAM_SCSI_BUS_RESET`, `CAM_UNEXP_BUSFREE`, `CAM_SEQUENCE_FAIL`, `CAM_REQ_CMP_ERR`) were not explicitly mapped, falling to generic `SRB_STATUS_ERROR`
- `DataTransferLength` was never updated from residual — SRB always reported original requested length, not actual bytes transferred
- Autosense error path was backwards: `CAM_AUTOSENSE_FAIL` means the sense-fetch itself failed (sense data is invalid), but the code was copying the stale data and claiming validity

**Impact**: Devices could not be detected (StorPort never received successful INQUIRY responses). Installation hung and timed out.

**Fix**: Added comprehensive status mapping cases to `ahd_done()` and `ahc_done()`, computed `DataTransferLength` from residual, and fixed autosense validity flag logic.

### Part 2: Selection Timeout Pause Bypass

**Problem**: The platform hook `ahd_platform_freeze_devq()` (`aic79xx_storport.c:943-955`) calls `StorPortPauseDevice(..., 30)` unconditionally on every device-queue freeze, imposing a 30-second settlement pause. During bus discovery, when the driver probes empty target IDs via SCSI selection, the hardware times out (expected — no device present), the core calls `ahd_freeze_devq()` to freeze that target's queue, and the Windows hook pauses it for 30 seconds. With 16 targets and multiple empty IDs, this stalled bus scanning for ~8+ minutes, well beyond Windows' enumeration timeout window.

**Impact**: Even though devices were now detected (Part 1), the system couldn't enumerate them before Windows gave up.

**Fix**: Added an early-return bypass in `ahd_platform_freeze_devq()`: skip the pause specifically when the freeze reason is `CAM_SEL_TIMEOUT` (a selection timeout, which means no device is present — nothing to recover from). Genuine error-recovery pauses (bus resets, parity errors) still get the full 30-second settle time.

**Result**: Empty-target sweep dropped from ~8 minutes to ~7.7 seconds.

### Part 3: Extended Freeze Bypass for Routine Conditions

**Problem**: Part 2 fixed the SELTO case, but the ~150-second mount-phase stall remained: during initial volume mount (READ CAPACITY, MODE SENSE, INQUIRY on the real device), the driver was still hitting 30-second pauses on routine CHECK CONDITION responses — specifically, the first UNIT ATTENTION a newly-negotiated device typically reports ("Power-On Reset Occurred" or similar). The Windows class driver already handles CHECK CONDITION retries natively, so StorPort doesn't need the 30-second pause to "settle."

Additionally, negotiation-downgrade retries (WDTR/SDTR/PPR rejection handling in the core) set `CAM_REQUEUE_REQ` and call `ahd_freeze_devq()`, also hitting the full pause unnecessarily.

**Impact**: Even after Part 2, volume detection and mounting took ~150 seconds, with Device Manager appearing frozen.

**Fix**: Extended the freeze bypass condition in `ahd_platform_freeze_devq()` to also skip the pause for `CAM_SCSI_STATUS_ERROR` (routine CHECK CONDITION / UNIT ATTENTION) and `CAM_REQUEUE_REQ` (negotiation retry). These are conditions the Windows storage stack already manages with its own request-level retry logic.

**Critical invariant**: This bypass depends on the Part 4 fix (below) — the status must be set BEFORE the freeze fires, or the check reads stale data.

### Part 4: Freeze/Status Ordering — Core Bug Fix

**Problem**: The Part 3 bypass in `aic79xx_storport.c` reads `ahd_get_transaction_status(scb)` synchronously during `ahd_freeze_devq()`, but the core `ahd_handle_scsi_status()` function in `aic79xx_core.c:8820-8866` was calling `ahd_freeze_devq()` BEFORE setting the transaction status via `ahd_set_transaction_status()`. As a result, the bypass read the stale default value `CAM_REQ_INPROG`, not the real classification `CAM_SCSI_STATUS_ERROR`, so it never triggered — the Part 3 bypass silently did nothing.

**Root cause**: A state-ordering race condition between the Linux core's asynchronous completion path and the Windows wrapper's synchronous pause hook. Linux finishes its work later (after the core function returns); Windows needs decisions made immediately (inside the hook). The core was designed for Linux's model, not Windows'.

**Note on aic7xxx**: The parallel `ahc_handle_scsi_status()` in `aic7xxx_core.c:1038-1040` already orders status-before-freeze correctly — this bug was unique to aic79xx due to a code divergence in that specific function.

**Fix**: Reordered two statements in aic79xx_core.c:
- `ahd_handle_scsi_status()` (~line 8820): determine and set transaction status BEFORE calling `ahd_freeze_devq()`, deferring the autosense-related `ahd_done()` completion call via a local `autosense_fail` flag
- PPR-busfree error path (~line 3235): swap `ahd_set_transaction_status()` before `ahd_freeze_devq()`, same fix

This is a pure statement reorder with no functional change to what gets set — only the timing. No dependency exists between the status writes and the freeze/pause bookkeeping.

**Impact**: With status correctly visible at freeze time, the Part 3 bypass now engages for routine CHECK CONDITION and CAM_REQUEUE_REQ cases. Volume mount stalls disappeared. Total time-to-mount: ~8-9 seconds instead of ~150 seconds.

### Verification

All four parts were validated on real hardware (ASC-29320LPE, aic79xx, Windows 11 x64):
- Fresh kernel.log capture shows complete bus enumeration (16-ID SELTO sweep) in ~7.7 seconds, with no 30s stalls
- Volume mounted and recognized at t=8.71s (`CcInitializeNumaNodeForVolume` logged)
- 80MB file I/O completed successfully immediately after mount, with no Device Manager freeze

### Future Maintenance

**CRITICAL**: If `aic79xx_core.c` is ever re-synced from a newer Linux kernel baseline, the Part 4 ordering invariant must be preserved:

- `ahd_handle_scsi_status()` must call `ahd_set_transaction_status()` BEFORE `ahd_freeze_devq()`
- PPR-busfree path (line ~3235) must call `ahd_set_transaction_status()` BEFORE `ahd_freeze_devq()`

Failure to maintain this ordering will silently re-enable the ~150-second mount stall without any obvious log messages, because the Part 3 bypass will stop working. The `ahd_platform_freeze_devq()` hook will read stale status and never engage its bypass conditions.

This is the single most important invariant this Windows port introduces. The Linux core was not designed to interact with synchronous, platform-specific decision hooks at freeze time; the reordering works around that by ensuring classification happens before any hook sees the SCB.

### Part 5: Distribution Packaging — INF, Signing Workflow, Architecture Documentation

**Summary**: Completed driver packaging and test-signing infrastructure for Windows distribution.

**Deliverables**:
- **INF files** (`aic79xx.inf`, `aic7xxx.inf`): Device/driver matching, already comprehensive and correct (no changes needed)
- **DriverVer bump**: Updated to 08/23/2026, version 1.0.0.0 to reflect Parts 1-4 core fixes
- **PowerShell signing script** (`toolchain/windows/sign-test-driver.ps1`): Automates test-certificate generation and self-signing (inf2cat → cert creation → signing → pnputil installation)
- **PORTING.md architecture notes**: Documented Parts 1-4 fixes with critical invariants and maintenance guidance
- **TESTING.md**: Updated to point to the new signing workflow, removed deprecated makecert-based instructions

**Impact**: Developers can now build, self-sign, and test drivers without manual Windows certificate management steps. The signing script is repeatable and generates a fresh test certificate on each run.

### Part 6: Debug/Trace Logging Gating — Release vs. Debug Builds

**Problem**: Development included extensive per-I/O `DbgPrint` trace logging (SI#, Q#, ISR#, DONE# calls) to capture kernel.log for diagnosis. These traces are invaluable during development but inappropriate for release builds — they fire on every SCSI command, adding per-I/O overhead and flooding kernel debug output.

**Solution**: Gate ~17 high-frequency per-I/O trace calls behind an `AIC_DBGPRINT` macro, controlled by the existing `DEBUG=1` Makefile variable:
- **Release builds** (default `make`): Trace logging compiled out entirely (zero code, zero overhead)
- **Debug builds** (`make DEBUG=1`): Trace logging enabled, symbols retained for WinDbg/objdump

**Implementation**:
- Added `AIC_DBGPRINT` macro to both driver OSM headers (`aic79xx_osm.h`, `aic7xxx_osm.h`) and shared header (`ahc_osm_common.h`)
- Wrapped ~17 trace calls: SI#, non-EXECUTE fn, Q#, queued cdb, ISR#, DONE cam=, DONE#, ahd_print_path/ahc_print_path
- Left ~16 one-time init/error messages ungated (adapter found, HwFindAdapter complete, PCI/alloc failures, HwResetBus) — always present in both builds
- Updated both Makefiles to define `AIC_DBGPRINT_ENABLED` when `DEBUG=1`
- Updated `build.sh` to forward DEBUG environment variable: `DEBUG=1 ./toolchain/build.sh`

**Binary size comparison** (verified on real hardware):

| Build | MinGW Release | MinGW Debug | VS2026 Release | VS2026 Debug |
|-------|---------------|-------------|----------------|--------------|
| Size | 129 KB | 149 KB | 91 KB | 135 KB |
| Diff | — | +20 KB (+16%) | — | +44 KB (+48%) |

**Verification** (2026-08-23):
- Release build (both toolchains): zero trace strings (SI#/Q#/DONE# absent), init/error messages present ✓
- Debug build (both toolchains): all trace strings included, symbols retained ✓
- Functionality identical: kernel.log shows same clean behavior, 8.7s mount time ✓
- Performance: no regression in release builds despite macro wrapping ✓

**Impact**: Release drivers are production-ready without debug overhead. Development can enable tracing for diagnosis via `DEBUG=1` rebuild.

## Summary of All 6 Parts

| Part | Issue | Fix | Impact |
|------|-------|-----|--------|
| 1 | Completions misreported as errors | CAM status mapping + residual handling + autosense fix | Devices detectable |
| 2 | 8+ minute empty-target discovery stall | SELTO pause bypass | ~7.7s scan time |
| 3 | ~150s mount-phase stall | Extended bypass for CHECK CONDITION + CAM_REQUEUE_REQ | ~8.9s mount time |
| 4 | Part 3 bypass not working (core bug) | Status-before-freeze reordering in aic79xx_core.c | Part 3 bypass now active |
| 5 | Manual signing, no test distribution | PowerShell signing script + INF/docs | Repeatable test builds |
| 6 | Debug logging overhead in release builds | AIC_DBGPRINT macro gating + DEBUG flag | ~20-44 KB smaller release |

**Result**: From non-functional (Parts 1-3) → fast device detection (Part 4) → production-ready packages (Part 5) → optimized release builds (Part 6).
