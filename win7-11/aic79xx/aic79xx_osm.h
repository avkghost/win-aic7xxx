/*
 * Windows StorPort OSM (OS Module) header for AIC79xx (U320).
 * Replaces Linux aic79xx_osm.h — provides all type definitions,
 * constants, macros, and inline functions the core needs.
 *
 * The core (aic79xx_core.c) stays unmodified. This header is the
 * sole adaptation layer between Linux core code and Windows StorPort.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AIC79XX_OSM_WIN_H_
#define _AIC79XX_OSM_WIN_H_

#pragma warning(disable:4100) /* unreferenced formal parameter */
#pragma warning(disable:4127) /* conditional expression is constant */
#pragma warning(disable:4201) /* nameless struct/union */
#pragma warning(disable:4213) /* nonstandard extension: cast on l-value */
#pragma warning(disable:4214) /* nonstandard extension: bit field types other than int */
#pragma warning(disable:4701) /* potentially uninitialized local variable */
#pragma warning(disable:4706) /* assignment within conditional expression */
#pragma warning(disable:4996) /* deprecated POSIX names */

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

/*
 * MinGW DDK bug: srb.h and storport.h both define struct _SCSI_WMI_REQUEST_BLOCK
 * but with different members when _WIN64 is defined (srb.h adds Reserved6).
 * We use local patched copies in compat/ that add an include guard.
 * The -I compat/ flag must come before -I /usr/share/.../ddk on the command line.
 */
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

/*
 * MinGW/MSVCRT defines errno as a macro that calls _errno().
 * The Linux core uses "errno" as a struct field name in ahd_hard_errors.
 * Undefine the macro early so core.c compiles unmodified.
 */
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
#define __inline             __inline
#define __printf_like(a, b)
#define __func__             __FUNCTION__
#define __VA_OPT(...)
#define __maybe_unused
#define likely(x)            (x)
#define unlikely(x)          (x)
#define powerof2(x)          ((((x)-1)&(x))==0)

/* container_of: recover pointer to enclosing struct from member pointer */
#define container_of(ptr, type, member)     ((type *)((char *)(ptr) - offsetof(type, member)))

/* timer_container_of: Linux kernel timer callback helper */
#ifdef _MSC_VER
/* MSVC C has no typeof(); both drivers only use stat_timer callbacks */
#define timer_container_of(var, callback_timer, timer_fieldname) \
    ((struct ahd_softc *)((char *)(callback_timer) - offsetof(struct ahd_softc, timer_fieldname)))
#else
#define timer_container_of(var, callback_timer, timer_fieldname) \
    container_of(callback_timer, typeof(*var), timer_fieldname)
#endif

/* Variadic printk → StorPort debug output */
/* printk returns number of chars printed (core uses this for column tracking) */
/* DbgPrint — import from ntoskrnl.exe */
STORPORTAPI
ULONG __cdecl
DbgPrint(const char *Format, ...);

#define printk(fmt, ...)     DbgPrint(fmt, ##__VA_ARGS__)

/* Per-I/O trace logging, gated by DEBUG=1 build flag.
 * Release builds (default): compiles to nothing (zero code size).
 * Debug builds (DEBUG=1):   expands to DbgPrint() for full tracing. */
#ifdef AIC_DBGPRINT_ENABLED
#define AIC_DBGPRINT(...) DbgPrint(__VA_ARGS__)
#else
#define AIC_DBGPRINT(...)
#endif

/* Delay in microseconds */
#define DELAY(us)            StorPortStallExecution((us))

/* Memory barriers */
#define mb()                 MemoryBarrier()
#define wmb()                MemoryBarrier()
#define rmb()                MemoryBarrier()

/***************************** Integer Types **********************************/

/*
 * MinGW's <ntddk.h> → <windef.h> → <stdint.h> already provides
 * uint8_t/16_t/32_t/64_t etc. Only define types the DDK doesn't provide.
 */
#ifndef _U_INT_DEFINED
#define _U_INT_DEFINED
typedef unsigned int        u_int;
#endif
typedef unsigned long       u_long;
typedef unsigned long       dma_addr_t;
typedef unsigned long       resource_size_t;

/************************* Forward Declarations *******************************/

struct ahd_softc;

/*
 * ahd_dev_softc_t: the "device softc" passed to PCI config accessors.
 * On Linux this is struct pci_dev*. On Windows we use the StorPort device
 * extension pointer (PVOID), since StorPort APIs require it.
 */
typedef PVOID ahd_dev_softc_t;

/*
 * ahd_io_ctx_t: the per-I/O context pointer stored in scb->io_ctx.
 * On Linux this is struct scsi_cmnd*. On Windows we use a shadow struct.
 */

