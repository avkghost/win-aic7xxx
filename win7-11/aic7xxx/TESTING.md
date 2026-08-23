# aic7xxx.sys — Runtime Test Procedure

## Hardware Required

- **Adaptec AHA-29160 / 2940U2W / 3960D** (or any aic78xx-family card listed in `aic7xxx.inf`)
- A SCSI device (hard drive, tape, or loopback terminator) connected to the card
- Windows 7/10/11 x64 test machine with a free PCI/PCIe slot

## Software Required

- **DebugView** (Sysinternals) — captures kernel DbgPrint output
  https://learn.microsoft.com/en-us/sysinternals/downloads/debugview
- Test-signing certificate or DSE bypass for unsigned driver loading
- The built files: `aic7xxx.sys`, `aic7xxx.inf`

## Step 1: Prepare the Test Machine

### Enable test signing (recommended over DSE bypass)

Open an **elevated** Command Prompt:

```
bcdedit /set testsigning on
```

Reboot. The desktop watermark "Test Mode" will appear.

### Alternatively: disable DSE entirely (less safe)

```
bcdedit /set nointegritychecks on
bcdedit /set testsigning on
```

Reboot. This is only for lab/test machines.

## Step 2: Build and Sign

### On the Linux build host:

```bash
cd win7-11/aic7xxx
make clean && make
```

Transfer `aic7xxx.sys`, `aic7xxx.inf`, and `aic7xxx.cat` (if pre-generated) to the Windows test machine.

### On the Windows test machine: Automated self-signing

Run the provided PowerShell signing script (requires Administrator privileges and Windows SDK/WDK installed):

```powershell
cd toolchain\windows
.\sign-test-driver.ps1 -Driver aic7xxx -SourceDir C:\path\to\aic7xxx -Install
```

The script will:
1. Generate the catalog with `inf2cat`
2. Create a self-signed certificate (CN=AIC Dev Test)
3. Trust it in Root and TrustedPublisher stores
4. Sign both `.sys` and `.cat` files
5. Optionally install the driver via `pnputil`

See `../../../win7-11/aic7xxx/doc/PORTING.md` for detailed signing and installation procedures if manual steps are preferred.

## Step 3: Install the Driver

### Option A: Device Manager

1. Open Device Manager (devmgmt.msc)
2. Look under "Other devices" or "SCSI and RAID controllers" for the Adaptec card
   - PCI\VEN_9005&DEV_0080 = AHA-29160 (AIC-7892)
   - PCI\VEN_9005&DEV_0010 = AHA-2940U2W/U2B (AIC-7890)
   - PCI\VEN_9005&DEV_00C0 = AHA-3960D (AIC-7899)
   - PCI\VEN_9004&DEV_8178 = AHA-2940U/UW (AIC-7880)
3. Right-click → **Update driver**
4. **Browse my computer for drivers** → **Let me pick** → **Have Disk**
5. Browse to `aic7xxx.inf`, select the matching Adaptec controller entry
6. Accept the unsigned driver warning

### Option B: pnputil (command line)

```cmd
pnputil /add-driver aic7xxx.inf /install
```

### Option C: Manual copy (quick test)

```cmd
copy aic7xxx.sys %SystemRoot%\System32\drivers\
```

Then in Device Manager, update driver pointing to the INF.

## Step 4: Capture Debug Output

1. Launch **DebugView** as Administrator
2. Enable **Capture** → **Kernel** (essential — DbgPrint goes to kernel debug output)
3. Enable **Capture** → **Capture Events**
4. **Clear** the log, then **reload** the driver (see Step 5)

## Step 5: Load the Driver

If the driver was just installed, reboot to trigger boot-start loading.

To reload without reboot (from elevated Command Prompt):

```cmd
sc stop aic7xxx
sc start aic7xxx
```

Or disable/re-enable the device in Device Manager.

## Step 6: Expected Debug Output

Look for these DbgPrint messages in DebugView:

### Successful initialization sequence:

```
aic7xxx: Found <card name from identity table>
```
→ `ahc_find_pci_device()` matched an entry in `ahc_pci_ident_table`.

```
aic7xxx: HwFindAdapter complete, bus=%lu slot=%lu base=0x%p
```
→ HwFindAdapter succeeded. Note the bus/slot/base for reference.

### SCSI bus scan:

The core issues INQUIRY to each target during device discovery. Devices
found on the bus appear in Device Manager under their class drivers.

### Failure messages:

