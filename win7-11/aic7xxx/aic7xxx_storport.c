/*
 * Windows StorPort Miniport glue for AIC7xxx (U160 SCSI).
 * Implements StorPort entry points, platform functions, DMA stubs,
 * PCI config access, and debug print infrastructure.
 *
 * The core (aic7xxx_core.c) stays unmodified. This file provides
 * all Windows-specific implementations declared in aic7xxx_osm.h.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */

#include "aic7xxx_win.h"
#include "aic7xxx_inline.h"

void *ahc_sg_setup(struct ahc_softc *ahc, struct scb *scb,
                   void *sgptr, dma_addr_t addr, bus_size_t len, int last);

/****************************** Global State ***********************************/

uint32_t aic7xxx_verbose = 1;
uint32_t aic7xxx_slowcrc = 0;

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

    hwInitData.HwInitialize    = Aic7xxx_HwInitialize;
    hwInitData.HwFindAdapter   = Aic7xxx_HwFindAdapter;
    hwInitData.HwStartIo       = Aic7xxx_HwStartIo;
    hwInitData.HwInterrupt     = Aic7xxx_HwInterrupt;
    hwInitData.HwResetBus      = Aic7xxx_HwResetBus;
    hwInitData.HwAdapterControl = Aic7xxx_HwAdapterControl;

    hwInitData.TaggedQueuing = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;
    hwInitData.DeviceExtensionSize = sizeof(AHC7XXX_DEVICE_EXTENSION);

    /* PCI adapter: memory-mapped registers + I/O ports (see LSI U3 sample) */
    hwInitData.NumberOfAccessRanges = 2;
    hwInitData.NeedPhysicalAddresses = TRUE;

    hwInitData.MapBuffers = STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;

    status = StorPortInitialize(DriverObject, RegistryPath, &hwInitData, NULL);
    AicTrace(AIC_STEP_STORPORT_INIT_RET, status);

    return status;
}

/****************************** HwFindAdapter **********************************/