/************************* Shadow struct scsi_cmnd ****************************/

/*
 * The Linux core accesses scb->io_ctx as struct scsi_cmnd*, calling
 * OSM inline functions that read/write fields like result, sc_data_direction,
 * and device. We provide a shadow struct with only those fields.
 *
 * AHC_TARGET_MODE is NOT defined — all target mode code (~700 lines)
 * that accesses ccb_h.func_code and csio.tag_id is excluded.
 */
struct scsi_device {
    int     qfrozen;
};

struct scsi_cmnd {
    uint32_t             result;           /* CAM status << 16 | SCSI status */
    struct scsi_device  *device;
    uint32_t             sc_data_direction;
    uint32_t             resid;
};

#define scsi_set_resid(cmd, r)  ((cmd)->resid = (uint32_t)(r))
#define scsi_get_resid(cmd)     ((cmd)->resid)

typedef struct scsi_cmnd *ahd_io_ctx_t;

/************************* CAM Constants **************************************/

/*
 * CAM status codes — from FreeBSD cam.h / Linux cam.h.
 * The core packs these into bits 16-21 of scsi_cmnd.result.
 */
typedef enum {
    CAM_REQ_INPROG        = 0x00,
    CAM_REQ_CMP           = 0x01,
    CAM_REQ_ABORTED       = 0x02,
    CAM_UA_ABORT          = 0x03,
    CAM_REQ_CMP_ERR       = 0x04,
    CAM_BUSY              = 0x05,
    CAM_REQ_INVALID       = 0x06,
    CAM_PATH_INVALID      = 0x07,
    CAM_SEL_TIMEOUT       = 0x08,
    CAM_CMD_TIMEOUT       = 0x09,
    CAM_SCSI_STATUS_ERROR = 0x0A,
    CAM_SCSI_BUS_RESET    = 0x0B,
    CAM_UNCOR_PARITY      = 0x0C,
    CAM_AUTOSENSE_FAIL    = 0x0D,
    CAM_NO_HBA            = 0x0E,
    CAM_DATA_RUN_ERR      = 0x0F,
    CAM_UNEXP_BUSFREE     = 0x10,
    CAM_SEQUENCE_FAIL     = 0x11,
    CAM_CCB_LEN_ERR       = 0x12,
    CAM_PROVIDE_FAIL      = 0x13,
    CAM_BDR_SENT          = 0x14,
    CAM_REQ_TERMIO        = 0x15,
    CAM_UNREC_HBA_ERROR   = 0x16,
    CAM_REQ_TOO_BIG       = 0x17,
    CAM_UA_TERMIO         = 0x18,
    CAM_MSG_REJECT_REC    = 0x19,
    CAM_DEV_NOT_THERE     = 0x1A,
    CAM_RESRC_UNAVAIL     = 0x1B,
    CAM_REQUEUE_REQ       = 0x1C,
    CAM_DEV_QFRZN         = 0x40,
    CAM_STATUS_MASK       = 0x3F
} cam_status;

/* Additional CAM flags used by the core */
#define CAM_DIS_DISCONNECT     0x0080
#define CAM_FUNC_NOTAVAIL      0x0040
#define CAM_LUN_ALRDY_ENA      0x0010
#define CAM_LUN_INVALID        0x0020
#define CAM_TID_INVALID        0x0008
#define CAM_TAG_ACTION_VALID   0x0004
#define CAM_CDB_RECVD          0x0002
#define CAM_MESSAGE_RECV       0x0001

/* CAM direction constants */
#define CAM_DIR_IN     0x01
#define CAM_DIR_OUT    0x02
#define CAM_DIR_NONE   0x00

/* CAM wildcard values */
#define CAM_TARGET_WILDCARD    ((u_int)~0)
#define CAM_LUN_WILDCARD       ((u_int)~0)
#define CAM_BUS_WILDCARD       ((u_int)~0)

/* Async callback codes */
#define AC_TRANSFER_NEG    0x200
#define AC_SENT_BDR        0x010
#define AC_BUS_RESET       0x001
#define AC_FOUND_DEVICE    0x080
#define AC_LOST_DEVICE     0x100

/************************* SCSI Message Constants ******************************/

/*
 * Standard SCSI message byte values — from FreeBSD scsi_message.h.
 */
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

/* Identify message */
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

/* MSG_OUT is defined in aic79xx_reg.h as register offset 0x137 */

/*
 * Aliases the core uses — these are the same SCSI message byte values
 * but named per Linux scsi/scsi.h conventions.
 */
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

/************************* Timer Stubs ****************************************/

