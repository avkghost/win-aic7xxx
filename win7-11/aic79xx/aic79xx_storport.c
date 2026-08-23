/*
 * Windows StorPort Miniport glue for AIC79xx (U320 SCSI).
 * Implements StorPort entry points, platform functions, DMA stubs,
 * PCI config access, and debug print infrastructure.
 *
 * The core (aic79xx_core.c) stays unmodified. This file provides
 * all Windows-specific implementations declared in aic79xx_osm.h.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */

#include "aic79xx_win.h"
/* aic79xx_reg.h already included via aic79xx_osm.h */
#include "aic79xx_inline.h"

/* ahd_print_register is defined in aic79xx_core.c */

/****************************** Global State ***********************************/

uint32_t aic79xx_verbose = 1;
uint32_t aic79xx_slowcrc = 0;

/****************************** DriverEntry ************************************/

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    HW_INITIALIZATION_DATA hwInitData;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    AicTrace(AIC_STEP_DRVENTRY_ENTER, STATUS_SUCCESS);

    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);
    hwInitData.AdapterInterfaceType = PCIBus;

    /* StorPort callbacks */
    hwInitData.HwInitialize    = Aic79xx_HwInitialize;
    hwInitData.HwFindAdapter   = Aic79xx_HwFindAdapter;
    hwInitData.HwStartIo       = Aic79xx_HwStartIo;
    hwInitData.HwInterrupt     = Aic79xx_HwInterrupt;
    hwInitData.HwResetBus      = Aic79xx_HwResetBus;
    hwInitData.HwAdapterControl = Aic79xx_HwAdapterControl;

    /* We handle bus resets ourselves */
    hwInitData.TaggedQueuing = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;

    /* StorPort allocates this many bytes as HwDeviceExtension */
    hwInitData.DeviceExtensionSize = sizeof(AIC79XX_DEVICE_EXTENSION);

    /* PCI adapter: memory-mapped registers + I/O ports (see LSI U3 sample) */
    hwInitData.NumberOfAccessRanges = 2;
    hwInitData.NeedPhysicalAddresses = TRUE;

    /* Ask StorPort for an interrupt-synced DPC for completion */
    hwInitData.MapBuffers = STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;

    status = StorPortInitialize(DriverObject, RegistryPath, &hwInitData, NULL);

    AicTrace(AIC_STEP_STORPORT_INIT_RET, status);

    return status;
}

/****************************** HwFindAdapter **********************************/

