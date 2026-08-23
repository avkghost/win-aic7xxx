/*
 * Windows StorPort OSM (OS Module) header for AIC7xxx (Fast/U160).
 * Replaces Linux aic7xxx_osm.h -- provides all type definitions,
 * constants, macros, and inline functions the core needs.
 *
 * The core (aic7xxx_core.c) stays unmodified. This header is the
 * sole adaptation layer between Linux core code and Windows StorPort.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AIC7XXX_OSM_WIN_H_
#define _AIC7XXX_OSM_WIN_H_

#pragma warning(disable:4100)
#pragma warning(disable:4127)
#pragma warning(disable:4201)
#pragma warning(disable:4213)
#pragma warning(disable:4214)
#pragma warning(disable:4701)
#pragma warning(disable:4706)
#pragma warning(disable:4996)

/* VS/WDK already defines these on the command line (CharacterSet=Unicode) */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <ntddk.h>
#include <storport.h>

/* w32api spells it STORPORTAPI, genuine WDK uses STORPORT_API */
#ifndef STORPORTAPI
#define STORPORTAPI STORPORT_API
#endif

/* w32api legacy name; genuine WDK only defines STOR_PHYSICAL_ADDRESS */
#ifndef SCSI_PHYSICAL_ADDRESS
#define SCSI_PHYSICAL_ADDRESS STOR_PHYSICAL_ADDRESS
#endif

/*
 * Genuine WDK pulls in no <errno.h>; the Linux core returns these codes
 * (values follow Linux errno so they stay consistent across toolchains).
 */
#ifndef ENOENT
#define ENOENT      2   /* No such file or directory */
#endif
#ifndef EIO
#define EIO         5   /* I/O error */
#endif
#ifndef ENXIO
#define ENXIO       6   /* No such device or address */
#endif
#ifndef ENOMEM
#define ENOMEM     12   /* Out of memory */
#endif
#ifndef EBUSY
#define EBUSY      16   /* Device or resource busy */
#endif
#ifndef EINVAL
#define EINVAL     22   /* Invalid argument */
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110   /* Connection timed out */
#endif

/* MinGW/MSVCRT defines errno as a macro that calls _errno().
 * The Linux core uses "errno" as a struct field name.
 * Undefine the macro early so core.c compiles unmodified. */
#ifdef errno
#undef errno
#endif

/*************************** Registry breadcrumbs ****************************/
/* Write Step/Status to HKLM\SOFTWARE\aicdbg so headless failures are
 * observable with "reg query" after the fact (no debugger required).
 * Passive-level only — DriverEntry / HwFindAdapter context.               */
#define AIC_TRACE_PATH   L"\\Registry\\Machine\\SOFTWARE\\aicdbg"

static __inline VOID
AicTrace(ULONG step, NTSTATUS status)
{
    NTSTATUS s;
    UNICODE_STRING path;

    RtlInitUnicodeString(&path, AIC_TRACE_PATH);
    s = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, path.Buffer,
                              L"LastStep", REG_DWORD, &step, sizeof(step));
    if (NT_SUCCESS(s))
        RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, path.Buffer,
                              L"LastStatus", REG_DWORD,
                              &status, sizeof(status));
}

/* Well-known step codes (aic79xx & aic7xxx) */
#define AIC_STEP_DRVENTRY_ENTER     1
#define AIC_STEP_STORPORT_INIT_RET  2
#define AIC_STEP_FIND_ENTER         3
#define AIC_STEP_PCI_CFG_READ       4
#define AIC_STEP_CMD_REG_SET        5
#define AIC_STEP_REGS_MAPPED        6
#define AIC_STEP_UNCACHED_ALLOC     7
#define AIC_STEP_SOFTC_INIT         8
#define AIC_STEP_IRQ_CONNECTED      9
#define AIC_STEP_FOUND              10

/*************************** Linux Compatibility Macros ************************/

#define __KERNEL__
#define __LITTLE_ENDIAN      1234
#define __BYTE_ORDER         __LITTLE_ENDIAN
#define __printf_like(a, b)
#define __func__             __FUNCTION__
#define __VA_OPT(...)
#define __maybe_unused
#define likely(x)            (x)
#define unlikely(x)          (x)
#define powerof2(x)          ((((x)-1)&(x))==0)