#define HZ      1000
#define jiffies 0

/* Linux timer API stubs — core uses these for periodic stat polling */
#define timer_setup(timer, callback, flags)  ((void)0)
#define timer_delete(timer)                  ((void)0)
#define timer_delete_sync(timer)             ((void)0)
#define timer_reset(timer, expiration)       ((void)0)
/* ahd_timer_reset is defined in aic79xx_core.c */

/************************* fallthrough *****************************************/
#ifdef __GNUC__
#define fallthrough __attribute__((fallthrough))
#else
#define fallthrough ((void)0)
#endif

/************************* ARRAY_SIZE ******************************************/
#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

/************************* SAM / SCSI Status ***********************************/

#define SAM_STAT_GOOD                0x00
#define SAM_STAT_CHECK_CONDITION     0x02
#define SAM_STAT_COMMAND_TERMINATED  0x22

/* SCSI status byte values */
#define SCSI_STATUS_OK              0x00
#define SCSI_STATUS_CHECK_COND      0x02
#define SCSI_STATUS_COND_MET        0x04
#define SCSI_STATUS_BUSY            0x08
#define SCSI_STATUS_INTERMED        0x10
#define SCSI_STATUS_RESERV_CONFLICT 0x18
#define SCSI_STATUS_CMD_TERMINATED  0x22
#define SCSI_STATUS_QUEUE_FULL      0x28

/************************* ac_code (async callback) **************************/

typedef uint32_t ac_code;

/************************* SCSI Tag Types **************************************/

#define SIMPLE_QUEUE_TAG    0x20
#define ORDERED_QUEUE_TAG   0x22
#define HEAD_OF_Q_TAG       0x21
#define UNTAGGED            0x00

/* role_t and ahd_queue_alg are defined in aic79xx.h */

/************************* Sense Data ******************************************/

struct scsi_sense {
    uint8_t  opcode;
    uint8_t  byte2;
    uint8_t  unused[2];
    uint8_t  length;
    uint8_t  control;
};

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

/************************* SCSI Revision Codes *********************************/

#define SCSI_REV_0      0
#define SCSI_REV_CCS    1
#define SCSI_REV_2      2
#define SCSI_REV_SPC    3
#define SCSI_REV_SPC2   4

/************************* Bus Space / DMA Types *******************************/

typedef size_t              bus_size_t;

typedef enum {
    BUS_SPACE_MEMIO,
    BUS_SPACE_PIO
} bus_space_tag_t;

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
#define BUS_DMA_LOAD_SEGS   0x4

#define BUS_SPACE_MAXADDR           0xFFFFFFFF
#define BUS_SPACE_MAXADDR_32BIT     0xFFFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT     0xFFFFFFFF

/* DMA sync operations */
#define BUS_DMASYNC_PREREAD     0x01
#define BUS_DMASYNC_POSTREAD    0x02
#define BUS_DMASYNC_PREWRITE    0x04
#define BUS_DMASYNC_POSTWRITE   0x08

/************************* MinGW DDK Compat Defines **************************/

/* PCI_COMMON_COMMAND is not in MinGW DDK — use Command field offset */
#ifndef PCI_COMMON_COMMAND
#define PCI_COMMON_COMMAND  0x04
#endif

/************************* Configuration Data **********************************/

#define AHD_NSEG    128
#define AHD_TAG_SUCCESS_INTERVAL  50
#define AHD_OTAG_THRESH           500
#define AHD_LOCK_TAGS_COUNT       50
#define AHD_LINUX_NOIRQ           ((uint32_t)~0)
#define AIC79XX_DRIVER_VERSION    "3.0"

/**************************** Includes ****************************************/

/* Stub struct timer_list — the core uses it in ahd_softc.stat_timer */
struct timer_list {
    void    (*function)(unsigned long);
    unsigned long data;
    unsigned long expires;
};

/* BSD queue macros (TAILQ, SLIST, LIST) — must come before aic79xx.h */
#include "queue.h"

/* Register header — defines AIC_DEBUG_REGISTERS and register offsets */
#define AIC_DEBUG_REGISTERS 0
#include "aic79xx.h"

/************************* Device Data Structures ******************************/

typedef enum {
    AHD_DEV_FREEZE_TIL_EMPTY = 0x02,
    AHD_DEV_Q_BASIC          = 0x10,
    AHD_DEV_Q_TAGGED         = 0x20,
    AHD_DEV_PERIODIC_OTAG    = 0x40
} ahd_linux_dev_flags;