ULONG
NTAPI
Aic79xx_HwFindAdapter(
    _In_  PVOID DeviceExtension,
    _In_  PVOID HwContext,
    _In_  PVOID BusInformation,
    _In_  PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Again
)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)DeviceExtension;
    struct ahd_softc *ahd = &ext->softc;
    const struct ahd_pci_identity *entry;
    PCI_COMMON_CONFIG pciConfig;
    ULONG bytesRead;
    int error;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    *Again = FALSE;

    StorPortZeroMemory(ext, sizeof(AIC79XX_DEVICE_EXTENSION));

    AicTrace(AIC_STEP_FIND_ENTER, STATUS_SUCCESS);

    /* Capture the PCI bus and slot from StorPort's ConfigInfo */
    ext->pci_bus  = ConfigInfo->SystemIoBusNumber;
    ext->pci_slot = ConfigInfo->SlotNumber;

    /* Read PCI configuration space */
    bytesRead = StorPortGetBusData(
        ext,
        PCIConfiguration,
        ext->pci_bus,
        ext->pci_slot,
        &pciConfig,
        sizeof(PCI_COMMON_CONFIG)
    );

    if (bytesRead < sizeof(PCI_COMMON_HEADER)) {
        DbgPrint("aic79xx: Failed to read PCI config\n");
        AicTrace(AIC_STEP_PCI_CFG_READ, STATUS_UNSUCCESSFUL);
        return SP_RETURN_ERROR;
    }

    /* Enable bus mastering and memory access */
    {
        uint16_t cmd = pciConfig.Command | PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE;
        StorPortSetBusDataByOffset(
            ext, PCIConfiguration, ext->pci_bus, ext->pci_slot,
            &cmd, PCIR_COMMAND, sizeof(cmd));
    }

    /* Map device registers using the TRANSLATED ranges PnP assigned.
     * Raw config-space BARs are bus-relative, and on these Adaptec parts
     * BAR0 (0x10) is the I/O-port window - the register space is the
     * memory-range. AccessRanges[] already resolved both problems.
     */
    {
        SCSI_PHYSICAL_ADDRESS ioAddress;
        PACCESS_RANGE ranges = (PACCESS_RANGE)(VOID *)ConfigInfo->AccessRanges;
        ULONG span = 0;
        ULONG i;

        ioAddress.QuadPart = 0;
        for (i = 0; i < ConfigInfo->NumberOfAccessRanges; i++) {
            if (!ranges[i].RangeInMemory)
                continue;
            ioAddress = ranges[i].RangeStart;
            span      = ranges[i].RangeLength;
            break;
        }
        ext->hw_device_base = NULL;
        if (span != 0)
            ext->hw_device_base = StorPortGetDeviceBase(
                ext, PCIBus, ext->pci_bus, ioAddress, span,
                FALSE /* memory space */);
    }

    if (ext->hw_device_base == NULL) {
        DbgPrint("aic79xx: Failed to map device registers\n");
        AicTrace(AIC_STEP_REGS_MAPPED, STATUS_UNSUCCESSFUL);
        return SP_RETURN_ERROR;
    }

    /* Allocate uncached extension for DMA
     * AHD_NSEG=128 SG segments, each ~16 bytes = 2KB minimum
     * Plus SCB area.  Ask for generous space.
     */
    ext->uncached_ext_size = 64 * 1024;  /* 64KB */
    ext->uncached_ext_va = (PUCHAR)StorPortGetUncachedExtension(
        ext,
        ConfigInfo,
        ext->uncached_ext_size
    );

    if (ext->uncached_ext_va == NULL) {
        DbgPrint("aic79xx: Failed to allocate uncached extension\n");
        AicTrace(AIC_STEP_UNCACHED_ALLOC, STATUS_UNSUCCESSFUL);
        return SP_RETURN_ERROR;
    }

    /* Get the physical address of the uncached extension base */
    {
        SCSI_PHYSICAL_ADDRESS physBase;
        ULONG physLen;
        physBase = StorPortGetPhysicalAddress(
            ext, NULL, ext->uncached_ext_va, &physLen);
        ext->uncached_ext_phys = (dma_addr_t)physBase.QuadPart;
    }

    /* ---- Initialize the embedded ahd_softc (what ahd_alloc() does on Linux) ---- */

    /* Set up dev_softc so PCI config accessors work */
    ahd->dev_softc = ext;  /* StorPort device extension */

    /* Allocate SEEPROM config */
    ahd->seep_config = (struct seeprom_config *)
         ExAllocatePoolWithTag(NonPagedPool, sizeof(struct seeprom_config), 'hcIA');
    if (ahd->seep_config == NULL) {
        DbgPrint("aic79xx: Failed to allocate SEEPROM config\n");
        AicTrace(AIC_STEP_SOFTC_INIT, STATUS_INSUFFICIENT_RESOURCES);
        return SP_RETURN_ERROR;
    }
    StorPortZeroMemory(ahd->seep_config, sizeof(struct seeprom_config));

    /* Initialize linked list */
    LIST_INIT(&ahd->pending_scbs);

    /* Set default softc fields */
    ahd->name = "aic79xx";
    ahd->unit = 0;
    ahd->description = NULL;
    ahd->bus_description = NULL;
    ahd->channel = 'A';
    ahd->chip = AHD_NONE;
    ahd->features = AHD_FENONE;
    ahd->bugs = AHD_BUGNONE;
    ahd->flags = AHD_SPCHK_ENB_A | AHD_RESET_BUS_A | AHD_TERM_ENB_A
               | AHD_EXTENDED_TRANS_A | AHD_STPWLEVEL_A;

    /* Timer: no-op on Windows */
    StorPortZeroMemory(&ahd->stat_timer, sizeof(ahd->stat_timer));

    /* Int coalescing defaults */
    ahd->int_coalescing_timer = AHD_INT_COALESCING_TIMER_DEFAULT;
    ahd->int_coalescing_maxcmds = AHD_INT_COALESCING_MAXCMDS_DEFAULT;
    ahd->int_coalescing_mincmds = AHD_INT_COALESCING_MINCMDS_DEFAULT;
    ahd->int_coalescing_threshold = AHD_INT_COALESCING_THRESHOLD_DEFAULT;
    ahd->int_coalescing_stop_threshold =
        AHD_INT_COALESCING_STOP_THRESHOLD_DEFAULT;

    /* Platform data */
    ahd->platform_data = (struct ahd_platform_data *)ext;
    ahd_platform_alloc(ahd, NULL);

    /* Initialize uncached extension bump allocator */
    ext->uc_ext_ptr = ext->uncached_ext_va;
    ext->uc_ext_remain = ext->uncached_ext_size;

    /* ---- Call into the Linux PCI config/init sequence ---- */

    /* Find the PCI identity for this chip (AIC-7901) */
    entry = ahd_find_pci_device(ahd->dev_softc);
    if (entry == NULL) {
        DbgPrint("aic79xx: Unknown PCI device\n");
        AicTrace(AIC_STEP_SOFTC_INIT, 0xE0001001); /* custom: no PCI identity */
        return SP_RETURN_ERROR;
    }

    DbgPrint("aic79xx: Found %s\n", entry->name);

    /* ahd_pci_config sets up chip registers, loads sequencer firmware,
     * probes SCB RAM, checks SEEPROM for termination settings, and
     * calls ahd_init() to start the sequencer.
     */
    error = ahd_pci_config(ahd, entry);
    if (error != 0) {
        DbgPrint("aic79xx: ahd_pci_config failed (%d)\n", error);
        AicTrace(AIC_STEP_SOFTC_INIT, (NTSTATUS)(0xE0002000 | (error & 0xFFF)));
        return SP_RETURN_ERROR;
    }

    /* Set required StorPort configuration fields */
    ConfigInfo->NumberOfBuses             = 1;         /* one SCSI bus - required for LUN enumeration */
    ConfigInfo->MaximumTransferLength     = 0x100000;  /* 1MB - AIC-7901 supports large transfers */
    ConfigInfo->NumberOfPhysicalBreaks     = AHD_NSEG;  /* max SG elements */
    ConfigInfo->MaximumNumberOfTargets     = 16;         /* SCSI bus supports 16 targets */
    ConfigInfo->MaximumNumberOfLogicalUnits = 8;         /* 8 LUNs per target */
    ConfigInfo->InitiatorBusId[0]          = (UCHAR)ahd->our_id;

    DbgPrint("aic79xx: HwFindAdapter complete, bus=%lu slot=%lu base=0x%p\n",
              ext->pci_bus, ext->pci_slot, ext->hw_device_base);

    /* ahd_reset() leaves the chip with INTEN masked (only ahd_resume()
     * re-enables it upstream). Storport expects interrupts from now on -
     * without this every command times out silently. */
    ahd_intr_enable(ahd, TRUE);

    AicTrace(AIC_STEP_FOUND, STATUS_SUCCESS);

    return SP_RETURN_FOUND;
}