#define GFP_ATOMIC           0
#define GFP_KERNEL           0

static __inline void *
_kzalloc_obj_impl(size_t sz) {
    void *p = ExAllocatePoolWithTag(NonPagedPool, sz, 'hcIA');
    if (p) {
        uint8_t *bp = (uint8_t *)p;
        size_t i;
        for (i = 0; i < sz; i++) bp[i] = 0;
    }
    return p;
}
#define kzalloc_obj(type, flags)     _kzalloc_obj_impl(sizeof(type))
#define kzalloc_objs(type, count, flags) \
    ((type *)_kzalloc_obj_impl((count) * sizeof(type)))

static __inline void *
kmalloc_array(size_t n, size_t size, int flags)
{
    return ExAllocatePoolWithTag(NonPagedPool, n * size, 'hcIA');
}

#define kmalloc_obj(type, flags) \
    ExAllocatePoolWithTag(NonPagedPool, sizeof(type), 'hcIA')
#define kzalloc(size, flags) _kzalloc_obj_impl(size)
#define kfree(ptr) do { if (ptr) { ExFreePoolWithTag((ptr), 'hcIA'); (ptr) = NULL; } } while(0)
#define malloc(size) ExAllocatePoolWithTag(NonPagedPool, (size), 'hcIA')

static __inline void *
kmemdup(const void *src, size_t size, int flags)
{
    void *dst = ExAllocatePoolWithTag(NonPagedPool, size, 'hcIA');
    if (dst) memcpy(dst, src, size);
    return dst;
}

/* memset/memcpy/memcmp — map to NT kernel equivalents */
static __inline void *
_memset_compat(void *p, int v, size_t n)
{
    uint8_t *bp = (uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) bp[i] = (uint8_t)v;
    return p;
}
#define memset(p, v, n)     _memset_compat((p), (v), (n))
#define memcpy(d, s, n)     StorPortMoveMemory((d), (s), (n))
#define memcmp(s1, s2, n)   RtlCompareMemory((s1), (s2), (n))

#define bzero(p, n)         StorPortZeroMemory((p), (n))
#define bcopy(s, d, n)      StorPortCopyMemory((d), (s), (n))

#ifndef StorPortZeroMemory
static __inline void
StorPortZeroMemory(PVOID Destination, SIZE_T Length)
{
    PUCHAR dst = (PUCHAR)Destination;
    SIZE_T i;
    for (i = 0; i < Length; i++)
        dst[i] = 0;
}
#endif

#ifndef StorPortCopyMemory
#define StorPortCopyMemory(Dest, Src, Len)  StorPortMoveMemory((Dest), (Src), (Len))
#endif

/* Minimal kernel-mode sprintf — core.c uses only %s, %c, %d, %x, %p */
static __inline int
_ksprintf(char *buf, const char *fmt, ...)
{
    UNREFERENCED_PARAMETER(buf);
    UNREFERENCED_PARAMETER(fmt);
    return 0;
}
#define sprintf  _ksprintf

/* add_timer: no-op stub */
#define add_timer(timer)  ((void)0)

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#ifdef _MSC_VER
/* MSVC C has no typeof(); ahc uses this only for stat_timer callbacks */
#define timer_container_of(var, callback_timer, timer_fieldname) \
    ((struct ahc_softc *)((char *)(callback_timer) - offsetof(struct ahc_softc, timer_fieldname)))
#else
#define timer_container_of(var, callback_timer, timer_fieldname) \
    container_of(callback_timer, typeof(*var), timer_fieldname)
#endif

STORPORTAPI
ULONG __cdecl
DbgPrint(const char *Format, ...);

#define printk(fmt, ...)     DbgPrint(fmt, ##__VA_ARGS__)
#define panic(fmt, ...)      do { DbgPrint(fmt, ##__VA_ARGS__); __debugbreak(); } while(0)

/* Per-I/O trace logging, gated by DEBUG=1 build flag.
 * Release builds (default): compiles to nothing (zero code size).
 * Debug builds (DEBUG=1):   expands to DbgPrint() for full tracing. */