struct ahd_linux_device {
    int                 active;
    int                 openings;
    u_int               qfrozen;
    u_long              commands_issued;
    u_int               tag_success_count;
    ahd_linux_dev_flags flags;
    u_int               maxtags;
    u_int               tags_on_last_queuefull;
    u_int               last_queuefull_same_count;
    u_int               commands_since_idle_or_otag;
};

/************************* Platform Data Structures ****************************/

struct scb_platform_data {
    struct ahd_linux_device *dev;
    dma_addr_t              buf_busaddr;
    uint32_t                xfer_len;
    uint32_t                sense_resid;
    PSCSI_REQUEST_BLOCK     srb;
    struct scsi_device      shadow_dev;     /* embedded shadow device for io_ctx->device */
    struct scsi_cmnd        shadow_cmd;     /* embedded shadow scsi_cmnd for scb->io_ctx */
};

struct ahd_platform_data {
    PVOID               dev_ext;        /* Back-pointer to device extension */
    PVOID               hw_device_base; /* StorPortGetDeviceBase result (BAR0) */
    KSPIN_LOCK          spin_lock;
    uint32_t            bios_address;
    dma_addr_t          mem_busaddr;
    PSCSI_REQUEST_BLOCK pending_srb;
    ULONG               int_count;
    ULONG               pci_bus;        /* PCI bus number */
    ULONG               pci_slot;       /* PCI slot (device<<3 | function) */
};

/************************* PCI Definitions *************************************/

#define PCIR_DEVVENDOR          0x00
#define PCIR_VENDOR             0x00
#define PCIR_DEVICE             0x02
#define PCIR_COMMAND            0x04
#define PCIM_CMD_PORTEN         0x0001
#define PCIM_CMD_MEMEN          0x0002
#define PCIM_CMD_BUSMASTEREN    0x0004
#define PCIM_CMD_MWRICEN        0x0010
#define PCIM_CMD_PERRESPEN      0x0040
#define PCIM_CMD_SERRESPEN      0x0100
#define PCIR_STATUS             0x06
#define PCIR_REVID              0x08
#define PCIR_PROGIF             0x09
#define PCIR_SUBCLASS           0x0A
#define PCIR_CLASS              0x0B
#define PCIR_CACHELNSZ          0x0C
#define PCIR_LATTIMER           0x0D
#define PCIR_HEADERTYPE         0x0E
#define PCIM_MFDEV              0x80
#define PCIR_BIST               0x0F
#define PCIR_CAP_PTR            0x34
#define PCIR_MAPS               0x10
#define PCIXR_COMMAND           0x96
#define PCIXR_STATUS            0x9A

/* PCI subsystem ID registers */
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_SUBSYSTEM_ID        0x2E

/************************* Power State *****************************************/

typedef enum {
    AHD_POWER_STATE_D0,
    AHD_POWER_STATE_D1,
    AHD_POWER_STATE_D2,
    AHD_POWER_STATE_D3
} ahd_power_state;

/************************* Debug Print Infrastructure **************************/

/*
 * aic79xx_reg.h_shipped auto-generates macros like:
 *   #define ahd_intstat_print(regvalue, cur_col, wrap) \
 *       ahd_print_register(NULL, 0, "INTSTAT", 0x01, regvalue, cur_col, wrap)
 *
 * We implement ahd_print_register() in the StorPort glue.
 * The parse table walk makes the output human-readable.
 */
int ahd_print_register(const ahd_reg_parse_entry_t *table, u_int num_entries,
                       const char *name, u_int address, u_int value,
                       u_int *cur_column, u_int wrap);

/************************* Byte Order ******************************************/

/*
 * x86 is little-endian. Most of these are no-ops but we define them
 * for correctness and portability.
 */
static __inline uint16_t ahd_htole16(uint16_t x) { return x; }
static __inline uint32_t ahd_htole32(uint32_t x) { return x; }
static __inline uint64_t ahd_htole64(uint64_t x) { return x; }
static __inline uint16_t ahd_le16toh(uint16_t x) { return x; }
static __inline uint32_t ahd_le32toh(uint32_t x) { return x; }
static __inline uint64_t ahd_le64toh(uint64_t x) { return x; }

static __inline uint16_t ahd_htobe16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
static __inline uint32_t ahd_htobe32(uint32_t x) {
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}
static __inline uint64_t ahd_htobe64(uint64_t x) {
    return ((uint64_t)ahd_htobe32((uint32_t)x) << 32) | ahd_htobe32((uint32_t)(x >> 32));
}
static __inline uint16_t ahd_be16toh(uint16_t x) { return ahd_htobe16(x); }
static __inline uint32_t ahd_be32toh(uint32_t x) { return ahd_htobe32(x); }
static __inline uint64_t ahd_be64toh(uint64_t x) { return ahd_htobe64(x); }

