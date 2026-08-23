/*
 * Shared OSM (OS Module) definitions for AIC-7xxx/79xx Windows StorPort drivers.
 * Provides Linux compatibility macros, type definitions, CAM/SCSI constants,
 * SCSI message constants, SAM/SCSI status codes, bus/DMA types, shadow SCSI
 * structs, PCI definitions, kernel-compat allocation helpers, and DDK stubs.
 *
 * Included by each driver's OSM header AFTER system includes and core header.
 * Everything here is identical between both drivers.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AHC_OSM_COMMON_H_
#define _AHC_OSM_COMMON_H_

/*************************** Linux Compatibility Macros ************************/

#define __KERNEL__
#define __LITTLE_ENDIAN      1234
#define __BYTE_ORDER         __LITTLE_ENDIAN
#define __printf_like(a, b)
#define __VA_OPT(...)
#define __maybe_unused
#define likely(x)            (x)
#define unlikely(x)          (x)
#define powerof2(x)          ((((x)-1)&(x))==0)

#define container_of(ptr, type, member)     ((type *)((char *)(ptr) - offsetof(type, member)))
#define timer_container_of(var, callback_timer, timer_fieldname)     container_of(callback_timer, typeof(*var), timer_fieldname)

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

#define DELAY(us)            StorPortStallExecution((us))
#define mb()                 MemoryBarrier()
#define wmb()                MemoryBarrier()
#define rmb()                MemoryBarrier()

/***************************** Integer Types **********************************/

#ifndef _U_INT_DEFINED
#define _U_INT_DEFINED
typedef unsigned int        u_int;
#endif
typedef unsigned long       u_long;
typedef unsigned long       dma_addr_t;
typedef unsigned long       resource_size_t;

/************************* CAM Constants **************************************/

#define CAM_STATUS_MASK             0x000000FF
#define CAM_REQ_INPROG              0x00
#define CAM_REQ_CMP                 0x01
#define CAM_REQ_ABORTED             0x02
#define CAM_UA_ABORT                0x03
#define CAM_REQ_CMP_ERR             0x04
#define CAM_BUSY                    0x05
#define CAM_REQ_INVALID             0x06
#define CAM_PATH_INVALID            0x07
#define CAM_SEL_TIMEOUT             0x08
#define CAM_CMD_TIMEOUT             0x09
#define CAM_SCSI_STATUS_ERROR       0x0A
#define CAM_SCSI_BUS_RESET          0x0B
#define CAM_UNCOR_PARITY            0x0C
#define CAM_AUTOSENSE_FAIL          0x0D
#define CAM_NO_HBA                  0x0E
#define CAM_DATA_RUN_ERR            0x0F
#define CAM_UNEXP_BUSFREE           0x10
#define CAM_SEQUENCE_FAIL           0x11
#define CAM_CCB_LEN_ERR             0x12
#define CAM_PROVIDE_FAIL            0x13
#define CAM_BDR_SENT                0x14
#define CAM_REQ_TERMIO              0x15
#define CAM_UNREC_HBA_ERROR         0x16
#define CAM_REQ_TOO_BIG             0x17
#define CAM_UA_TERMIO               0x18
#define CAM_MSG_REJECT_REC          0x19
#define CAM_DEV_NOT_THERE           0x1A
#define CAM_RESRC_UNAVAIL           0x1B
#define CAM_REQUEUE_REQ             0x1C
#define CAM_DEV_QFRZN               0x40

#define CAM_DIS_DISCONNECT     0x0080
#define CAM_FUNC_NOTAVAIL      0x0040
#define CAM_LUN_ALRDY_ENA      0x0010
#define CAM_LUN_INVALID        0x0020
#define CAM_TID_INVALID        0x0008
#define CAM_TAG_ACTION_VALID   0x0004
#define CAM_CDB_RECVD          0x0002
#define CAM_MESSAGE_RECV       0x0001

#define CAM_DIR_IN     0x01
#define CAM_DIR_OUT    0x02
#define CAM_DIR_NONE   0x00

#define CAM_TARGET_WILDCARD    ((u_int)~0)
#define CAM_LUN_WILDCARD       ((u_int)~0)
#define CAM_BUS_WILDCARD       ((u_int)~0)

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

#define MSG_SIMPLE_Q_TAG        0x20
#define MSG_HEAD_OF_Q_TAG       0x21
#define MSG_ORDERED_Q_TAG       0x22
#define MSG_LINK_CMD_COMPLETE   0x2A
#define MSG_LINK_CMD_COMPLETE_FLAG 0x2B

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

/************************* SAM / SCSI Status ***********************************/

#define SAM_STAT_GOOD                0x00
#define SAM_STAT_CHECK_CONDITION     0x02
#define SAM_STAT_COMMAND_TERMINATED  0x22

#define SCSI_STATUS_OK              0x00
#define SCSI_STATUS_CHECK_COND      0x02
#define SCSI_STATUS_COND_MET        0x04
#define SCSI_STATUS_BUSY            0x08
#define SCSI_STATUS_INTERMED        0x10
#define SCSI_STATUS_RESERV_CONFLICT 0x18
#define SCSI_STATUS_CMD_TERMINATED  0x22
#define SCSI_STATUS_QUEUE_FULL      0x28