#ifdef AIC_DBGPRINT_ENABLED
#define AIC_DBGPRINT(...) DbgPrint(__VA_ARGS__)
#else
#define AIC_DBGPRINT(...)
#endif

#define DELAY(us)            StorPortStallExecution((us))
#define DELAY_MS(ms)         StorPortStallExecution((ms) * 1000)

#define mb()                 MemoryBarrier()
#define wmb()                MemoryBarrier()
#define rmb()                MemoryBarrier()

#ifdef __GNUC__
#define fallthrough __attribute__((fallthrough))
#else
#define fallthrough ((void)0)
#endif

/***************************** Integer Types **********************************/

typedef int bool;
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef unsigned int        u_int;
typedef unsigned long       u_long;
typedef uint8_t             u_char;
typedef unsigned long       dma_addr_t;
typedef uint32_t            bus_addr_t;
typedef uint32_t            ac_code;

#ifndef AIC_LIB_PREFIX
#define AIC_LIB_PREFIX ahc
#endif

/************************* CAM / SCSI Status Codes ****************************/

#define CAM_STATUS_MASK             0x000000FF
#define CAM_REQ_INPROG              0x00000000
#define CAM_REQ_CMP                 0x00000001
#define CAM_REQ_ABORTED             0x00000002
#define CAM_UA_ABORT                0x00000003
#define CAM_REQ_CMP_ERR             0x00000004
#define CAM_BUSY                    0x00000005
#define CAM_REQ_INVALID             0x00000006
#define CAM_PATH_INVALID            0x00000007
#define CAM_SEL_TIMEOUT             0x00000008
#define CAM_CMD_TIMEOUT             0x00000009
#define CAM_SCSI_STATUS_ERROR       0x0000000A
#define CAM_SCSI_BUS_RESET          0x0000000B
#define CAM_UNCOR_PARITY            0x0000000C
#define CAM_AUTOSENSE_FAIL          0x0000000D
#define CAM_NO_HBA                  0x0000000E
#define CAM_DATA_RUN_ERR            0x0000000F
#define CAM_UNEXP_BUSFREE           0x00000010
#define CAM_SEQUENCE_FAIL           0x00000011
#define CAM_CCB_LEN_ERR             0x00000012
#define CAM_PROVIDE_FAIL            0x00000013
#define CAM_BDR_SENT                0x00000014
#define CAM_REQ_TERMIO              0x00000015
#define CAM_UNREC_HBA_ERROR         0x00000016
#define CAM_REQ_TOO_BIG             0x00000017
#define CAM_UA_TERMIO               0x00000018
#define CAM_MSG_REJECT_REC          0x00000019
#define CAM_DEV_NOT_THERE           0x0000001A
#define CAM_RESRC_UNAVAIL           0x0000001B
#define CAM_REQUEUE_REQ             0x0000001C
#define CAM_DEV_QFRZN               0x00000040

/* cam_status type used by the core */
typedef uint32_t cam_status;

/* CAM direction and flags */
#define CAM_DIR_IN      0x01
#define CAM_DIR_OUT     0x02
#define CAM_DIR_NONE    0x00
#define CAM_DIS_DISCONNECT  0x0080
#define CAM_FUNC_NOTAVAIL   0x0040
#define CAM_LUN_ALRDY_ENA   0x0010
#define CAM_LUN_INVALID     0x0020
#define CAM_TID_INVALID     0x0008
#define CAM_TAG_ACTION_VALID 0x0004
#define CAM_CDB_RECVD       0x0002
#define CAM_MESSAGE_RECV    0x0001

#define CAM_TARGET_WILDCARD ((u_int)~0)
#define CAM_LUN_WILDCARD    ((u_int)~0)
#define CAM_BUS_WILDCARD    ((u_int)~0)

/* Async callback codes */
#define AC_TRANSFER_NEG    0x200
#define AC_SENT_BDR        0x010
#define AC_BUS_RESET       0x001
#define AC_FOUND_DEVICE    0x080
#define AC_LOST_DEVICE     0x100

/************************* SCSI Message Constants ******************************/