ULONG
NTAPI
Aic7xxx_HwFindAdapter(
    _In_  PVOID DeviceExtension,
    _In_  PVOID HwContext,
    _In_  PVOID BusInformation,
    _In_  PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Again
)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)DeviceExtension;
    struct ahc_softc *ahc = &ext->softc;
    const struct ahc_pci_identity *entry;
    PCI_COMMON_CONFIG pciConfig;
    ULONG bytesRead;
    int error;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    *Again = FALSE;

    StorPortZeroMemory(ext, sizeof(AHC7XXX_DEVICE_EXTENSION));

    AicTrace(AIC_STEP_FIND_ENTER, STATUS_SUCCESS);

    ext->pci_bus  = ConfigInfo->SystemIoBusNumber;
    ext->pci_slot = ConfigInfo->SlotNumber;

    bytesRead = StorPortGetBusData(
        ext, PCIConfiguration, ext->pci_bus, ext->pci_slot,
        &pciConfig, sizeof(PCI_COMMON_CONFIG));

    if (bytesRead < sizeof(PCI_COMMON_HEADER)) {
        DbgPrint("aic7xxx: Failed to read PCI config\n");
        AicTrace(AIC_STEP_PCI_CFG_READ, STATUS_UNSUCCESSFUL);
        return SP_RETURN_ERROR;
    }

    {
        uint16_t cmd = pciConfig.Command | PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE;
        StorPortSetBusDataByOffset(
            ext, PCIConfiguration, ext->pci_bus, ext->pci_slot,
            &cmd, PCIR_COMMAND, sizeof(cmd));
    }

    /* Map device registers from the TRANSLATED AccessRanges (see aic79xx) */
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
        DbgPrint("aic7xxx: Failed to map device registers\n");
        AicTrace(AIC_STEP_REGS_MAPPED, STATUS_UNSUCCESSFUL);
        return SP_RETURN_ERROR;
    }

    ext->uncached_ext_size = 64 * 1024;
    ext->uncached_ext_va = (PUCHAR)StorPortGetUncachedExtension(
        ext, ConfigInfo, ext->uncached_ext_size);

    if (ext->uncached_ext_va == NULL) {
        DbgPrint("aic7xxx: Failed to allocate uncached extension\n");
        AicTrace(AIC_STEP_UNCACHED_ALLOC, STATUS_UNSUCCESSFUL);
        return SP_RETURN_ERROR;
    }

    {
        SCSI_PHYSICAL_ADDRESS physBase;
        ULONG physLen;
        physBase = StorPortGetPhysicalAddress(ext, NULL, ext->uncached_ext_va, &physLen);
        ext->uncached_ext_phys = (dma_addr_t)physBase.QuadPart;
    }

    ahc->dev_softc = ext;

    ahc->seep_config = (struct seeprom_config *)
         ExAllocatePoolWithTag(NonPagedPool, sizeof(struct seeprom_config), 'xcIA');
    if (ahc->seep_config == NULL) {
        DbgPrint("aic7xxx: Failed to allocate SEEPROM config\n");
        AicTrace(AIC_STEP_SOFTC_INIT, STATUS_INSUFFICIENT_RESOURCES);
        return SP_RETURN_ERROR;
    }
    StorPortZeroMemory(ahc->seep_config, sizeof(struct seeprom_config));

    LIST_INIT(&ahc->pending_scbs);

    ahc->name = "aic7xxx";
    ahc->unit = 0;
    ahc->description = NULL;
    ahc->channel = 'A';
    ahc->chip = AHC_NONE;
    ahc->features = AHC_FENONE;
    ahc->bugs = AHC_BUGNONE;
    ahc->flags = AHC_FENONE;

    ahc->platform_data = (struct ahc_platform_data *)ext;
    ahc_platform_alloc(ahc, NULL);

    ext->uc_ext_ptr = ext->uncached_ext_va;
    ext->uc_ext_remain = ext->uncached_ext_size;

    entry = ahc_find_pci_device(ahc->dev_softc);
    if (entry == NULL) {
        DbgPrint("aic7xxx: Unknown PCI device\n");
        AicTrace(AIC_STEP_SOFTC_INIT, 0xE0001001); /* custom: no PCI identity */
        return SP_RETURN_ERROR;
    }

    DbgPrint("aic7xxx: Found %s\n", entry->name);

    error = ahc_pci_config(ahc, entry);
    if (error != 0) {
        DbgPrint("aic7xxx: ahc_pci_config failed (%d)\n", error);
        AicTrace(AIC_STEP_SOFTC_INIT, (NTSTATUS)(0xE0002000 | (error & 0xFFF)));
        return SP_RETURN_ERROR;
    }

    ConfigInfo->NumberOfBuses             = 1;         /* one SCSI bus - required for LUN enumeration */
    ConfigInfo->MaximumTransferLength     = 0x100000;
    ConfigInfo->NumberOfPhysicalBreaks     = AHC_NSEG;
    ConfigInfo->MaximumNumberOfTargets     = 16;
    ConfigInfo->MaximumNumberOfLogicalUnits = 8;
    ConfigInfo->InitiatorBusId[0]          = (UCHAR)ahc->our_id;

    DbgPrint("aic7xxx: HwFindAdapter complete, bus=%lu slot=%lu base=0x%p\n",
              ext->pci_bus, ext->pci_slot, ext->hw_device_base);

    /* Chip comes up with interrupts masked; unmask for Storport */
    ahc_intr_enable(ahc, TRUE);

    AicTrace(AIC_STEP_FOUND, STATUS_SUCCESS);

    return SP_RETURN_FOUND;
}

/****************************** HwInitialize ***********************************/

BOOLEAN NTAPI Aic7xxx_HwInitialize(_In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    return TRUE;
}

/* The aic7xxx core expects the port layer to program the DMA view of each
 * transaction into the HSCB (see the core's own REQUEST SENSE path). */
static void
ahc_setup_data_scb(struct ahc_softc *ahc, struct scb *scb)
{
    struct ahc_dma_seg *sg = (struct ahc_dma_seg *)scb->sg_list;

    UNREFERENCED_PARAMETER(ahc);
    scb->hscb->dataptr = sg->addr;
    scb->hscb->datacnt = sg->len;
    scb->hscb->sgptr   = ahc_htole32(scb->sg_list_phys | SG_FULL_RESID);
}