/************************* Low Level I/O (MMIO) *******************************/

/*
 * Register access via ahd->bshs[0], a PUCHAR set by ahd_pci_map_registers.
 */
static __inline uint8_t
ahd_inb(struct ahd_softc *ahd, long port)
{
    return READ_REGISTER_UCHAR(ahd->bshs[0] + port);
}

static __inline void
ahd_outb(struct ahd_softc *ahd, long port, uint8_t val)
{
    WRITE_REGISTER_UCHAR(ahd->bshs[0] + port, val);
}

static __inline uint16_t
ahd_inw_atomic(struct ahd_softc *ahd, long port)
{
    return READ_REGISTER_USHORT((PUSHORT)(ahd->bshs[0] + port));
}

static __inline void
ahd_outw_atomic(struct ahd_softc *ahd, long port, uint16_t val)
{
    WRITE_REGISTER_USHORT((PUSHORT)(ahd->bshs[0] + port), val);
}

/* ahd_inl/ahd_outl/ahd_inq/ahd_outq are defined in aic79xx_core.c */
uint32_t ahd_inl(struct ahd_softc *ahd, u_int port);
void     ahd_outl(struct ahd_softc *ahd, u_int port, uint32_t value);
uint64_t ahd_inq(struct ahd_softc *ahd, u_int port);
void ahd_setup_data_scb(struct ahd_softc *scb_softc, struct scb *scb);
void ahd_setup_noxfer_scb(struct ahd_softc *scb_softc, struct scb *scb);

void     ahd_outq(struct ahd_softc *ahd, u_int port, uint64_t value);

static __inline void
ahd_outsb(struct ahd_softc *ahd, long port, uint8_t *buf, int count)
{
    int i;
    for (i = 0; i < count; i++)
        ahd_outb(ahd, port, buf[i]);
}

static __inline void
ahd_insb(struct ahd_softc *ahd, long port, uint8_t *buf, int count)
{
    int i;
    for (i = 0; i < count; i++)
        buf[i] = ahd_inb(ahd, port);
}

/************************* Sequencer Execution Control *************************/

void ahd_set_modes(struct ahd_softc *ahd, ahd_mode src, ahd_mode dst);
ahd_mode_state ahd_save_modes(struct ahd_softc *ahd);
void ahd_restore_modes(struct ahd_softc *ahd, ahd_mode_state state);
int  ahd_is_paused(struct ahd_softc *ahd);
void ahd_pause(struct ahd_softc *ahd);
void ahd_unpause(struct ahd_softc *ahd);

/************************* Locking *********************************************/

static __inline void
ahd_lockinit(struct ahd_softc *ahd)
{
    KeInitializeSpinLock(&ahd->platform_data->spin_lock);
}

static __inline void
ahd_lock(struct ahd_softc *ahd, unsigned long *flags)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&ahd->platform_data->spin_lock, &oldIrql);
    *flags = (unsigned long)oldIrql;
}

static __inline void
ahd_unlock(struct ahd_softc *ahd, unsigned long *flags)
{
    KeReleaseSpinLock(&ahd->platform_data->spin_lock, (KIRQL)*flags);
}

/************************* Delay ***********************************************/

static __inline void
ahd_delay(long us)
{
    StorPortStallExecution((ULONG)us);
}

/************************* Memory Barrier **************************************/

/* MemoryBarrier may not be defined in MinGW DDK headers */
#ifndef MemoryBarrier
#define MemoryBarrier()  __sync_synchronize()
#endif

static __inline void
ahd_flush_device_writes(struct ahd_softc *ahd)
{
    MemoryBarrier();
}

/************************* DDK Inline Stubs **************************************
 * These are macros/inline functions in the real Windows DDK headers but may
 * be missing from MinGW's DDK header set. Define them here for cross-build.
 */
#ifndef StorPortCopyMemory
#define StorPortCopyMemory(Dest, Src, Len)  StorPortMoveMemory((Dest), (Src), (Len))
#endif

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

/* StorPortSetBusDataByOffset is the only PCI config write we use.
 * Declared here in case MinGW headers don't include it.
 * StorPortSetBusData (full-buffer) is NOT imported — use ByOffset.
 */

/************************* PCI Config Access ***********************************/

uint32_t ahd_pci_read_config(ahd_dev_softc_t pci, int reg, int width);
void     ahd_pci_write_config(ahd_dev_softc_t pci, int reg, uint32_t value, int width);