/****************************** HwInitialize ***********************************/

BOOLEAN
NTAPI
Aic79xx_HwInitialize(
    _In_ PVOID DeviceExtension
)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    /* Chip initialization happens in HwFindAdapter.
     * Sequencer should already be running.
     */
    return TRUE;
}

/****************************** HwStartIo **************************************/

BOOLEAN
NTAPI
Aic79xx_HwStartIo(
    _In_  PVOID DeviceExtension,
    _In_  PSCSI_REQUEST_BLOCK Srb
)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)DeviceExtension;
    struct ahd_softc *ahd = &ext->softc;
    struct scb *scb;
    struct hardware_scb *hscb;
    uint8_t *cdb;
    uint8_t cdb_len;
    PSCATTER_GATHER_LIST sgList;
    u_int col_idx = AHD_NEVER_COL_IDX;
    static ULONG dbg_si = 0, dbg_nonx = 0;

    if (dbg_si < 24) {
        dbg_si++;
        AIC_DBGPRINT("aic79xx: SI#%lu fn=%02x t=%d l=%d len=%lx\n",
                     dbg_si, Srb->Function, Srb->TargetId, Srb->Lun,
                     Srb->DataTransferLength);
    }
    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
        if (dbg_nonx < 8) { dbg_nonx++;
            AIC_DBGPRINT("aic79xx: non-EXECUTE fn=%02x\n", Srb->Function); }
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        StorPortNotification(RequestComplete, ext, Srb);
        StorPortNotification(NextRequest, ext);
        return TRUE;
    }

    /* Get a free SCB from the pool */
    scb = ahd_get_scb(ahd, col_idx);
    if (scb == NULL) {
        /* No SCBs available — complete with busy status so StorPort retries */
        Srb->SrbStatus = SRB_STATUS_BUSY;
        StorPortNotification(RequestComplete, ext, Srb);
        StorPortNotification(NextRequest, ext);
        return TRUE;
    }

    /* Store SRB back-pointer for ahd_done() */
    scb->platform_data->srb = Srb;

    /* Wire up the shadow scsi_cmnd for the core's io_ctx.
     * The core accesses scb->io_ctx as a struct scsi_cmnd* for status,
     * data direction, residual, and device pointer.  On Windows there
     * is no SCSI midlayer, so we embed the shadow in platform_data. */
    scb->io_ctx = &scb->platform_data->shadow_cmd;
    scb->platform_data->shadow_cmd.device = &scb->platform_data->shadow_dev;
    scb->platform_data->shadow_dev.qfrozen = 0;

    /* Populate the shadow scsi_cmnd for the core's io_ctx */
    {
        struct scsi_cmnd *cmd = scb->io_ctx;
        cmd->result = (CAM_REQ_INPROG << 16);
        cmd->sc_data_direction = Srb->SrbFlags & SRB_FLAGS_DATA_IN
            ? 1 /* DMA_FROM_DEVICE */ : 2 /* DMA_TO_DEVICE */;
        cmd->resid = 0;
        /* cmd->device already set to &shadow_dev above — do NOT NULL it */
    }

    /* Fill out the hardware SCB */
    hscb = scb->hscb;
    hscb->control = DISCENB | SIMPLE_QUEUE_TAG;
    hscb->scsiid = ((Srb->TargetId << TID_SHIFT) & TID) | ahd->our_id;
    hscb->lun = Srb->Lun;
    hscb->task_management = 0;

    /* Copy CDB */
    cdb = Srb->Cdb;
    cdb_len = Srb->CdbLength;
    hscb->cdb_len = cdb_len;
    memcpy(hscb->shared_data.idata.cdb, cdb, cdb_len);

    scb->platform_data->xfer_len = 0;
    ahd_set_residual(scb, 0);
    ahd_set_sense_residual(scb, 0);
    scb->sg_count = 0;

    /* Get the scatter-gather list from StorPort */
    sgList = StorPortGetScatterGatherList(ext, Srb);

    if (sgList != NULL && sgList->NumberOfElements > 0) {
        void *sg = scb->sg_list;
        ULONG i;

        for (i = 0; i < sgList->NumberOfElements; i++) {
            dma_addr_t addr = (dma_addr_t)sgList->Elements[i].Address.QuadPart;
            bus_size_t len = (bus_size_t)sgList->Elements[i].Length;
            int last = (i == sgList->NumberOfElements - 1);

            scb->platform_data->xfer_len += len;
            sg = ahd_sg_setup(ahd, scb, sg, addr, len, last);
        }
    }

    /* Program the hardware's DMA view: first SG entry + SG list busaddr.
     * Without this the sequencer never learns where the SG list lives -
     * any real data phase then dies (UNEXP_BUSFREE / phantom status). */
    if (sgList != NULL && sgList->NumberOfElements > 0)
        ahd_setup_data_scb(ahd, scb);
    else
        ahd_setup_noxfer_scb(ahd, scb);

    {
        static ULONG dbg_q = 0;
        if (dbg_q < 8) { dbg_q++;
            struct ahd_dma_seg *s0 = (struct ahd_dma_seg *)scb->sg_list;
            AIC_DBGPRINT("aic79xx: Q#%lu cdb=%02x dp=%llx dc=%08x sgp=%08x "
                         "sg0a=%08x sg0l=%08x busaddr=%08x\n",
                         dbg_q, Srb->Cdb[0],
                         (unsigned long long)scb->hscb->dataptr,
                         scb->hscb->datacnt, scb->hscb->sgptr,
                         s0 ? ahd_le32toh(s0->addr) : 0,
                         s0 ? ahd_le32toh(s0->len) : 0,
                         scb->sg_list_busaddr); }
    }

    /* Add to pending list and queue to sequencer */
    LIST_INSERT_HEAD(&ahd->pending_scbs, scb, pending_links);
    scb->flags |= SCB_ACTIVE;
    ahd_queue_scb(ahd, scb);
    if (dbg_si <= 24)
        AIC_DBGPRINT("aic79xx: queued cdb=%02x/%d sg=%u\n",
                     Srb->Cdb[0], Srb->CdbLength,
                     sgList ? sgList->NumberOfElements : 0);

    return TRUE;
}