static void
ahc_setup_noxfer_scb(struct ahc_softc *ahc, struct scb *scb)
{
    UNREFERENCED_PARAMETER(ahc);
    scb->hscb->sgptr   = ahc_htole32(SG_LIST_NULL);
    scb->hscb->dataptr = 0;
    scb->hscb->datacnt = 0;
}

/****************************** HwStartIo **************************************/

BOOLEAN NTAPI
Aic7xxx_HwStartIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)DeviceExtension;
    struct ahc_softc *ahc = &ext->softc;
    struct scb *scb;
    struct hardware_scb *hscb;
    uint8_t *cdb;
    uint8_t cdb_len;
    PSCATTER_GATHER_LIST sgList;
    static ULONG dbg_si = 0, dbg_nonx = 0;

    if (dbg_si < 24) {
        dbg_si++;
        AIC_DBGPRINT("aic7xxx: SI#%lu fn=%02x t=%d l=%d len=%lx\n",
                     dbg_si, Srb->Function, Srb->TargetId, Srb->Lun,
                     Srb->DataTransferLength);
    }
    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
        if (dbg_nonx < 8) { dbg_nonx++;
            AIC_DBGPRINT("aic7xxx: non-EXECUTE fn=%02x\n", Srb->Function); }
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        StorPortNotification(RequestComplete, ext, Srb);
        StorPortNotification(NextRequest, ext);
        return TRUE;
    }

    scb = ahc_get_scb(ahc);
    if (scb == NULL) {
        Srb->SrbStatus = SRB_STATUS_BUSY;
        StorPortNotification(RequestComplete, ext, Srb);
        StorPortNotification(NextRequest, ext);
        return TRUE;
    }

    scb->platform_data->srb = Srb;
    scb->io_ctx = &scb->platform_data->shadow_cmd;
    scb->platform_data->shadow_cmd.device = &scb->platform_data->shadow_dev;
    scb->platform_data->shadow_dev.qfrozen = 0;

    {
        struct scsi_cmnd *cmd = scb->io_ctx;
        cmd->result = (CAM_REQ_INPROG << 16);
        cmd->sc_data_direction = Srb->SrbFlags & SRB_FLAGS_DATA_IN ? 1 : 2;
        cmd->resid = 0;
    }

    hscb = scb->hscb;
    hscb->control = DISCENB | SIMPLE_QUEUE_TAG;
    hscb->scsiid = ((Srb->TargetId << TID_SHIFT) & TID) | ahc->our_id;
    hscb->lun = Srb->Lun;

    cdb = Srb->Cdb;
    cdb_len = Srb->CdbLength;
    hscb->cdb_len = cdb_len;
    memcpy(hscb->shared_data.cdb, cdb, cdb_len);

    scb->platform_data->xfer_len = 0;
    ahc_set_residual(scb, 0);
    ahc_set_sense_residual(scb, 0);
    scb->sg_count = 0;

    sgList = StorPortGetScatterGatherList(ext, Srb);
    if (sgList != NULL && sgList->NumberOfElements > 0) {
        void *sg = scb->sg_list;
        ULONG i;
        for (i = 0; i < sgList->NumberOfElements; i++) {
            dma_addr_t addr = (dma_addr_t)sgList->Elements[i].Address.QuadPart;
            bus_size_t len = (bus_size_t)sgList->Elements[i].Length;
            int last = (i == sgList->NumberOfElements - 1);
            scb->platform_data->xfer_len += len;
            sg = ahc_sg_setup(ahc, scb, sg, addr, len, last);
        }
    }

    LIST_INSERT_HEAD(&ahc->pending_scbs, scb, pending_links);
    scb->flags |= SCB_ACTIVE;
    if (sgList != NULL && sgList->NumberOfElements > 0)
        ahc_setup_data_scb(ahc, scb);
    else
        ahc_setup_noxfer_scb(ahc, scb);

    ahc_queue_scb(ahc, scb);
    return TRUE;
}

/****************************** HwInterrupt *************************************/

