/*
 * Step-driven model of the PET as bus controller and talker, for the sim
 * backend. It advances one step on every GPIO read the device code makes,
 * so time is counted in reads: deterministic, and independent of host speed.
 *
 * Follows the PET behaviour of the driver brief, section 6: ATN asserted for
 * command bytes, device-present check after ATN, DAV timeout while waiting
 * for NDAC, EOI with DAV on the last byte, IFC on reset.
 */
#ifndef PET_MODEL_H
#define PET_MODEL_H

#include <signal.h>
#include "gpio_sim.h"

typedef struct {
    uint8_t byte;
    uint8_t atn;          /* 1: command byte under ATN */
    uint8_t eoi;          /* 1: EOI with DAV */
} pet_frame_t;

typedef struct {
    const pet_frame_t *frames;
    int n, i;
    int state;
    long wait, t;
    long timeout_reads;   /* stands in for the 64 us DAV timeout */
    int ifc_before;       /* pulse IFC before this frame index (-1: never) */
    int ifc_done;
    volatile sig_atomic_t *stop;

    /* Results */
    int delivered, timeouts, not_present;
    int no_listener;      /* DAV asserted with NDAC already high: nobody listening */
    pet_frame_t log[256]; /* frames the listener acknowledged */
} pet_t;

void pet_init(pet_t *p, sim_hat_t *s, const pet_frame_t *frames, int n, volatile sig_atomic_t *stop);

#endif