/****************************** HwInterrupt *************************************/

BOOLEAN
NTAPI
Aic79xx_HwInterrupt(
    _In_ PVOID DeviceExtension
)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)DeviceExtension;
    struct ahd_softc *ahd = &ext->softc;
    static ULONG dbg_isr = 0;
    BOOLEAN claimed;
    uint8_t pre_intstat = ahd_inb(ahd, INTSTAT);
    uint8_t pre_seqcode = (pre_intstat & 0x02) ? ahd_inb(ahd, SEQINTCODE) : 0;
    uint8_t pre_seqsrc  = ahd_inb(ahd, SEQINTSRC);

    /*
     * ahd_intr() processes sequencer interrupts and completes SCBs
     * via ahd_done(), which maps CAM status to SRB status and calls
     * StorPortNotification(RequestComplete).
     */
    claimed = ahd_intr(ahd) ? TRUE : FALSE;
    if (dbg_isr < 24) { dbg_isr++;
        AIC_DBGPRINT("aic79xx: ISR#%lu claimed=%u pre=%02x seqcode=%02x seqsrc=%02x\n",
                 dbg_isr, claimed, pre_intstat, pre_seqcode, pre_seqsrc); }
    return claimed;
}

/****************************** HwResetBus **************************************/

BOOLEAN
NTAPI
Aic79xx_HwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId
)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)DeviceExtension;
    struct ahd_softc *ahd = &ext->softc;

    UNREFERENCED_PARAMETER(PathId);

    DbgPrint("aic79xx: HwResetBus path=%lu\n", PathId);

    /* Issue a SCSI bus reset through the core */
    ahd_reset_channel(ahd, 'A', /*initiate_reset*/1);

    return TRUE;
}