BOOLEAN NTAPI Aic7xxx_HwInterrupt(_In_ PVOID DeviceExtension)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)DeviceExtension;
    struct ahc_softc *ahc = &ext->softc;
    static ULONG dbg_isr = 0;
    BOOLEAN claimed = ahc_intr(ahc) ? TRUE : FALSE;
    if (dbg_isr < 12) { dbg_isr++;
        AIC_DBGPRINT("aic7xxx: ISR#%lu claimed=%u intstat=%02x\n",
                     dbg_isr, claimed, ahc_inb(ahc, INTSTAT)); }
    return claimed;
}

/****************************** HwResetBus **************************************/

BOOLEAN NTAPI Aic7xxx_HwResetBus(_In_ PVOID DeviceExtension, _In_ ULONG PathId)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)DeviceExtension;
    struct ahc_softc *ahc = &ext->softc;
    UNREFERENCED_PARAMETER(PathId);
    DbgPrint("aic7xxx: HwResetBus path=%lu\n", PathId);
    ahc_reset_channel(ahc, 'A', 1);
    return TRUE;
}

/****************************** HwAdapterControl ********************************/

SCSI_ADAPTER_CONTROL_STATUS NTAPI
Aic7xxx_HwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters
)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)DeviceExtension;

    switch (ControlType) {
    case ScsiQuerySupportedControlTypes: {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST list =
            (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        ULONG i;
        for (i = 0; i < list->MaxControlType; i++)
            list->SupportedTypeList[i] = TRUE;
        break;
    }
    case ScsiStopAdapter: {
        struct ahc_softc *ahc = &ext->softc;
        ahc_intr_enable(ahc, FALSE);
        break;
    }
    case ScsiRestartAdapter: {
        struct ahc_softc *ahc = &ext->softc;
        ahc_reset(ahc, 1);
        ahc_intr_enable(ahc, TRUE);
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
ahc_pci_read_config(ahc_dev_softc_t pci, int reg, int width)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)pci;
    PCI_COMMON_CONFIG pciConfig;
    ULONG bytesRead;

    bytesRead = StorPortGetBusData(
        pci, PCIConfiguration, ext->pci_bus, ext->pci_slot,
        &pciConfig, sizeof(PCI_COMMON_CONFIG));

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
ahc_pci_write_config(ahc_dev_softc_t pci, int reg, uint32_t value, int width)
{
    PAHC7XXX_DEVICE_EXTENSION ext = (PAHC7XXX_DEVICE_EXTENSION)pci;
    uint8_t val_buf[4];
    ULONG len;

    if (reg < 0 || reg + width > 256)
        return;

    if (width == 1) { val_buf[0] = (uint8_t)value; len = 1; }
    else if (width == 2) { *(uint16_t *)val_buf = (uint16_t)value; len = 2; }
    else { *(uint32_t *)val_buf = value; len = 4; }

    StorPortSetBusDataByOffset(pci, PCIConfiguration, ext->pci_bus,
        ext->pci_slot, val_buf, (ULONG)reg, len);
}

/****************************** DMA Stubs **************************************/

static dma_addr_t
ahc_ucext_vtop(struct ahc_softc *ahc, void *vaddr)
{
    PAHC7XXX_DEVICE_EXTENSION ext =
        (PAHC7XXX_DEVICE_EXTENSION)ahc->platform_data->dev_ext;
    return ext->uncached_ext_phys +
           ((PUCHAR)vaddr - ext->uncached_ext_va);
}

static void *
ahc_ucext_alloc(struct ahc_softc *ahc, bus_size_t size, bus_size_t align)
{
    PAHC7XXX_DEVICE_EXTENSION ext =
        (PAHC7XXX_DEVICE_EXTENSION)ahc->platform_data->dev_ext;
    PUCHAR ptr = ext->uc_ext_ptr;
    PUCHAR aligned;
    ULONG  offset;

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
ahc_dma_tag_create(struct ahc_softc *ahc, bus_dma_tag_t parent,
                   bus_size_t alignment, bus_size_t boundary,
                   dma_addr_t lowaddr, dma_addr_t highaddr,
                   bus_dma_filter_t *filter, void *filterarg,
                   bus_size_t maxsize, int nsegments, bus_size_t maxsegsz,
                   int flags, bus_dma_tag_t *dmatagp)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(alignment); UNREFERENCED_PARAMETER(boundary);
    UNREFERENCED_PARAMETER(lowaddr); UNREFERENCED_PARAMETER(highaddr);
    UNREFERENCED_PARAMETER(filter); UNREFERENCED_PARAMETER(filterarg);
    UNREFERENCED_PARAMETER(maxsize); UNREFERENCED_PARAMETER(nsegments);
    UNREFERENCED_PARAMETER(maxsegsz); UNREFERENCED_PARAMETER(flags);
    *dmatagp = (bus_dma_tag_t)1;
    return 0;
}

void ahc_dma_tag_destroy(struct ahc_softc *ahc, bus_dma_tag_t dmatag)
{ UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(dmatag); }

int
ahc_dmamem_alloc(struct ahc_softc *ahc, bus_dma_tag_t dmatag,
                 void **vaddr, int flags, bus_dmamap_t *mapp)
{
    void *mem;
    UNREFERENCED_PARAMETER(dmatag); UNREFERENCED_PARAMETER(flags);
    mem = ahc_ucext_alloc(ahc, PAGE_SIZE, PAGE_SIZE);
    if (mem == NULL) return (ENOMEM);
    *vaddr = mem;
    *mapp = 0;
    return (0);
}

void
ahc_dmamem_free(struct ahc_softc *ahc, bus_dma_tag_t dmatag,
                void *vaddr, bus_dmamap_t map)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(vaddr); UNREFERENCED_PARAMETER(map);
}

void
ahc_dmamap_destroy(struct ahc_softc *ahc, bus_dma_tag_t dmatag,
                   bus_dmamap_t map)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(map);
}