#define MSG_CMDCOMPLETE         0x00
#define MSG_EXTENDED            0x01
#define MSG_SAVEDATAPOINTER     0x02
#define MSG_RESTOREPOINTERS     0x03
#define MSG_DISCONNECT          0x04
#define MSG_INITIATOR_DET_ERR   0x05
#define MSG_ABORT               0x06
#define MSG_MESSAGE_REJECT      0x07
#define MSG_NOOP                0x08
#define MSG_PARITY_ERROR        0x09
#define MSG_BUS_DEV_RESET       0x0C
#define MSG_ABORT_TAG           0x0D
#define MSG_CLEAR_QUEUE         0x0E

#define MSG_IDENTIFYFLAG        0x80
#define MSG_IDENTIFY_DISCFLAG   0x40
#define MSG_IDENTIFY_LUNMASK    0x3F

/* Extended messages */
#define MSG_EXT_SDTR            0x01
#define MSG_EXT_SDTR_LEN        0x03
#define MSG_EXT_WDTR            0x03
#define MSG_EXT_WDTR_LEN        0x02
#define MSG_EXT_WDTR_BUS_8_BIT  0x00
#define MSG_EXT_WDTR_BUS_16_BIT 0x01
#define MSG_EXT_WDTR_BUS_32_BIT 0x02
#define MSG_EXT_PPR             0x04
#define MSG_EXT_PPR_LEN         0x06
#define MSG_EXT_PPR_DT_REQ      0x02
#define MSG_EXT_PPR_IU_REQ      0x01
#define MSG_EXT_PPR_QAS_REQ     0x04
#define MSG_EXT_PPR_HOLD_MCS    0x08
#define MSG_EXT_PPR_WR_FLOW     0x10
#define MSG_EXT_PPR_RD_STRM     0x20
#define MSG_EXT_PPR_RTI         0x40
#define MSG_EXT_PPR_PCOMP_EN    0x80

/* Queue tag messages */
#define MSG_SIMPLE_Q_TAG        0x20
#define MSG_HEAD_OF_Q_TAG       0x21
#define MSG_ORDERED_Q_TAG       0x22
#define MSG_LINK_CMD_COMPLETE   0x2A
#define MSG_LINK_CMD_COMPLETE_FLAG 0x2B

/* SCSI message aliases used by the core (Linux scsi/scsi.h conventions) */
#define NOP                 MSG_NOOP
#define INITIATOR_ERROR     MSG_INITIATOR_DET_ERR
#define ABORT               MSG_ABORT
#define ABORT_TASK          0x06
#define ABORT_TASK_SET      0x06
#define TARGET_RESET        MSG_BUS_DEV_RESET
#define BUS_DEVICE_RESET    MSG_BUS_DEV_RESET
#define MESSAGE_REJECT      MSG_MESSAGE_REJECT
#define DISCONNECT          MSG_DISCONNECT
#define SAVE_POINTERS       MSG_SAVEDATAPOINTER
#define RESTORE_POINTERS    MSG_RESTOREPOINTERS
#define TASK_SET_FULL       0x28
#define LINKED_CMD_COMPLETE MSG_LINK_CMD_COMPLETE
#define SIMPLE_QUEUE        MSG_SIMPLE_Q_TAG
#define ORDERED_QUEUE_TAG   MSG_ORDERED_Q_TAG
#define ORDERED_QUEUE_TAG_  MSG_ORDERED_Q_TAG
#define HEAD_OF_QUEUE       MSG_HEAD_OF_Q_TAG
#define INITIATOR_DONE      0x07
#define FEATURE_NEGO        0x00
#define EXTENDED_MESSAGE    0x01
#define EXTENDED_SDTR       0x01
#define EXTENDED_WDTR       0x03
#define EXTENDED_PPR        0x04
#define COMMAND_COMPLETE    0x00
#define IGNORE_WIDE_RESIDUE 0x23
#define REQUEST_SENSE       0x03
#define TERMINATE_IO_PROC   0x11
#define QAS_REQUEST         0x55

/************************* SAM / SCSI Status ***********************************/

#define SAM_STAT_CHECK_CONDITION    0x02
#define SAM_STAT_TASK_SET_FULL      0x28
#define SAM_STAT_COMMAND_TERMINATED 0x11
#define SAM_STAT_GOOD               0x00

