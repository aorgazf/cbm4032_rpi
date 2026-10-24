#define _GNU_SOURCE
#include "bus_monitor.h"
#include "hat_board.h"
#include "rt_util.h"

#include <stdio.h>

/* ---- ring (single producer: poll thread, single consumer: printer) ---- */

static inline void push(mon_ctx_t *c, uint8_t kind, uint64_t t, uint32_t raw,
                        uint8_t byte, uint8_t flags, uint32_t lat)
{
    mon_ring_t *r = c->ring;
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t tl = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - tl >= MON_RING_SIZE) {
        atomic_fetch_add_explicit(&r->dropped, 1, memory_order_relaxed);
        return;
    }
    mon_event_t *e = &r->ev[h & (MON_RING_SIZE - 1)];
    e->t = t;
    e->raw = raw;
    e->lat = lat;
    e->kind = kind;
    e->byte = byte;
    e->flags = flags;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
}

int mon_pop(mon_ring_t *r, mon_event_t *e)
{
    uint32_t tl = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tl == h)
        return 0;
    *e = r->ev[tl & (MON_RING_SIZE - 1)];
    atomic_store_explicit(&r->tail, tl + 1, memory_order_release);
    return 1;
}

const char *mon_command_name(uint8_t b, char *buf)
{
    if (b == 0x3F) snprintf(buf, 24, "UNLISTEN");
    else if (b == 0x5F) snprintf(buf, 24, "UNTALK");
    else if ((b & 0xE0) == 0x20) snprintf(buf, 24, "LISTEN %d", b & 0x1F);
    else if ((b & 0xE0) == 0x40) snprintf(buf, 24, "TALK %d", b & 0x1F);
    else if ((b & 0xF0) == 0x60) snprintf(buf, 24, "DATA ch %d", b & 0x0F);
    else if ((b & 0xF0) == 0xE0) snprintf(buf, 24, "CLOSE ch %d", b & 0x0F);
    else if ((b & 0xF0) == 0xF0) snprintf(buf, 24, "OPEN ch %d", b & 0x0F);
    else snprintf(buf, 24, "CMD $%02X", b);
    return buf;
}

/* ---- polling ---- */

typedef struct {
    uint32_t prev;
    uint64_t last_t;
    unsigned n;
} poll_state_t;

static inline uint32_t poll(mon_ctx_t *c, poll_state_t *s)
{
    uint32_t r = gpio_read(c->g);
    uint64_t t = rt_ticks();
    uint64_t gap = t - s->last_t;
    if (gap > c->max_gap) c->max_gap = gap;
    s->last_t = t;
    if ((r ^ s->prev) & MON_WATCH) {
        push(c, MON_EV_EDGE, t, r, 0, 0, 0);
        s->prev = r;
    }
    return r;
}

static inline int stopped(mon_ctx_t *c, poll_state_t *s)
{
    return !(++s->n & 0x3ff) && *c->stop;
}

static void run_passive(mon_ctx_t *c, poll_state_t *s)
{
    for (;;) {
        uint32_t before = s->prev;
        uint32_t r = poll(c, s);
        if ((before & BIT(PIN_DAV)) && !(r & BIT(PIN_DAV))) {
            uint32_t r2 = gpio_read(c->g);   /* data settled before DAV (IEEE-488 T1) */
            uint8_t f = (!(r2 & BIT(PIN_ATN)) ? MON_F_ATN : 0) | (!(r2 & BIT(PIN_EOI)) ? MON_F_EOI : 0);
            push(c, MON_EV_BYTE, s->last_t, r2, hat_bus_byte(r2), f, 0);
            c->bytes++;
        }
        if (stopped(c, s)) return;
    }
}

/* ---- ack mode: listener handshake of section 5 ---- */

enum { HS_OK, HS_ATN, HS_IFC, HS_STOP };

static void track_command(mon_ctx_t *c, uint8_t b)
{
    if (b == 0x3F) c->listen = 0;
    else if (b == 0x5F) c->talk = 0;
    else if ((b & 0xE0) == 0x20) { if ((b & 0x1F) == c->device) c->listen = 1; }
    else if ((b & 0xE0) == 0x40) c->talk = ((b & 0x1F) == c->device);
}