int
ahc_dmamap_load(struct ahc_softc *ahc, bus_dma_tag_t dmatag,
                bus_dmamap_t map, void *buf, bus_size_t buflen,
                bus_dmamap_callback_t *callback, void *cbarg, int flags)
{
    bus_dma_segment_t seg;
    UNREFERENCED_PARAMETER(dmatag); UNREFERENCED_PARAMETER(map);
    UNREFERENCED_PARAMETER(flags);
    seg.ds_addr = ahc_ucext_vtop(ahc, buf);
    seg.ds_len  = buflen;
    callback(cbarg, &seg, 1, 0);
    return (0);
}

int
ahc_dmamap_unload(struct ahc_softc *ahc, bus_dma_tag_t dmatag,
                  bus_dmamap_t map)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(dmatag);
    UNREFERENCED_PARAMETER(map);
    return (0);
}

/****************************** Platform Functions ******************************/

void *
ahc_sg_setup(struct ahc_softc *ahc, struct scb *scb,
             void *sgptr, dma_addr_t addr, bus_size_t len, int last)
{
    struct ahc_dma_seg *sg = (struct ahc_dma_seg *)sgptr;

    UNREFERENCED_PARAMETER(ahc);

    scb->sg_count++;
    sg->addr = ahc_htole32(addr & 0xFFFFFFFF);
    sg->len  = ahc_htole32(len | ((addr >> 8) & AHC_SG_HIGH_ADDR_MASK)
                        | (last ? AHC_DMA_LAST_SEG : 0));
    return (sg + 1);
}

