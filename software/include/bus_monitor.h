/*
 * Bus monitor core: a busy-poll loop that records line changes and bytes
 * into a single-producer / single-consumer ring for a normal-priority
 * thread to print.
 *
 * Passive mode drives nothing: the PET sees the HAT exactly as with no
 * driver (U23 acknowledges ATN, then the first byte times out), so every
 * command attempt shows one byte.
 *
 * Ack mode is a minimal listener (driver brief, section 5): it accepts every
 * command byte under ATN and, when addressed as listener on its device
 * number, every data byte. It never talks, so a TALK to it ends in the PET's
 * DAV timeout, as with a drive that has nothing to send.
 */
#ifndef BUS_MONITOR_H
#define BUS_MONITOR_H

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include "gpio_backend.h"
#include "hat_pins.h"

enum {
    MON_EV_EDGE = 1,      /* watched lines changed; raw = new level word */
    MON_EV_BYTE,          /* byte seen at DAV low; flags below; lat = DAV low seen -> NDAC released */
    MON_EV_ATN_START,     /* ack mode: took over NDAC under ATN */
    MON_EV_ATN_END,       /* ack mode: ATN released; byte = role (MON_ROLE_*) */
    MON_EV_IFC,           /* IFC asserted: roles dropped */
    MON_EV_START,
};

enum { MON_F_ATN = 1, MON_F_EOI = 2, MON_F_ACKED = 4 };
enum { MON_ROLE_NONE = 0, MON_ROLE_LISTEN = 1, MON_ROLE_TALK = 2 };

typedef struct {
    uint64_t t;           /* rt_ticks() */
    uint32_t raw;
    uint32_t lat;         /* ticks */
    uint8_t kind, byte, flags, pad;
} mon_event_t;

#define MON_RING_SIZE (1u << 16)

typedef struct {
    _Atomic uint32_t head, tail;
    _Atomic uint32_t dropped;
    mon_event_t ev[MON_RING_SIZE];
} mon_ring_t;

typedef struct {
    gpio_backend_t *g;
    mon_ring_t *ring;
    volatile sig_atomic_t *stop;
    int ack;              /* 0: passive, 1: listener ack mode */
    int device;           /* primary address for ack mode (default 8) */

    /* Statistics, written by the poll thread */
    uint64_t bytes, max_lat, max_gap;
    int listen, talk;     /* ack-mode addressing state */
} mon_ctx_t;

/* Watched lines for edge events */
#define MON_WATCH (BIT(PIN_ATN) | BIT(PIN_DAV) | BIT(PIN_EOI) | BIT(PIN_NDAC) | \
                   BIT(PIN_NRFD) | BIT(PIN_IFC) | BIT(PIN_REN))

/* Configure the HAT for monitoring (U3 receive) and run until *stop. */
void mon_run(mon_ctx_t *c);

/* Consumer side */
int mon_pop(mon_ring_t *r, mon_event_t *e);

/* Name of a command byte sent under ATN, e.g. "LISTEN 8" (buf >= 24) */
const char *mon_command_name(uint8_t b, char *buf);

#endif
