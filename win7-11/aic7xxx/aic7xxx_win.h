/*
 * Windows types and HW_DEVICE_EXTENSION for AIC7xxx (U160) StorPort miniport.
 * Most types are in aic7xxx_osm.h; this header defines only
 * the StorPort-specific structures.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AIC7XXX_WIN_H_
#define _AIC7XXX_WIN_H_

#include "aic7xxx_osm.h"

/*
 * Hardware Device Extension — StorPort passes this as HwDeviceExtension
 * to all miniport callbacks. We embed the ahc_softc inside.
 *
 * The ahc_softc structure is defined in aic7xxx.h and contains
 * all chip state: register mappings, SCB pool, sequencer state, etc.
 * StorPortGetUncachedExtension provides DMA-coherent memory.
 */
typedef struct _AHC7XXX_DEVICE_EXTENSION {
    struct ahc_softc    softc;          /* must be first — core uses pointer casts */
    PVOID               hw_device_base; /* StorPortGetDeviceBase result */
    ULONG               uncached_ext_size;
    PUCHAR              uncached_ext_va; /* uncached extension virtual address */
    dma_addr_t          uncached_ext_phys; /* uncached extension physical address */
    PUCHAR              uc_ext_ptr;     /* bump allocator current pointer */
    ULONG               uc_ext_remain;  /* bump allocator remaining bytes */
    ULONG               pci_bus;        /* PCI bus number from ConfigInfo */
    ULONG               pci_slot;       /* PCI slot (device/function) from ConfigInfo */
} AHC7XXX_DEVICE_EXTENSION, *PAHC7XXX_DEVICE_EXTENSION;

/*
 * StorPort miniport function prototypes.
 */
BOOLEAN NTAPI Aic7xxx_HwInitialize(PVOID);
ULONG NTAPI
Aic7xxx_HwFindAdapter(PVOID, PVOID, PVOID, PCHAR,
                       PPORT_CONFIGURATION_INFORMATION, PBOOLEAN);
BOOLEAN NTAPI Aic7xxx_HwStartIo(PVOID, PSCSI_REQUEST_BLOCK);
BOOLEAN NTAPI Aic7xxx_HwInterrupt(PVOID);
BOOLEAN NTAPI Aic7xxx_HwResetBus(PVOID, ULONG);
SCSI_ADAPTER_CONTROL_STATUS NTAPI
Aic7xxx_HwAdapterControl(PVOID, SCSI_ADAPTER_CONTROL_TYPE, PVOID);

#endif /* _AIC7XXX_WIN_H_ */