void
ahc_done(struct ahc_softc *ahc, struct scb *scb)
{
    PAHC7XXX_DEVICE_EXTENSION ext;
    PSCSI_REQUEST_BLOCK srb;
    uint32_t cam_status;
    uint32_t scsi_status;

    if (scb == NULL || scb->platform_data == NULL)
        return;

    srb = scb->platform_data->srb;
    if (srb == NULL)
        return;

    ext = (PAHC7XXX_DEVICE_EXTENSION)ahc->platform_data->dev_ext;

    LIST_REMOVE(scb, pending_links);

    cam_status  = ahc_get_transaction_status(scb);
    scsi_status = ahc_get_scsi_status(scb);

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

    if ((cam_status == CAM_SCSI_STATUS_ERROR) &&
        (scsi_status == SAM_STAT_CHECK_CONDITION ||
         scsi_status == SAM_STAT_COMMAND_TERMINATED)) {
        srb->SrbStatus = SRB_STATUS_ERROR;
        srb->ScsiStatus = (UCHAR)scsi_status;
        if ((scb->flags & SCB_SENSE) != 0 &&
            srb->SenseInfoBuffer != NULL &&
            srb->SenseInfoBufferLength > 0) {
            struct scsi_sense_data *sense_buf = ahc_get_sense_buf(ahc, scb);
            ULONG senseLen = min(sizeof(struct scsi_sense_data),
                                 (size_t)srb->SenseInfoBufferLength);
            StorPortMoveMemory(srb->SenseInfoBuffer,
                               sense_buf, senseLen);
            /* Without this flag Storport ignores the sense buffer:
             * pending UNIT ATTENTIONs then stall enumeration forever */
            srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
        }
    }

    /* Core's internal autosense second pass lands here as CAM_AUTOSENSE_FAIL
     * when the REQUEST SENSE command itself fails. The sense_buf field
     * was never populated (core leaves it untouched). Do not copy or claim
     * validity of sense data. */
    if (cam_status == CAM_AUTOSENSE_FAIL) {
        srb->SrbStatus = SRB_STATUS_ERROR;
        srb->ScsiStatus = ahc_get_scsi_status(scb)
                             ? ahc_get_scsi_status(scb)
                             : SAM_STAT_CHECK_CONDITION;
    }

    /* Report actual bytes transferred, not the originally-requested length */
    {
        u_long resid = ahc_get_residual(scb);
        u_long xfer_len = ahc_get_transfer_length(scb);
        if (resid <= xfer_len)
            srb->DataTransferLength = (ULONG)(xfer_len - resid);
    }

    scb->flags &= ~SCB_ACTIVE;
    ahc_free_scb(ahc, scb);
    StorPortNotification(RequestComplete, ext, srb);
    StorPortNotification(NextRequest, ext);
    {
        static ULONG dbg_done = 0;
        if (dbg_done < 16) { dbg_done++;
            AIC_DBGPRINT("aic7xxx: DONE#%lu t=%d l=%d srb=%02x scsi=%02x\n",
                         dbg_done, srb->TargetId, srb->Lun,
                         srb->SrbStatus, srb->ScsiStatus); }
    }
}

void
ahc_send_async(struct ahc_softc *ahc, char channel, u_int target,
               u_int lun, ac_code event)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(channel);
    UNREFERENCED_PARAMETER(target); UNREFERENCED_PARAMETER(lun);
    UNREFERENCED_PARAMETER(event);
}

void
ahc_print_path(struct ahc_softc *ahc, struct scb *scb)
{
    if (scb != NULL)
        AIC_DBGPRINT("%s: ccb -> Target %d LUN %d: ",
                     ahc_name(ahc), SCB_GET_TARGET(ahc, scb), SCB_GET_LUN(scb));
    else
        AIC_DBGPRINT("%s: ", ahc_name(ahc));
}

int
ahc_platform_alloc(struct ahc_softc *ahc, void *platform_arg)
{
    struct ahc_platform_data *pd;
    PAHC7XXX_DEVICE_EXTENSION ext;

    UNREFERENCED_PARAMETER(platform_arg);

    ext = (PAHC7XXX_DEVICE_EXTENSION)ahc->platform_data;

    pd = (struct ahc_platform_data *)
         ExAllocatePoolWithTag(NonPagedPool, sizeof(*pd), 'xcIA');
    if (pd == NULL)
        return 1;

    RtlZeroMemory(pd, sizeof(*pd));
    KeInitializeSpinLock(&pd->spin_lock);
    pd->dev_ext = ext;
    pd->hw_device_base = ext->hw_device_base;
    pd->pci_bus  = ext->pci_bus;
    pd->pci_slot = ext->pci_slot;
    ahc->platform_data = pd;
    return 0;
}