/************************* Register Definitions (after types) ******************/

#include "aic7xxx_reg.h"

/************************* Forward Declarations *******************************/

struct ahc_softc;
typedef void *ahc_dev_softc_t;
typedef struct scsi_cmnd *ahc_io_ctx_t;

/******************************* Byte Order ***********************************/

#define ahc_htobe16(x)  _byteswap_ushort(x)
#define ahc_htobe32(x)  _byteswap_ulong(x)
#define ahc_htole16(x)  (x)
#define ahc_htole32(x)  (x)

#define ahc_be16toh(x)  _byteswap_ushort(x)
#define ahc_be32toh(x)  _byteswap_ulong(x)
#define ahc_le16toh(x)  (x)
#define ahc_le32toh(x)  (x)

/***************************** Bus Space/DMA **********************************/

typedef uint32_t bus_size_t;

typedef int bus_space_tag_t;
typedef PUCHAR bus_space_handle_t;

typedef struct bus_dma_segment {
    dma_addr_t  ds_addr;
    bus_size_t  ds_len;
} bus_dma_segment_t;

typedef void *bus_dma_tag_t;
typedef dma_addr_t bus_dmamap_t;
typedef int bus_dma_filter_t(void *, dma_addr_t);
typedef void bus_dmamap_callback_t(void *, bus_dma_segment_t *, int, int);

#define BUS_DMA_WAITOK      0x0
#define BUS_DMA_NOWAIT      0x1
#define BUS_DMA_ALLOCNOW    0x2

#define BUS_SPACE_MAXADDR           0xFFFFFFFF
#define BUS_SPACE_MAXADDR_32BIT     0xFFFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT     0xFFFFFFFF

#define BUS_DMASYNC_PREREAD     0x01
#define BUS_DMASYNC_POSTREAD    0x02
#define BUS_DMASYNC_PREWRITE    0x04
#define BUS_DMASYNC_POSTWRITE   0x08

/************************* SCSI Sense Data ************************************/

struct scsi_sense_data {
    uint8_t  error_code;
    uint8_t  segment;
    uint8_t  flags;
    uint8_t  info[4];
    uint8_t  add_sense_len;
    uint8_t  cmd_spec_info[4];
    uint8_t  add_sense_code;
    uint8_t  add_sense_code_qual;
    uint8_t  fru_code;
    uint8_t  sksv[3];
};

#define SSD_ERRCODE_VALID   0x80
#define SSD_KEY             0x0F
#define SSD_MIN_SIZE        18
#define SSD_FULL_SIZE       sizeof(struct scsi_sense_data)

struct scsi_sense {
    uint8_t  opcode;
    uint8_t  byte2;
    uint8_t  unused[2];
    uint8_t  length;
    uint8_t  control;
};

/************************* SCSI Revision Codes *********************************/

#define SCSI_REV_0      0
#define SCSI_REV_CCS    1
#define SCSI_REV_2      2
#define SCSI_REV_SPC    3
#define SCSI_REV_SPC2   4

/************************* Shadow SCSI Types **********************************/

/* ahc_inl/ahc_outl/ahc_inq/ahc_outq are defined in aic7xxx_core.c */
uint32_t ahc_inl(struct ahc_softc *ahc, u_int port);
void     ahc_outl(struct ahc_softc *ahc, u_int port, uint32_t value);
uint64_t ahc_inq(struct ahc_softc *ahc, u_int port);
void     ahc_outq(struct ahc_softc *ahc, u_int port, uint64_t value);

struct scsi_cmnd {
    uint32_t            result;
    int                 sc_data_direction;
    int                 resid;
    struct scsi_device *device;
};

struct scsi_device {
    int qfrozen;
};

/**************************** Per-SCB Platform Data ***************************/

struct scb_platform_data {
    struct ahc_linux_device *dev;
    dma_addr_t              buf_busaddr;
    uint32_t                xfer_len;
    uint32_t                sense_resid;
    PSCSI_REQUEST_BLOCK     srb;
    struct scsi_cmnd        shadow_cmd;
    struct scsi_device      shadow_dev;
};

/**************************** Per-Adapter Platform Data ***********************/