/****************************** HwAdapterControl ********************************/

SCSI_ADAPTER_CONTROL_STATUS
NTAPI
Aic79xx_HwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters
)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)DeviceExtension;

    switch (ControlType) {
    case ScsiQuerySupportedControlTypes: {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST list =
            (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        ULONG i;
        for (i = 0; i < list->MaxControlType; i++) {
            list->SupportedTypeList[i] = TRUE;
        }
        break;
    }
    case ScsiStopAdapter: {
        struct ahd_softc *ahd = &ext->softc;
        ahd_intr_enable(ahd, FALSE);
        break;
    }
    case ScsiRestartAdapter: {
        struct ahd_softc *ahd = &ext->softc;
        ahd_reset(ahd, /*reinit*/1);
        ahd_intr_enable(ahd, TRUE);
        break;
    }
    case ScsiSetBootConfig:
    case ScsiSetRunningConfig:
        break;
    default:
        break;
    }

    return ScsiAdapterControlSuccess;
}

/****************************** PCI Config Access *******************************/

uint32_t
ahd_pci_read_config(ahd_dev_softc_t pci, int reg, int width)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)pci;
    PCI_COMMON_CONFIG pciConfig;
    ULONG bytesRead;

    bytesRead = StorPortGetBusData(
        pci,
        PCIConfiguration,
        ext->pci_bus,
        ext->pci_slot,
        &pciConfig,
        sizeof(PCI_COMMON_CONFIG)
    );

    if (bytesRead >= sizeof(PCI_COMMON_HEADER) && reg >= 0 && reg < (int)sizeof(PCI_COMMON_CONFIG)) {
        uint8_t *base = (uint8_t *)&pciConfig;
        uint32_t val = 0;
        if (width == 1) val = base[reg];
        else if (width == 2) val = *(uint16_t *)(base + reg);
        else val = *(uint32_t *)(base + reg);
        return val;
    }
    return 0;
}

void
ahd_pci_write_config(ahd_dev_softc_t pci, int reg, uint32_t value, int width)
{
    PAIC79XX_DEVICE_EXTENSION ext = (PAIC79XX_DEVICE_EXTENSION)pci;
    uint8_t val_buf[4];
    ULONG len;

    if (reg < 0 || reg + width > 256)
        return;

    if (width == 1) {
        val_buf[0] = (uint8_t)value;
        len = 1;
    } else if (width == 2) {
        *(uint16_t *)val_buf = (uint16_t)value;
        len = 2;
    } else {
        *(uint32_t *)val_buf = value;
        len = 4;
    }

    StorPortSetBusDataByOffset(
        pci,
        PCIConfiguration,
        ext->pci_bus,
        ext->pci_slot,
        val_buf,
        (ULONG)reg,
        len
    );
}

/****************************** DMA Stubs **************************************/

/*
 * StorPort uncached extension bump allocator.
 * The uncached extension is one physically contiguous block.
 * We carve DMA-coherent memory from it for hscb arrays, SG lists, and
 * sense buffers.  Bus address = base_phys + (va - base_va).
 */

static dma_addr_t
ahd_ucext_vtop(struct ahd_softc *ahd, void *vaddr)
{
    PAIC79XX_DEVICE_EXTENSION ext =
        (PAIC79XX_DEVICE_EXTENSION)ahd->platform_data->dev_ext;
    return ext->uncached_ext_phys +
           ((PUCHAR)vaddr - ext->uncached_ext_va);
}

static void *
ahd_ucext_alloc(struct ahd_softc *ahd, bus_size_t size, bus_size_t align)
{
    PAIC79XX_DEVICE_EXTENSION ext =
        (PAIC79XX_DEVICE_EXTENSION)ahd->platform_data->dev_ext;
    PUCHAR ptr = ext->uc_ext_ptr;
    PUCHAR aligned;
    ULONG  offset;

    /* Align the pointer */
    offset = (ULONG)((uintptr_t)ptr % align);
    if (offset != 0)
        ptr += (align - offset);

    if (ext->uc_ext_remain < size)
        return NULL;

    aligned = ptr;
    ext->uc_ext_ptr = ptr + size;
    ext->uc_ext_remain = (ULONG)((ext->uncached_ext_va + ext->uncached_ext_size)
                                  - ext->uc_ext_ptr);
    return aligned;
}