```
aic7xxx: Failed to read PCI config
```
→ PCI bus/slot incorrect.

```
aic7xxx: Unknown PCI device
```
→ Card not in `ahc_pci_ident_table` (check VEN/DEV against `aic7xxx_pci.h`).

```
aic7xxx: Failed to map device registers
```
→ BAR0 mapping failed.

```
aic7xxx: Failed to allocate uncached extension
```
→ Not enough contiguous DMA memory.

```
aic7xxx: ahc_pci_config failed (%d)
```
→ Chip initialization failed — check SEEPROM probe and sequencer load.

## Step 7: Test SCSI I/O

### With a physical SCSI device attached:

1. Open **Disk Management** (diskmgmt.msc)
2. The SCSI disk should appear as an uninitialized disk
3. Initialize, partition, and format it
4. Run a benchmark (CrystalDiskMark) to verify throughput

### With a SCSI loopback/terminator only:

Even with no device attached, a successful init means the driver loaded.
The bus scan will report "no device" for all IDs, which is correct behavior.

Use a SCSI bus terminator on the LVD connector if no devices are attached
to avoid bus floating errors.

## Step 8: Verify in Device Manager

1. Open Device Manager
2. Expand **SCSI and RAID controllers**
3. Verify the Adaptec controller entry appears without warning icons
4. Right-click → Properties → **Driver** tab:
   - Driver Provider: Adaptec
   - Driver Date: 08/22/2026
   - Driver Version: 1.0.0.0
5. Properties → **Details** tab → **Driver Key**:
   Should show `{4D36E97B-E325-11CE-BFC1-08002BE10318}\000N`

## Troubleshooting

### Driver fails to load (Code 39 or Code 52)

- Enable test signing: `bcdedit /set testsigning on`
- Sign the .sys: `signtool sign /s PrivateCertStore /n "aic7xxx-test" aic7xxx.sys`
- Check DebugView for DbgPrint output

### No DebugView output at all

- DebugView must be run **as Administrator**
- **Capture → Kernel** must be enabled
- If kernel debug is enabled (`bcdedit /debug on`), DebugView may not capture output; use WinDbg instead

### HwFindAdapter returns SP_RETURN_ERROR

Check DebugView for:
- "Failed to read PCI config" — PCI bus/slot incorrect
- "Unknown PCI device" — device not in identity table
- "Failed to map device registers" — BAR0 issue
- "Failed to allocate uncached extension" — not enough contiguous DMA memory

### SCSI bus reset loop

The driver calls `ahc_reset_channel()` during init and on HwResetBus. If a SCSI
device keeps causing bus errors, the driver may reset repeatedly. Check
termination, cable length, and SCSI ID conflicts.

### SCSI devices not detected

- Verify the SCSI bus is properly terminated (active terminator at each end of the bus)
- Check for SCSI ID conflicts — each device must have a unique ID (0-15 wide, 0-7 narrow)
- Ensure cables are within specification length (12m max for LVD Ultra160)
- Try lowering the maximum transfer rate in BIOS if signal integrity is marginal

## Debug Tunables

These can be set in the source before building:

| Variable | Default | Effect |
|---|---|---|
| `aic7xxx_verbose` | 1 | Enable/disable verbose debug prints |

The core also has `ahc_debug` (bitmask, requires `#define AHC_DEBUG`) for deeper diagnostics:

```c
#define AHC_SHOW_MISC             0x0001  /* Miscellaneous traces */
#define AHC_SHOW_SENSE            0x0002  /* Auto-sense data */
#define AHC_SHOW_TERMCTL          0x0008  /* Termination control */
#define AHC_SHOW_MEMORY           0x0010  /* Memory allocation traces */
#define AHC_SHOW_MESSAGES         0x0020  /* SCSI message phase traces */
#define AHC_SHOW_DV               0x0040  /* Domain validation */
#define AHC_SHOW_SELTO            0x0080  /* Selection timeout traces */
#define AHC_SHOW_QFULL            0x0200  /* Queue full handling */
#define AHC_SHOW_QUEUE            0x0400  /* Queue operations */
#define AHC_SHOW_TQIN             0x0800  /* Target mode incoming queue */
#define AHC_SHOW_MASKED_ERRORS    0x1000  /* Masked error reports */
#define AHC_DEBUG_SEQUENCER       0x2000  /* Sequencer download traces */
```

Set `ahc_debug = 0xFFFF;` for maximum verbosity.