struct ahc_platform_data {
    PVOID               dev_ext;
    PVOID               hw_device_base;
    KSPIN_LOCK          spin_lock;
    uint32_t            bios_address;
    dma_addr_t          mem_busaddr;
    PSCSI_REQUEST_BLOCK pending_srb;
    uint32_t            int_count;
    uint32_t            pci_bus;
    uint32_t            pci_slot;
};

/**************************** Linux Device Flags ******************************/

typedef enum {
    AHC_DEV_FREEZE_TIL_EMPTY = 0x02,
    AHC_DEV_Q_BASIC          = 0x10,
    AHC_DEV_Q_TAGGED         = 0x20,
    AHC_DEV_PERIODIC_OTAG    = 0x40,
} ahc_linux_dev_flags;

struct ahc_linux_device {
    int                  active;
    int                  openings;
    u_int                qfrozen;
    u_long               commands_issued;
    u_int                tag_success_count;
    ahc_linux_dev_flags  flags;
    u_int                maxtags;
    u_int                tags_on_last_queuefull;
    u_int                last_queuefull_same_count;
    u_int                commands_since_idle_or_otag;
};

/************************* Include Core Header ********************************/

#include "queue.h"
#include "aic7xxx.h"

/************************* Overrides ******************************************/

#undef AHC_NSEG
#define AHC_NSEG 128

#define AIC_DEBUG_REGISTERS 0
#define bootverbose aic7xxx_verbose

/************************* ARRAY_SIZE ******************************************/

#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

/************************* SCSI Control Bits **********************************/

#ifndef DISCENB
#define DISCENB             0x40
#endif
#ifndef SIMPLE_QUEUE_TAG
#define SIMPLE_QUEUE_TAG    0x20
#endif

/************************* StorPort Constants **********************************/

/***************************** Timer Facilities *******************************/

static __inline void
ahc_scb_timer_reset(struct scb *scb, u_int usec)
{
    UNREFERENCED_PARAMETER(scb);
    UNREFERENCED_PARAMETER(usec);
}

/****************************** Low Level I/O *********************************/

static __inline uint8_t
ahc_inb(struct ahc_softc *ahc, long port)
{
    return READ_REGISTER_UCHAR((PUCHAR)ahc->bsh + port);
}

static __inline void
ahc_outb(struct ahc_softc *ahc, long port, uint8_t val)
{
    WRITE_REGISTER_UCHAR((PUCHAR)ahc->bsh + port, val);
}

static __inline void
ahc_outsb(struct ahc_softc *ahc, long port, uint8_t *buffer, int count)
{
    int i;
    for (i = 0; i < count; i++)
        ahc_outb(ahc, port, buffer[i]);
}

static __inline void
ahc_insb(struct ahc_softc *ahc, long port, uint8_t *buffer, int count)
{
    int i;
    for (i = 0; i < count; i++)
        buffer[i] = ahc_inb(ahc, port);
}

static __inline void
ahc_flush_device_writes(struct ahc_softc *ahc)
{
    ahc_inb(ahc, INTSTAT);
}

/******************************* Locking ***************************************/

static __inline void
ahc_lockinit(struct ahc_softc *ahc)
{
    KeInitializeSpinLock(&ahc->platform_data->spin_lock);
}

static __inline void
ahc_lock(struct ahc_softc *ahc, u_long *flags)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&ahc->platform_data->spin_lock, &oldIrql);
    *flags = (u_long)oldIrql;
}

static __inline void
ahc_unlock(struct ahc_softc *ahc, u_long *flags)
{
    KeReleaseSpinLock(&ahc->platform_data->spin_lock, (KIRQL)*flags);
}

static __inline void
ahc_delay(long usec)
{
    StorPortStallExecution((ULONG)(usec));
}

/******************************* PCI Definitions ******************************/