int
ahd_dma_tag_create(struct ahd_softc *ahd, bus_dma_tag_t parent,
                   bus_size_t alignment, bus_size_t boundary,
                   dma_addr_t lowaddr, dma_addr_t highaddr,
                   bus_dma_filter_t *filter, void *filterarg,
                   bus_size_t maxsize, int nsegments, bus_size_t maxsegsz,
                   int flags, bus_dma_tag_t *dmatagp)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(alignment);
    UNREFERENCED_PARAMETER(boundary);
    UNREFERENCED_PARAMETER(lowaddr);
    UNREFERENCED_PARAMETER(highaddr);
    UNREFERENCED_PARAMETER(filter);
    UNREFERENCED_PARAMETER(filterarg);
    UNREFERENCED_PARAMETER(maxsize);
    UNREFERENCED_PARAMETER(nsegments);
    UNREFERENCED_PARAMETER(maxsegsz);
    UNREFERENCED_PARAMETER(flags);

    /* StorPort manages DMA — return a dummy tag */
    *dmatagp = (bus_dma_tag_t)1;
    return 0;
}

void
ahd_dma_tag_destroy(struct ahd_softc *ahd, bus_dma_tag_t dmatag)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(dmatag);
}

int
ahd_dmamem_alloc(struct ahd_softc *ahd, bus_dma_tag_t dmatag,
                 void **vaddr, int flags, bus_dmamap_t *mapp)
{
    void *mem;

    UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(flags);

    /* Allocate PAGE_SIZE-aligned block from uncached extension */
    mem = ahd_ucext_alloc(ahd, PAGE_SIZE, PAGE_SIZE);
    if (mem == NULL)
        return (ENOMEM);

    *vaddr = mem;
    *mapp = 0;  /* no separate map needed */
    return (0);
}

void
ahd_dmamem_free(struct ahd_softc *ahd, bus_dma_tag_t dmatag,
                void *vaddr, bus_dmamap_t map)
{
    /* Bump allocator — no free.  Memory lives for driver lifetime. */
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(vaddr);
    UNREFERENCED_PARAMETER(map);
}

void
ahd_dmamap_destroy(struct ahd_softc *ahd, bus_dma_tag_t dmatag,
                   bus_dmamap_t map)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(map);
}

int
ahd_dmamap_load(struct ahd_softc *ahd, bus_dma_tag_t dmatag,
                bus_dmamap_t map, void *buf, bus_size_t buflen,
                bus_dmamap_callback_t *callback, void *cbarg, int flags)
{
    bus_dma_segment_t seg;

    UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(map);
    UNREFERENCED_PARAMETER(flags);

    /*
     * Memory from the uncached extension is physically contiguous,
     * so the entire buffer maps to a single segment.
     * Bus address = physical base + offset within extension.
     */
    seg.ds_addr = ahd_ucext_vtop(ahd, buf);
    seg.ds_len  = buflen;

    /* Invoke the callback (ahd_dmamap_cb) with the segment */
    callback(cbarg, &seg, /*nseg*/1, /*error*/0);
    return (0);
}

int
ahd_dmamap_unload(struct ahd_softc *ahd, bus_dma_tag_t dmatag,
                  bus_dmamap_t map)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(map);
    return (0);
}

/****************************** Platform Functions ******************************/