static __inline int
ahd_get_pci_function(ahd_dev_softc_t pci)
{
    /* pci = device extension; platform_data has pci_slot */
    struct ahd_softc *ahd = pci;  /* device extension starts with ahd_softc */
    return (int)(ahd->platform_data->pci_slot & 0x07);
}

static __inline int
ahd_get_pci_slot(ahd_dev_softc_t pci)
{
    struct ahd_softc *ahd = pci;
    return (int)(ahd->platform_data->pci_slot >> 3);
}

static __inline int
ahd_get_pci_bus(ahd_dev_softc_t pci)
{
    struct ahd_softc *ahd = pci;
    return (int)ahd->platform_data->pci_bus;
}

/************************* Transaction Access Wrappers *************************/

static __inline void
ahd_cmd_set_transaction_status(struct scsi_cmnd *cmd, uint32_t status)
{
    cmd->result &= ~(CAM_STATUS_MASK << 16);
    cmd->result |= status << 16;
}

static __inline void
ahd_set_transaction_status(struct scb *scb, uint32_t status)
{
    ahd_cmd_set_transaction_status(scb->io_ctx, status);
}

static __inline void
ahd_cmd_set_scsi_status(struct scsi_cmnd *cmd, uint32_t status)
{
    cmd->result &= ~0xFFFF;
    cmd->result |= status;
}

static __inline void
ahd_set_scsi_status(struct scb *scb, uint32_t status)
{
    ahd_cmd_set_scsi_status(scb->io_ctx, status);
}

static __inline uint32_t
ahd_cmd_get_transaction_status(struct scsi_cmnd *cmd)
{
    return ((cmd->result >> 16) & CAM_STATUS_MASK);
}

static __inline uint32_t
ahd_get_transaction_status(struct scb *scb)
{
    return ahd_cmd_get_transaction_status(scb->io_ctx);
}

static __inline uint32_t
ahd_cmd_get_scsi_status(struct scsi_cmnd *cmd)
{
    return (cmd->result & 0xFFFF);
}

static __inline uint32_t
ahd_get_scsi_status(struct scb *scb)
{
    return ahd_cmd_get_scsi_status(scb->io_ctx);
}

static __inline void
ahd_set_transaction_tag(struct scb *scb, int enabled, u_int type)
{
    UNREFERENCED_PARAMETER(scb);
    UNREFERENCED_PARAMETER(enabled);
    UNREFERENCED_PARAMETER(type);
}

static __inline u_long
ahd_get_transfer_length(struct scb *scb)
{
    return (u_long)scb->platform_data->xfer_len;
}

static __inline int
ahd_get_transfer_dir(struct scb *scb)
{
    return (int)scb->io_ctx->sc_data_direction;
}

static __inline void
ahd_set_residual(struct scb *scb, u_long resid)
{
    scsi_set_resid(scb->io_ctx, resid);
}

static __inline void
ahd_set_sense_residual(struct scb *scb, u_long resid)
{
    scb->platform_data->sense_resid = (uint32_t)resid;
}

static __inline u_long
ahd_get_residual(struct scb *scb)
{
    return (u_long)scsi_get_resid(scb->io_ctx);
}

static __inline u_long
ahd_get_sense_residual(struct scb *scb)
{
    return (u_long)scb->platform_data->sense_resid;
}

static __inline int
ahd_perform_autosense(struct scb *scb)
{
    UNREFERENCED_PARAMETER(scb);
    return 1;
}

static __inline uint32_t
ahd_get_sense_bufsize(struct ahd_softc *ahd, struct scb *scb)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(scb);
    return sizeof(struct scsi_sense_data);
}

static __inline void
ahd_notify_xfer_settings_change(struct ahd_softc *ahd,
                                struct ahd_devinfo *devinfo)
{
    UNREFERENCED_PARAMETER(ahd);
    UNREFERENCED_PARAMETER(devinfo);
}

static __inline void
ahd_platform_scb_free(struct ahd_softc *ahd, struct scb *scb)
{
    UNREFERENCED_PARAMETER(scb);
    ahd->flags &= ~AHD_RESOURCE_SHORTAGE;
}

static __inline void
ahd_freeze_scb(struct scb *scb)
{
    if ((scb->io_ctx->result & (CAM_DEV_QFRZN << 16)) == 0) {
        scb->io_ctx->result |= CAM_DEV_QFRZN << 16;
    }
}

/************************* SPI Transport Stubs ********************************/

/* spi_populate_* are Linux SCSI Transport helpers.
 * Forward-declare as returning int; core only uses them in
 * target mode code (excluded when AHC_TARGET_MODE is not defined).
 * These should never be called but must compile.
 */