#define PCIR_DEVVENDOR          0x00
#define PCIR_VENDOR             0x00
#define PCIR_DEVICE             0x02
#define PCIR_COMMAND            0x04
#define PCIM_CMD_PORTEN         0x0001
#define PCIM_CMD_MEMEN          0x0002
#define PCIM_CMD_BUSMASTEREN    0x0004
#define PCIR_STATUS             0x06
#define PCIR_REVID              0x08
#define PCIR_PROGIF             0x09
#define PCIR_SUBCLASS           0x0a
#define PCIR_CLASS              0x0b
#define PCIR_CACHELNSZ          0x0c
#define PCIR_LATTIMER          0x0d
#define PCIR_HEADERTYPE         0x0e
#define PCIM_MFDEV              0x80
#define PCIR_BIST               0x0f
#define PCIR_CAP_PTR            0x34
#define PCIR_MAPS               0x10
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_SUBSYSTEM_ID        0x2E
#define PCIM_CMD_SERRESPEN      0x0100
#define PCIM_CMD_MWRICEN        0x0010

typedef enum {
    AHC_POWER_STATE_D0,
    AHC_POWER_STATE_D1,
    AHC_POWER_STATE_D2,
    AHC_POWER_STATE_D3
} ahc_power_state;

/**************************** PCI Config Access *******************************/

uint32_t ahc_pci_read_config(ahc_dev_softc_t pci, int reg, int width);
void     ahc_pci_write_config(ahc_dev_softc_t pci, int reg, uint32_t value, int width);

/**************************** Transaction Access Wrappers *********************/

static __inline void
ahc_cmd_set_transaction_status(struct scsi_cmnd *cmd, uint32_t status)
{
    cmd->result &= ~(CAM_STATUS_MASK << 16);
    cmd->result |= status << 16;
}

static __inline void
ahc_set_transaction_status(struct scb *scb, uint32_t status)
{
    ahc_cmd_set_transaction_status(scb->io_ctx, status);
}

static __inline void
ahc_cmd_set_scsi_status(struct scsi_cmnd *cmd, uint32_t status)
{
    cmd->result &= ~0xFFFF;
    cmd->result |= status;
}

static __inline void
ahc_set_scsi_status(struct scb *scb, uint32_t status)
{
    ahc_cmd_set_scsi_status(scb->io_ctx, status);
}

static __inline uint32_t
ahc_cmd_get_transaction_status(struct scsi_cmnd *cmd)
{
    return ((cmd->result >> 16) & CAM_STATUS_MASK);
}

static __inline uint32_t
ahc_get_transaction_status(struct scb *scb)
{
    return (ahc_cmd_get_transaction_status(scb->io_ctx));
}

static __inline uint32_t
ahc_cmd_get_scsi_status(struct scsi_cmnd *cmd)
{
    return (cmd->result & 0xFFFF);
}

static __inline uint32_t
ahc_get_scsi_status(struct scb *scb)
{
    return (ahc_cmd_get_scsi_status(scb->io_ctx));
}

static __inline void
ahc_set_transaction_tag(struct scb *scb, int enabled, u_int type)
{
    UNREFERENCED_PARAMETER(scb);
    UNREFERENCED_PARAMETER(enabled);
    UNREFERENCED_PARAMETER(type);
}

static __inline u_long
ahc_get_transfer_length(struct scb *scb)
{
    return (scb->platform_data->xfer_len);
}

static __inline int
ahc_get_transfer_dir(struct scb *scb)
{
    return (scb->io_ctx->sc_data_direction);
}

static __inline void
ahc_set_residual(struct scb *scb, u_long resid)
{
    scb->io_ctx->resid = (int)resid;
}

static __inline void
ahc_set_sense_residual(struct scb *scb, u_long resid)
{
    scb->platform_data->sense_resid = (uint32_t)resid;
}

static __inline u_long
ahc_get_residual(struct scb *scb)
{
    return (u_long)scb->io_ctx->resid;
}

static __inline u_long
ahc_get_sense_residual(struct scb *scb)
{
    return (scb->platform_data->sense_resid);
}

static __inline int
ahc_perform_autosense(struct scb *scb)
{
    UNREFERENCED_PARAMETER(scb);
    return (1);
}

static __inline uint32_t
ahc_get_sense_bufsize(struct ahc_softc *ahc, struct scb *scb)
{
    UNREFERENCED_PARAMETER(ahc);
    UNREFERENCED_PARAMETER(scb);
    return (sizeof(struct scsi_sense_data));
}