void
ahd_done(struct ahd_softc *ahd, struct scb *scb)
{
    PAIC79XX_DEVICE_EXTENSION ext;
    PSCSI_REQUEST_BLOCK srb;
    uint32_t cam_status;
    uint32_t scsi_status;

    if (scb == NULL || scb->platform_data == NULL)
        return;

    srb = scb->platform_data->srb;
    if (srb == NULL)
        return;

    ext = (PAIC79XX_DEVICE_EXTENSION)ahd->platform_data->dev_ext;

    /* Remove from pending list */
    LIST_REMOVE(scb, pending_links);

    cam_status  = ahd_get_transaction_status(scb);
    scsi_status = ahd_get_scsi_status(scb);

    /* Map CAM status to SRB status */
    if ((ULONG)cam_status != CAM_REQ_CMP &&
        (ULONG)cam_status != CAM_SEL_TIMEOUT &&
        (ULONG)cam_status != CAM_AUTOSENSE_FAIL) {
        static ULONG dbg_cs = 0;
        if (dbg_cs < 8) { dbg_cs++;
            AIC_DBGPRINT("aic79xx: DONE cam=%lu t=%d cdb=%02x flags=%lx "
                         "hscbctl=%02x sgc=%u xfer=%lu\n",
                         (ULONG)cam_status, srb->TargetId, srb->Cdb[0],
                         (ULONG)scb->flags, scb->hscb->control,
                         scb->sg_count,
                         (ULONG)scb->platform_data->xfer_len); }
    }
    switch (cam_status) {
    case CAM_REQ_INPROG:
    case CAM_REQ_CMP:
        srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;
    case CAM_REQUEUE_REQ:
        srb->SrbStatus = SRB_STATUS_BUSY;
        break;
    case CAM_SCSI_STATUS_ERROR:
        srb->SrbStatus = SRB_STATUS_SUCCESS;
        srb->ScsiStatus = (UCHAR)scsi_status;
        break;
    case CAM_SEL_TIMEOUT:
        srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
        break;
    case CAM_CMD_TIMEOUT:
        srb->SrbStatus = SRB_STATUS_TIMEOUT;
        break;
    case CAM_DATA_RUN_ERR:
        srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        break;
    case CAM_REQ_ABORTED:
        srb->SrbStatus = SRB_STATUS_ABORTED;
        break;
    case CAM_UNCOR_PARITY:
        srb->SrbStatus = SRB_STATUS_PARITY_ERROR;
        break;
    case CAM_SCSI_BUS_RESET:
        srb->SrbStatus = SRB_STATUS_BUS_RESET;
        break;
    case CAM_UNEXP_BUSFREE:
        srb->SrbStatus = SRB_STATUS_UNEXPECTED_BUS_FREE;
        break;
    case CAM_SEQUENCE_FAIL:
        srb->SrbStatus = SRB_STATUS_PHASE_SEQUENCE_FAILURE;
        break;
    case CAM_REQ_CMP_ERR:
        srb->SrbStatus = SRB_STATUS_ERROR;
        break;
    case CAM_BDR_SENT:
    case CAM_REQ_TERMIO:
        srb->SrbStatus = SRB_STATUS_ERROR;
        break;
    default:
        srb->SrbStatus = SRB_STATUS_ERROR;
        break;
    }

    /* Handle SCSI CHECK CONDITION / COMMAND TERMINATED */
    if ((cam_status == CAM_SCSI_STATUS_ERROR) &&
        (scsi_status == SAM_STAT_CHECK_CONDITION ||
         scsi_status == SAM_STAT_COMMAND_TERMINATED)) {
        srb->SrbStatus = SRB_STATUS_ERROR;
        srb->ScsiStatus = (UCHAR)scsi_status;

        /* Copy autosense data if the core acquired it */
        if ((scb->flags & (SCB_SENSE | SCB_PKT_SENSE)) != 0 &&
            srb->SenseInfoBuffer != NULL &&
            srb->SenseInfoBufferLength > 0) {
            ULONG senseLen = min(sizeof(struct scsi_sense_data),
                                 (size_t)srb->SenseInfoBufferLength);
            StorPortMoveMemory(srb->SenseInfoBuffer,
                               scb->sense_data, senseLen);
            /* Without this flag Storport ignores the sense buffer:
             * pending UNIT ATTENTIONs then stall enumeration forever */
            srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
        }
    }

    /* Core's internal autosense second pass lands here as CAM_AUTOSENSE_FAIL
     * when the REQUEST SENSE command itself fails. The scb->sense_data field
     * was never populated (core leaves it untouched; see ahd_handle_scsi_status
     * in aic79xx_core.c:8853-8863). Do not copy or claim validity of sense data. */
    if (cam_status == CAM_AUTOSENSE_FAIL) {
        srb->SrbStatus = SRB_STATUS_ERROR;
        srb->ScsiStatus = scb->hscb->shared_data.istatus.scsi_status
                             ? scb->hscb->shared_data.istatus.scsi_status
                             : SAM_STAT_CHECK_CONDITION;
    }

    /* Report actual bytes transferred, not the originally-requested length */
    {
        u_long resid = ahd_get_residual(scb);
        u_long xfer_len = ahd_get_transfer_length(scb);
        if (resid <= xfer_len)
            srb->DataTransferLength = (ULONG)(xfer_len - resid);
    }

    /* Clear SCB_ACTIVE before returning to pool */
    scb->flags &= ~SCB_ACTIVE;

    /* Release the SCB back to the pool */
    ahd_free_scb(ahd, scb);

    /* Complete the SRB back to StorPort */
    StorPortNotification(RequestComplete, ext, srb);

    /* Tell StorPort we are ready for the next request */
    StorPortNotification(NextRequest, ext);
    {
        static ULONG dbg_done = 0;
        if (dbg_done < 16) { dbg_done++;
            AIC_DBGPRINT("aic79xx: DONE#%lu t=%d l=%d srb=%02x scsi=%02x\n",
                         dbg_done, srb->TargetId, srb->Lun,
                         srb->SrbStatus, srb->ScsiStatus); }
    }
}

void
ahd_send_async(struct ahd_softc *ahd, char channel, u_int target,
               u_int lun, ac_code event)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(channel);
    UNREFERENCED_PARAMETER(target);
    UNREFERENCED_PARAMETER(lun);
    UNREFERENCED_PARAMETER(event);
}