struct scsi_pointer;
static __inline int spi_populate_width_msg(uint8_t *msg, int width) { (void)msg; (void)width; return 0; }
static __inline int spi_populate_sync_msg(uint8_t *msg, int period, int offset) { (void)msg; (void)period; (void)offset; return 0; }
static __inline int spi_populate_ppr_msg(uint8_t *msg, int period, int offset, int width, int flags) { (void)msg; (void)period; (void)offset; (void)width; (void)flags; return 0; }

/************************* SCSI Status IU (U320) *****************************/

struct scsi_status_iu_header {
    uint8_t  reserved[2];
    uint8_t  flags;
    uint8_t  status;
    uint8_t  sense_length[4];
    uint8_t  pkt_failures_length[4];
    uint8_t  pkt_failures[1];
};

#define SIU_SNSVALID              0x02
#define SIU_RSPVALID              0x01
#define SIU_PKTFAIL_OFFSET(siu)   12
#define SIU_PKTFAIL_CODE(siu)     (scsi_4btoul((siu)->pkt_failures) & 0xFF)
#define SIU_PFC_NONE              0
#define SIU_PFC_CIU_FIELDS_INVALID 2
#define SIU_PFC_TMF_NOT_SUPPORTED 4
#define SIU_PFC_TMF_FAILED        5
#define SIU_PFC_INVALID_TYPE_CODE 6
#define SIU_PFC_ILLEGAL_REQUEST   7
#define SIU_SENSE_OFFSET(siu)     (12 + (((siu)->flags & SIU_RSPVALID)     ? scsi_4btoul((siu)->pkt_failures_length) : 0))
#define SIU_TASKMGMT_NONE         0x00
#define SIU_TASKMGMT_ABORT_TASK   0x01
#define SIU_TASKMGMT_ABORT_TASK_SET 0x02
#define SIU_TASKMGMT_CLEAR_TASK_SET 0x04
#define SIU_TASKMGMT_LUN_RESET    0x08
#define SIU_TASKMGMT_TARGET_RESET 0x20
#define SIU_TASKMGMT_CLEAR_ACA    0x40

/************************* DMA Stubs *******************************************/

int  ahd_dma_tag_create(struct ahd_softc *, bus_dma_tag_t, bus_size_t,
                        bus_size_t, dma_addr_t, dma_addr_t, bus_dma_filter_t *,
                        void *, bus_size_t, int, bus_size_t, int, bus_dma_tag_t *);
void ahd_dma_tag_destroy(struct ahd_softc *, bus_dma_tag_t);
int  ahd_dmamem_alloc(struct ahd_softc *, bus_dma_tag_t, void **, int,
                      bus_dmamap_t *);
void ahd_dmamem_free(struct ahd_softc *, bus_dma_tag_t, void *, bus_dmamap_t);
void ahd_dmamap_destroy(struct ahd_softc *, bus_dma_tag_t, bus_dmamap_t);
int  ahd_dmamap_load(struct ahd_softc *, bus_dma_tag_t, bus_dmamap_t, void *,
                     bus_size_t, bus_dmamap_callback_t *, void *, int);
int  ahd_dmamap_unload(struct ahd_softc *, bus_dma_tag_t, bus_dmamap_t);

/* ahd_dmamap_sync is a no-op on Windows — uncached extension is coherent */
#define ahd_dmamap_sync(ahd, dma_tag, dmamap, offset, len, op)

/************************* Platform Functions (StorPort Glue) ******************/

void ahd_done(struct ahd_softc *, struct scb *);
void ahd_send_async(struct ahd_softc *, char channel, u_int target,
                    u_int lun, ac_code);
void ahd_print_path(struct ahd_softc *, struct scb *);
int  ahd_platform_alloc(struct ahd_softc *ahd, void *platform_arg);
void ahd_platform_free(struct ahd_softc *ahd);
void ahd_platform_init(struct ahd_softc *ahd);
void ahd_platform_freeze_devq(struct ahd_softc *ahd, struct scb *scb);
void ahd_platform_set_tags(struct ahd_softc *ahd, struct scsi_device *sdev,
                           struct ahd_devinfo *devinfo, ahd_queue_alg);
int  ahd_platform_abort_scbs(struct ahd_softc *ahd, int target, char channel,
                            int lun, u_int tag, role_t role, uint32_t status);
void ahd_power_state_change(struct ahd_softc *ahd, ahd_power_state new_state);
void ahd_freeze_simq(struct ahd_softc *ahd);
void ahd_release_simq(struct ahd_softc *ahd);

/************************* PCI Stub Functions ***********************************
 * On Windows, StorPort handles register mapping and interrupt registration.
 * These are no-op stubs called from the Linux-origin aic79xx_pci.c.
 */