/************************* ac_code ********************************************/

typedef uint32_t ac_code;

/************************* Timer Stubs ****************************************/

#define HZ      1000
#define jiffies 0
#define timer_setup(timer, callback, flags)  ((void)0)
#define timer_delete(timer)                  ((void)0)
#define timer_delete_sync(timer)             ((void)0)
#define timer_reset(timer, expiration)       ((void)0)
#define add_timer(timer)  ((void)0)

#ifdef __GNUC__
#define fallthrough __attribute__((fallthrough))
#else
#define fallthrough ((void)0)
#endif

#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

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

/************************* Shadow SCSI Types **********************************/

struct scsi_device {
    int     qfrozen;
};

struct scsi_cmnd {
    uint32_t             result;
    struct scsi_device  *device;
    uint32_t             sc_data_direction;
    uint32_t             resid;
};

#define scsi_set_resid(cmd, r)  ((cmd)->resid = (uint32_t)(r))
#define scsi_get_resid(cmd)     ((cmd)->resid)

/************************* Bus Space / DMA Types *******************************/

typedef size_t              bus_size_t;

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

#define BUS_DMASYNC_PREREAD     0x01
#define BUS_DMASYNC_POSTREAD    0x02
#define BUS_DMASYNC_PREWRITE    0x04
#define BUS_DMASYNC_POSTWRITE   0x08

/************************* MinGW DDK Compat Defines **************************/

#ifndef PCI_COMMON_COMMAND
#define PCI_COMMON_COMMAND  0x04
#endif

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
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_SUBSYSTEM_ID        0x2E

/************************* SCSI Tag Types **************************************/

#define SIMPLE_QUEUE_TAG    0x20
#define ORDERED_QUEUE_TAG   0x22
#define HEAD_OF_Q_TAG       0x21
#define UNTAGGED            0x00

/************************* SCSI Status IU **************************************/

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
#define SIU_SENSE_OFFSET(siu)     (12 + (((siu)->flags & SIU_RSPVALID) \
    ? scsi_4btoul((siu)->pkt_failures_length) : 0))
#define SIU_TASKMGMT_NONE         0x00
#define SIU_TASKMGMT_ABORT_TASK   0x01
#define SIU_TASKMGMT_ABORT_TASK_SET 0x02
#define SIU_TASKMGMT_CLEAR_TASK_SET 0x04
#define SIU_TASKMGMT_LUN_RESET    0x08
#define SIU_TASKMGMT_TARGET_RESET 0x20
#define SIU_TASKMGMT_CLEAR_ACA    0x40

/************************* Linux Device Flags **********************************/

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

/************************* Power State *****************************************/

typedef enum {
    AHD_POWER_STATE_D0,
    AHD_POWER_STATE_D1,
    AHD_POWER_STATE_D2,
    AHD_POWER_STATE_D3
} ahd_power_state;

/************************* Stub struct timer_list ******************************/

struct timer_list {
    void    (*function)(unsigned long);
    unsigned long data;
    unsigned long expires;
};

/************************* SPI Transport Stubs ********************************/

struct scsi_pointer;
static __inline int spi_populate_width_msg(uint8_t *msg, int width) { (void)msg; (void)width; return 0; }
static __inline int spi_populate_sync_msg(uint8_t *msg, int period, int offset) { (void)msg; (void)period; (void)offset; return 0; }
static __inline int spi_populate_ppr_msg(uint8_t *msg, int period, int offset, int width, int flags) { (void)msg; (void)period; (void)offset; (void)width; (void)flags; return 0; }

/************************* Memory Barrier **************************************/

#ifndef MemoryBarrier
#define MemoryBarrier()  __sync_synchronize()
#endif

/************************* Byte Order Helpers (generic) ************************/

static __inline uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
static __inline uint32_t bswap32(uint32_t x) {
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}
static __inline uint64_t bswap64(uint64_t x) {
    return ((uint64_t)bswap32((uint32_t)x) << 32) | bswap32((uint32_t)(x >> 32));
}

/************************* scsi_4btoul *****************************************/

static __inline uint32_t
scsi_4btoul(uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8)  |
           ((uint32_t)bytes[3]);
}

/************************* Kernel Compat Allocation ****************************/

#define GFP_ATOMIC  0
#define GFP_KERNEL  0

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

static __inline void *
kmemdup(const void *src, size_t size, int flags)
{
    void *dst = ExAllocatePoolWithTag(NonPagedPool, size, 'hcIA');
    if (dst) memcpy(dst, src, size);
    return dst;
}

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

#define panic(fmt, ...)  do { DbgPrint(fmt, ##__VA_ARGS__); __debugbreak(); } while(0)

#ifndef roundup
#define roundup(n, d) (((n) + (d) - 1) / (d) * (d))
#endif

static __inline int
_ksprintf(char *buf, const char *fmt, ...)
{
    (void)buf; (void)fmt;
    return 0;
}
#define sprintf  _ksprintf
static __inline int
_ksnprintf_stub(char *buf, size_t size, const char *fmt, ...)
{
    (void)buf; (void)size; (void)fmt;
    return 0;
}
#define snprintf _ksnprintf_stub

/************************* DDK Inline Stubs ***********************************/

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

#endif /* _AHC_OSM_COMMON_H_ */