static __inline void
ahc_notify_xfer_settings_change(struct ahc_softc *ahc,
                                struct ahc_devinfo *devinfo)
{
    UNREFERENCED_PARAMETER(ahc);
    UNREFERENCED_PARAMETER(devinfo);
}

static __inline void
ahc_platform_scb_free(struct ahc_softc *ahc, struct scb *scb)
{
    UNREFERENCED_PARAMETER(ahc);
    UNREFERENCED_PARAMETER(scb);
}

static __inline void
ahc_freeze_scb(struct scb *scb)
{
    if ((scb->io_ctx->result & (CAM_DEV_QFRZN << 16)) == 0) {
        scb->io_ctx->result |= CAM_DEV_QFRZN << 16;
        scb->platform_data->dev->qfrozen++;
    }
}

/************************* DMA Operations *************************************/

#define ahc_dmamap_sync(ahc, dma_tag, dmamap, offset, len, op)

int   ahc_dma_tag_create(struct ahc_softc *, bus_dma_tag_t, bus_size_t,
                          bus_size_t, dma_addr_t, dma_addr_t,
                          bus_dma_filter_t *, void *, bus_size_t,
                          int, bus_size_t, int, bus_dma_tag_t *);
void  ahc_dma_tag_destroy(struct ahc_softc *, bus_dma_tag_t);
int   ahc_dmamem_alloc(struct ahc_softc *, bus_dma_tag_t, void **,
                        int, bus_dmamap_t *);
void  ahc_dmamem_free(struct ahc_softc *, bus_dma_tag_t, void *,
                       bus_dmamap_t);
void  ahc_dmamap_destroy(struct ahc_softc *, bus_dma_tag_t, bus_dmamap_t);
int   ahc_dmamap_load(struct ahc_softc *, bus_dma_tag_t, bus_dmamap_t,
                       void *, bus_size_t, bus_dmamap_callback_t *,
                       void *, int);
int   ahc_dmamap_unload(struct ahc_softc *, bus_dma_tag_t, bus_dmamap_t);

/**************************** Platform Functions *******************************/

int   ahc_platform_alloc(struct ahc_softc *, void *);
void  ahc_platform_free(struct ahc_softc *);
void  ahc_platform_freeze_devq(struct ahc_softc *, struct scb *);
void  ahc_platform_set_tags(struct ahc_softc *, struct scsi_device *,
                            struct ahc_devinfo *, ahc_queue_alg);
int   ahc_platform_abort_scbs(struct ahc_softc *, int, char, int,
                               u_int, role_t, uint32_t);
void  ahc_platform_flushwork(struct ahc_softc *);
void  ahc_done(struct ahc_softc *, struct scb *);
void  ahc_send_async(struct ahc_softc *, char, u_int, u_int, ac_code);
void  ahc_print_path(struct ahc_softc *, struct scb *);
void  ahc_power_state_change(struct ahc_softc *, ahc_power_state);
void  ahc_freeze_simq(struct ahc_softc *);
void  ahc_release_simq(struct ahc_softc *);

/**************************** SCSI Transport Helpers ***************************/

static __inline int spi_populate_width_msg(uint8_t *msg, int width) { (void)msg; (void)width; return 0; }
static __inline int spi_populate_sync_msg(uint8_t *msg, int period, int offset) { (void)msg; (void)period; (void)offset; return 0; }
static __inline int spi_populate_ppr_msg(uint8_t *msg, int period, int offset, int width, int flags) { (void)msg; (void)period; (void)offset; (void)width; (void)flags; return 0; }

/**************************** PCI Helper Functions *****************************/

int   ahc_get_pci_function(ahc_dev_softc_t pci);
int   ahc_get_pci_slot(ahc_dev_softc_t pci);
int   ahc_get_pci_bus(ahc_dev_softc_t pci);
void  pci_set_power_state(ahc_dev_softc_t pci, ahc_power_state state);
int   ahc_pci_map_registers(struct ahc_softc *ahc);
int   ahc_pci_map_int(struct ahc_softc *ahc);

extern u_int aic7xxx_verbose;

#define AHC_PCI_CONFIG 1

#endif /* _AIC7XXX_OSM_WIN_H_ */
