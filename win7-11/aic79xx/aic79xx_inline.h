/*
 * Windows forward declarations for AIC79xx core functions.
 * The Linux aic79xx_inline.h has ahd_name(), mode_state helpers,
 * and sg_size. We keep the inlines that don't depend on Linux,
 * and forward-declare the rest.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AIC79XX_INLINE_H_
#define _AIC79XX_INLINE_H_

#include "aic79xx_osm.h"

/******************************** Debugging ***********************************/

static __inline char *
ahd_name(struct ahd_softc *ahd)
{
    return (ahd->name);
}

/************************ Sequencer Execution Control *************************/

static __inline void
ahd_known_modes(struct ahd_softc *ahd, ahd_mode src, ahd_mode dst)
{
    ahd->src_mode = src;
    ahd->dst_mode = dst;
    ahd->saved_src_mode = src;
    ahd->saved_dst_mode = dst;
}

static __inline ahd_mode_state
ahd_build_mode_state(struct ahd_softc *ahd, ahd_mode src, ahd_mode dst)
{
    UNREFERENCED_PARAMETER(ahd);
    return ((src << SRC_MODE_SHIFT) | (dst << DST_MODE_SHIFT));
}

static __inline void
ahd_extract_mode_state(struct ahd_softc *ahd, ahd_mode_state state,
                       ahd_mode *src, ahd_mode *dst)
{
    UNREFERENCED_PARAMETER(ahd);
    *src = (state & SRC_MODE) >> SRC_MODE_SHIFT;
    *dst = (state & DST_MODE) >> DST_MODE_SHIFT;
}

/************************** Sense Buffer Access *******************************/

static __inline uint8_t *
ahd_get_sense_buf(struct ahd_softc *ahd, struct scb *scb)
{
    UNREFERENCED_PARAMETER(ahd);
    return (scb->sense_data);
}

static __inline uint32_t
ahd_get_sense_bufaddr(struct ahd_softc *ahd, struct scb *scb)
{
    UNREFERENCED_PARAMETER(ahd);
    return (scb->sense_busaddr);
}

/************************** Forward Declarations ******************************/

struct ahd_initiator_tinfo *
    ahd_fetch_transinfo(struct ahd_softc *ahd, char channel, u_int our_id,
                        u_int remote_id, struct ahd_tmode_tstate **tstate);

uint16_t ahd_inw(struct ahd_softc *ahd, u_int port);
void     ahd_outw(struct ahd_softc *ahd, u_int port, u_int value);

u_int    ahd_get_scbptr(struct ahd_softc *ahd);
void     ahd_set_scbptr(struct ahd_softc *ahd, u_int scbptr);
u_int    ahd_inb_scbram(struct ahd_softc *ahd, u_int offset);
u_int    ahd_inw_scbram(struct ahd_softc *ahd, u_int offset);

struct scb *ahd_lookup_scb(struct ahd_softc *ahd, u_int tag);
void        ahd_queue_scb(struct ahd_softc *ahd, struct scb *scb);

void ahd_sync_sglist(struct ahd_softc *ahd, struct scb *scb, int op);
void *ahd_sg_setup(struct ahd_softc *ahd, struct scb *scb, void *sgptr,
                   dma_addr_t addr, bus_size_t len, int last);

/************************** Interrupt Processing *******************************/

int ahd_intr(struct ahd_softc *ahd);

#endif  /* _AIC79XX_INLINE_H_ */
