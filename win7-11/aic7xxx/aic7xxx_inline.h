/*
 * Windows forward declarations and inlines for AIC7xxx core functions.
 * The Linux aic7xxx_inline.h has ahc_name(), sense buffer access,
 * and forward declarations. We keep the inlines that don't depend
 * on Linux and forward-declare the rest.
 *
 * Copyright (c) 2026 Andrei Kazialetski. MIT License.
 */
#ifndef _AIC7XXX_INLINE_WIN_H_
#define _AIC7XXX_INLINE_WIN_H_

#include "aic7xxx_osm.h"

/******************************** Debugging ***********************************/

static __inline char *
ahc_name(struct ahc_softc *ahc)
{
    return (ahc->name);
}

/************************** Sense Buffer Access *******************************/

struct scsi_sense_data *
ahc_get_sense_buf(struct ahc_softc *ahc, struct scb *scb);

/************************** Forward Declarations ******************************/

struct ahc_initiator_tinfo *
    ahc_fetch_transinfo(struct ahc_softc *ahc, char channel, u_int our_id,
                        u_int remote_id, struct ahc_tmode_tstate **tstate);

uint16_t ahc_inw(struct ahc_softc *ahc, u_int port);
void     ahc_outw(struct ahc_softc *ahc, u_int port, u_int value);
uint32_t ahc_inl(struct ahc_softc *ahc, u_int port);
void     ahc_outl(struct ahc_softc *ahc, u_int port, uint32_t value);
uint64_t ahc_inq(struct ahc_softc *ahc, u_int port);
void     ahc_outq(struct ahc_softc *ahc, u_int port, uint64_t value);

int      ahc_is_paused(struct ahc_softc *ahc);
void     ahc_pause(struct ahc_softc *ahc);
void     ahc_unpause(struct ahc_softc *ahc);

struct scb *ahc_get_scb(struct ahc_softc *ahc);
void        ahc_free_scb(struct ahc_softc *ahc, struct scb *scb);
struct scb *ahc_lookup_scb(struct ahc_softc *ahc, u_int tag);
void        ahc_queue_scb(struct ahc_softc *ahc, struct scb *scb);

void ahc_sync_sglist(struct ahc_softc *ahc, struct scb *scb, int op);

/************************** Interrupt Processing *******************************/

int ahc_intr(struct ahc_softc *ahc);

#endif  /* _AIC7XXX_INLINE_WIN_H_ */