void
ahd_print_path(struct ahd_softc *ahd, struct scb *scb)
{
    if (scb != NULL)
        AIC_DBGPRINT("%s: ccb -> Target %d LUN %d: ",
                                ahd_name(ahd),
                                SCB_GET_TARGET(ahd, scb),
                                SCB_GET_LUN(scb));
    else
        AIC_DBGPRINT("%s: ", ahd_name(ahd));
}

int
ahd_platform_alloc(struct ahd_softc *ahd, void *platform_arg)
{
    struct ahd_platform_data *pd;
    PAIC79XX_DEVICE_EXTENSION ext;

    UNREFERENCED_PARAMETER(platform_arg);

    /* Save the device extension before platform_data gets overwritten */
    ext = (PAIC79XX_DEVICE_EXTENSION)ahd->platform_data;

    pd = (struct ahd_platform_data *)
         ExAllocatePoolWithTag(NonPagedPool, sizeof(*pd), 'hcIA');
    if (pd == NULL)
        return 1;

    RtlZeroMemory(pd, sizeof(*pd));
    KeInitializeSpinLock(&pd->spin_lock);
    pd->dev_ext = ext;  /* Back-pointer for ahd_done() */
    pd->hw_device_base = ext->hw_device_base;
    pd->pci_bus  = ext->pci_bus;
    pd->pci_slot = ext->pci_slot;
    ahd->platform_data = pd;
    return 0;
}

void
ahd_platform_free(struct ahd_softc *ahd)
{
    if (ahd->platform_data != NULL) {
        ExFreePoolWithTag(ahd->platform_data, 'hcIA');
        ahd->platform_data = NULL;
    }
}

void
ahd_platform_init(struct ahd_softc *ahd)
{
    UNREFERENCED_PARAMETER(ahd);
}

void
ahd_platform_freeze_devq(struct ahd_softc *ahd, struct scb *scb)
{
    PAIC79XX_DEVICE_EXTENSION ext =
        (PAIC79XX_DEVICE_EXTENSION)ahd->platform_data->dev_ext;
    u_int target = SCB_GET_TARGET(ahd, scb);
    u_int lun = SCB_GET_LUN(scb);

    /* Don't pause for conditions StorPort/the OS class driver already retries
     * on its own: a plain selection timeout (no device present -- nothing to
     * "recover"), a routine CHECK CONDITION (core freezes the devq for ANY
     * non-GOOD SCSI status here -- including a benign first-access UNIT
     * ATTENTION, before sense data is even parsed), or a negotiation-reject
     * requeue (CAM_REQUEUE_REQ, routine WDTR/SDTR/PPR downgrade retry).
     * Pausing 30s for these routine, expected conditions just stalls device
     * discovery/mount. Genuine bus faults (parity errors, command timeouts,
     * etc.) surface via other status codes and still get the full pause. */
    switch (ahd_get_transaction_status(scb)) {
    case CAM_SEL_TIMEOUT:
    case CAM_SCSI_STATUS_ERROR:
    case CAM_REQUEUE_REQ:
        return;
    default:
        break;
    }

    /* Pause this device for 30 seconds to allow error recovery.
     * StorPort will not send new requests to this target/LUN
     * until the timeout expires or StorPortResumeDevice is called. */
    StorPortPauseDevice(ext, 0, (UCHAR)target, (UCHAR)lun, 30);
}

void
ahd_platform_set_tags(struct ahd_softc *ahd, struct scsi_device *sdev,
                      struct ahd_devinfo *devinfo, ahd_queue_alg alg)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(sdev);
    UNREFERENCED_PARAMETER(devinfo);
    UNREFERENCED_PARAMETER(alg);
}

int
ahd_platform_abort_scbs(struct ahd_softc *ahd, int target, char channel,
                        int lun, u_int tag, role_t role, uint32_t status)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(target);
    UNREFERENCED_PARAMETER(channel);
    UNREFERENCED_PARAMETER(lun);
    UNREFERENCED_PARAMETER(tag);
    UNREFERENCED_PARAMETER(role);
    UNREFERENCED_PARAMETER(status);
    return 0;
}

void
ahd_power_state_change(struct ahd_softc *ahd, ahd_power_state new_state)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(new_state);
}

void
ahd_freeze_simq(struct ahd_softc *ahd)
{
    UNREFERENCED_PARAMETER(ahd);
    /* No-op: StorPort single-SRB model inherently serializes I/O,
     * and ahd_done completes one request at a time. */
}

void
ahd_release_simq(struct ahd_softc *ahd)
{
    UNREFERENCED_PARAMETER(ahd);
}

/****************************** End of File *************************************/
