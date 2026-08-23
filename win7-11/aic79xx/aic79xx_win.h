/*
 * Windows types and HW_DEVICE_EXTENSION for AIC79xx StorPort miniport.
 * Most types are in aic79xx_osm.h; this header defines only
 * the StorPort-specific structures.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AIC79XX_WIN_H_
#define _AIC79XX_WIN_H_

#include "aic79xx_osm.h"

/*
 * Hardware Device Extension — StorPort passes this as HwDeviceExtension
 * to all miniport callbacks. We embed the ahd_softc inside.
 *
 * The ahd_softc structure is defined in aic79xx.h and contains
 * all chip state: register mappings, SCB pool, sequencer state, etc.
 * StorPortGetUncachedExtension provides DMA-coherent memory.
 */
typedef struct _AIC79XX_DEVICE_EXTENSION {
    struct ahd_softc    softc;          /* must be first — core uses pointer casts */
    PVOID               hw_device_base; /* StorPortGetDeviceBase result */
    ULONG               uncached_ext_size;
    PUCHAR              uncached_ext_va; /* uncached extension virtual address */
    dma_addr_t          uncached_ext_phys; /* uncached extension physical address */
    PUCHAR              uc_ext_ptr;     /* bump allocator current pointer */
    ULONG               uc_ext_remain;  /* bump allocator remaining bytes */
    ULONG               pci_bus;        /* PCI bus number from ConfigInfo */
    ULONG               pci_slot;       /* PCI slot (device/function) from ConfigInfo */
} AIC79XX_DEVICE_EXTENSION, *PAIC79XX_DEVICE_EXTENSION;

/*
 * StorPort miniport function prototypes.
 * DDK defines PHW_INITIALIZE etc. as function pointer typedefs;
 * we declare the actual functions matching those signatures.
 */
BOOLEAN NTAPI Aic79xx_HwInitialize(PVOID);
ULONG NTAPI
Aic79xx_HwFindAdapter(PVOID, PVOID, PVOID, PCHAR,
                       PPORT_CONFIGURATION_INFORMATION, PBOOLEAN);
BOOLEAN NTAPI Aic79xx_HwStartIo(PVOID, PSCSI_REQUEST_BLOCK);
BOOLEAN NTAPI Aic79xx_HwInterrupt(PVOID);
BOOLEAN NTAPI Aic79xx_HwResetBus(PVOID, ULONG);
SCSI_ADAPTER_CONTROL_STATUS NTAPI
Aic79xx_HwAdapterControl(PVOID, SCSI_ADAPTER_CONTROL_TYPE, PVOID);

#endif /* _AIC79XX_WIN_H_ */