/*
 * One byte: release NRFD, wait DAV low, read DIO, pull NRFD low, release
 * NDAC, wait DAV high, pull NDAC low. Returns HS_ATN if ATN leaves the state
 * given by under_atn while waiting for DAV.
 */
static int handshake(mon_ctx_t *c, poll_state_t *s, int under_atn)
{
    gpio_backend_t *g = c->g;
    uint32_t r;

    gpio_set(g, BIT(PIN_NRFD));
    for (;;) {
        r = poll(c, s);
        if (!(r & BIT(PIN_IFC))) return HS_IFC;
        if ((!(r & BIT(PIN_ATN))) != under_atn) return HS_ATN;
        if (!(r & BIT(PIN_DAV))) break;
        if (stopped(c, s)) return HS_STOP;
    }
    uint64_t t0 = s->last_t;
    r = gpio_read(g);
    gpio_clr(g, BIT(PIN_NRFD));
    gpio_set(g, BIT(PIN_NDAC));
    uint64_t lat = rt_ticks() - t0;

    uint8_t b = hat_bus_byte(r);
    uint8_t f = MON_F_ACKED | (under_atn ? MON_F_ATN : 0) | (!(r & BIT(PIN_EOI)) ? MON_F_EOI : 0);
    push(c, MON_EV_BYTE, t0, r, b, f, (uint32_t)lat);
    if (lat > c->max_lat) c->max_lat = lat;
    c->bytes++;
    if (under_atn) track_command(c, b);

    for (;;) {
        r = poll(c, s);
        if (!(r & BIT(PIN_IFC))) return HS_IFC;
        if (r & BIT(PIN_DAV)) break;
        if (stopped(c, s)) return HS_STOP;
    }
    gpio_clr(g, BIT(PIN_NDAC));
    return HS_OK;
}

static int atn_phase(mon_ctx_t *c, poll_state_t *s)
{
    gpio_backend_t *g = c->g;
    hat_atn_takeover(g);
    push(c, MON_EV_ATN_START, rt_ticks(), gpio_read(g), 0, 0, 0);

    int rc;
    while ((rc = handshake(c, s, 1)) == HS_OK)
        ;
    if (rc != HS_ATN)
        return rc;

    /* ATN released. NDAC is held by GPIO18 and by U23 (ATN high, ATNA low). */
    uint8_t role = c->talk ? MON_ROLE_TALK : c->listen ? MON_ROLE_LISTEN : MON_ROLE_NONE;
    push(c, MON_EV_ATN_END, rt_ticks(), gpio_read(g), role, 0, 0);
    if (role == MON_ROLE_LISTEN)
        hat_atna_high(g);          /* stay listener; GPIO18 keeps NDAC low */
    else
        hat_listener_release(g);   /* not addressed, or talker (not supported here) */
    return HS_OK;
}

static void run_ack(mon_ctx_t *c, poll_state_t *s)
{
    gpio_backend_t *g = c->g;
    c->listen = c->talk = 0;
    for (;;) {
        uint32_t r = poll(c, s);
        int rc = HS_OK;
        if (!(r & BIT(PIN_IFC))) {
            rc = HS_IFC;
        } else if (!(r & BIT(PIN_ATN))) {
            rc = atn_phase(c, s);
        } else if (c->listen) {
            rc = handshake(c, s, 0);   /* HS_ATN here means ATN fell: next pass takes over */
        } else if (stopped(c, s)) {
            return;
        }
        if (rc == HS_STOP)
            return;
        if (rc == HS_IFC) {
            hat_listener_release(g);
            c->listen = c->talk = 0;
            push(c, MON_EV_IFC, s->last_t, r, 0, 0, 0);
            while (!(poll(c, s) & BIT(PIN_IFC)))
                if (stopped(c, s)) return;
        }
    }
}

void mon_run(mon_ctx_t *c)
{
    poll_state_t s = { 0 };
    hat_configure(c->g);
    s.prev = gpio_read(c->g);
    s.last_t = rt_ticks();
    push(c, MON_EV_START, s.last_t, s.prev, 0, 0, 0);
    c->max_gap = 0;
    if (c->ack) run_ack(c, &s);
    else run_passive(c, &s);
}