static __inline int
ahd_pci_map_registers(struct ahd_softc *ahd)
{
    /*
     * On Windows, StorPort already mapped BAR0 in HwFindAdapter.
     * ahd_platform_alloc() saved the virtual address in platform_data.
     * Store it in bshs[0] so ahd_inb/ahd_outb can use it.
     */
    ahd->bshs[0] = (bus_space_handle_t)ahd->platform_data->hw_device_base;
    return (0);
}

static __inline int
ahd_pci_map_int(struct ahd_softc *ahd)
{
    return (0);
}

/************************* Miscellaneous ****************************************/

#define AHD_PCI_CONFIG  1
#define bootverbose     aic79xx_verbose
extern uint32_t aic79xx_verbose;

/* Size helper for scatter-gather segments */
static __inline size_t
ahd_sg_size(struct ahd_softc *ahd)
{
    if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0)
        return sizeof(struct ahd_dma64_seg);
    return sizeof(struct ahd_dma_seg);
}

/* Byte swap helpers */
static __inline uint16_t bswap16(uint16_t x) { return ahd_htobe16(x); }
static __inline uint32_t bswap32(uint32_t x) { return ahd_htobe32(x); }
static __inline uint64_t bswap64(uint64_t x) { return ahd_htobe64(x); }

/************************* scsi_4btoul *****************************************/

static __inline uint32_t
scsi_4btoul(uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8)  |
           ((uint32_t)bytes[3]);
}

/************************* Linux Kernel Compat **********************************/

/* Minimal kernel-mode sprintf — core.c uses only %s, %c, %d, %x, %p.
 * Avoids pulling in user-mode CRT (__mingw_vsprintf).
 */
static __inline int
_ksprintf(char *buf, const char *fmt, ...)
{
    /* Stubs: StorPort has no sprintf. Return 0 (no output).
     * The 3 calls in core.c's ahd_softc_init() are purely for debug strings.
     * A real implementation would use RtlStringCchVPrintf or similar.
     */
    (void)buf;
    (void)fmt;
    return 0;
}
#define sprintf  _ksprintf
#define snprintf _ksnprintf_stub
static __inline int
_ksnprintf_stub(char *buf, size_t size, const char *fmt, ...)
{
    (void)buf; (void)size; (void)fmt;
    return 0;
}

/* roundup macro */
#ifndef roundup
#define roundup(n, d) (((n) + (d) - 1) / (d) * (d))
#endif

/* panic → debug print (no-op on Windows driver) */
#define panic(fmt, ...)  do { DbgPrint("aic79xx PANIC: " fmt, ##__VA_ARGS__); __debugbreak(); } while(0)

/* kzalloc_obj: single-object allocation (no cast — core passes *ptr as type) */
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

/* kmemdup: duplicate memory */
static __inline void *
kmemdup(const void *src, size_t size, int flags)
{
    void *dst = ExAllocatePoolWithTag(NonPagedPool, size, 'hcIA');
    if (dst) memcpy(dst, src, size);
    return dst;
}

/* kmalloc_array: allocate array of objects */
static __inline void *
kmalloc_array(size_t n, size_t size, int flags)
{
    return ExAllocatePoolWithTag(NonPagedPool, n * size, 'hcIA');
}

/* add_timer: no-op stub */
#define add_timer(timer)  ((void)0)


/* GFP flags — no-ops on Windows, all alloc is NonPagedPool */
#define GFP_ATOMIC  0
#define GFP_KERNEL  0

/* kmalloc/kfree compat — maps to ExAllocatePoolWithTag / ExFreePoolWithTag
 * kmalloc_obj is a Linux macro that allocates a single object.
 */
/* kmalloc_obj(*ptr, flags) — allocates sizeof(*ptr), returns void* */
#define kmalloc_obj(type, flags) \
    ExAllocatePoolWithTag(NonPagedPool, sizeof(type), 'hcIA')
#define kzalloc(size, flags) _kzalloc_obj_impl(size)
#define kfree(ptr) do { if (ptr) { ExFreePoolWithTag((ptr), 'hcIA'); (ptr) = NULL; } } while(0)
#define malloc(size) ExAllocatePoolWithTag(NonPagedPool, (size), 'hcIA')

/* memset/memcpy/memcmp — map to NT kernel equivalents */
/* Non-zero memset via byte loop (StorPortZeroMemory only zeros) */
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

/* bzero/bcopy */
#define bzero(p, n)         StorPortZeroMemory((p), (n))
#define bcopy(s, d, n)      StorPortCopyMemory((d), (s), (n))

#endif /* _AIC79XX_OSM_WIN_H_ */