void
ahc_platform_free(struct ahc_softc *ahc)
{
    if (ahc->platform_data != NULL) {
        ExFreePoolWithTag(ahc->platform_data, 'xcIA');
        ahc->platform_data = NULL;
    }
}

void ahc_platform_init(struct ahc_softc *ahc)
{ UNREFERENCED_PARAMETER(ahc); }

void
ahc_platform_freeze_devq(struct ahc_softc *ahc, struct scb *scb)
{
    PAHC7XXX_DEVICE_EXTENSION ext =
        (PAHC7XXX_DEVICE_EXTENSION)ahc->platform_data->dev_ext;
    u_int target = SCB_GET_TARGET(ahc, scb);
    u_int lun = SCB_GET_LUN(scb);

    /* Don't pause for conditions StorPort/the OS class driver already retries
     * on its own: a plain selection timeout (no device present -- nothing to
     * "recover"), a routine CHECK CONDITION (core freezes the devq for ANY
     * non-GOOD SCSI status here -- including a benign first-access UNIT
     * ATTENTION, before sense data is even parsed), or a negotiation-reject
     * requeue (CAM_REQUEUE_REQ, routine WDTR/SDTR downgrade retry).
     * Pausing 30s for these routine, expected conditions just stalls device
     * discovery/mount. Genuine bus faults (parity errors, command timeouts,
     * etc.) surface via other status codes and still get the full pause. */
    switch (ahc_get_transaction_status(scb)) {
    case CAM_SEL_TIMEOUT:
    case CAM_SCSI_STATUS_ERROR:
    case CAM_REQUEUE_REQ:
        return;
    default:
        break;
    }

    StorPortPauseDevice(ext, 0, (UCHAR)target, (UCHAR)lun, 30);
}

void
ahc_platform_set_tags(struct ahc_softc *ahc, struct scsi_device *sdev,
                      struct ahc_devinfo *devinfo, ahc_queue_alg alg)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(sdev);
    UNREFERENCED_PARAMETER(devinfo); UNREFERENCED_PARAMETER(alg);
}

int
ahc_platform_abort_scbs(struct ahc_softc *ahc, int target, char channel,
                        int lun, u_int tag, role_t role, uint32_t status)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(target);
    UNREFERENCED_PARAMETER(channel); UNREFERENCED_PARAMETER(lun);
    UNREFERENCED_PARAMETER(tag); UNREFERENCED_PARAMETER(role);
    UNREFERENCED_PARAMETER(status);
    return 0;
}

void
ahc_platform_flushwork(struct ahc_softc *ahc)
{ UNREFERENCED_PARAMETER(ahc); }

void
ahc_power_state_change(struct ahc_softc *ahc, ahc_power_state new_state)
{
    UNREFERENCED_PARAMETER(ahc); UNREFERENCED_PARAMETER(new_state);
}

void ahc_freeze_simq(struct ahc_softc *ahc)
{ UNREFERENCED_PARAMETER(ahc); }

void ahc_release_simq(struct ahc_softc *ahc)
{ UNREFERENCED_PARAMETER(ahc); }

/****************************** PCI Helper Functions ****************************/

int ahc_get_pci_function(ahc_dev_softc_t pci)
{
    struct ahc_softc *ahc = pci;
    return (int)(ahc->platform_data->pci_slot & 0x07);
}

int ahc_get_pci_slot(ahc_dev_softc_t pci)
{
    struct ahc_softc *ahc = pci;
    return (int)(ahc->platform_data->pci_slot >> 3);
}

int ahc_get_pci_bus(ahc_dev_softc_t pci)
{
    struct ahc_softc *ahc = pci;
    return (int)ahc->platform_data->pci_bus;
}

void pci_set_power_state(ahc_dev_softc_t pci, ahc_power_state state)
{
    UNREFERENCED_PARAMETER(pci); UNREFERENCED_PARAMETER(state);
}

int ahc_pci_map_registers(struct ahc_softc *ahc)
{
    ahc->bsh = (bus_space_handle_t)ahc->platform_data->hw_device_base;
    return (0);
}

int ahc_pci_map_int(struct ahc_softc *ahc)
{
    return (0);
}

/****************************** End of File *************************************/
