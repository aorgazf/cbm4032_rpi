#include "pet_model.h"

#include <string.h>

enum { S_NEXT, S_ATN_SETTLE, S_ATN_REL, S_WAIT_NRFD, S_SETTLE, S_WAIT_NDAC, S_IFC, S_DONE };

static void step(sim_hat_t *s, void *ctx)
{
    pet_t *p = ctx;
    sim_bus_t bus;
    sim_levels(s, &bus);

    switch (p->state) {
    case S_NEXT:
        if (p->i == p->ifc_before && !p->ifc_done) {
            p->ifc_done = 1;
            s->pet_atn = s->pet_dav = s->pet_eoi = 1;
            s->pet_dio = 0xff;
            s->pet_ifc = 0;
            p->wait = 50;
            p->state = S_IFC;
            break;
        }
        if (p->i >= p->n) {
            s->pet_atn = 1;
            p->wait = 5000;
            p->state = S_DONE;
            break;
        }
        if (p->frames[p->i].atn && s->pet_atn) {
            s->pet_atn = 0;
            p->wait = 5;
            p->state = S_ATN_SETTLE;
        } else if (!p->frames[p->i].atn && !s->pet_atn) {
            s->pet_atn = 1;
            p->wait = 5;
            p->state = S_ATN_REL;
        } else {
            p->state = S_WAIT_NRFD;
        }
        break;
    case S_ATN_SETTLE:
        if (--p->wait <= 0) {
            if (bus.ndac && bus.nrfd) p->not_present++;   /* DEVICE NOT PRESENT */
            p->state = S_WAIT_NRFD;
            p->t = 0;
        }
        break;
    case S_ATN_REL:
        if (--p->wait <= 0) { p->state = S_WAIT_NRFD; p->t = 0; }
        break;
    case S_WAIT_NRFD:
        if (bus.nrfd) {
            s->pet_dio = (uint8_t)~p->frames[p->i].byte;
            p->wait = 2;
            p->state = S_SETTLE;
        }
        break;
    case S_SETTLE:
        if (--p->wait <= 0) {
            if (bus.ndac && bus.nrfd) p->no_listener++;
            s->pet_dav = 0;
            s->pet_eoi = p->frames[p->i].eoi ? 0 : 1;
            p->t = 0;
            p->state = S_WAIT_NDAC;
        }
        break;
    case S_WAIT_NDAC:
        if (bus.ndac) {
            if (p->delivered < 256) p->log[p->delivered] = p->frames[p->i];
            p->delivered++;
        } else if (++p->t > p->timeout_reads) {
            p->timeouts++;
        } else {
            break;
        }
        s->pet_dav = s->pet_eoi = 1;
        s->pet_dio = 0xff;
        p->i++;
        p->state = S_NEXT;
        break;
    case S_IFC:
        if (--p->wait <= 0) { s->pet_ifc = 1; p->state = S_NEXT; }
        break;
    case S_DONE:
        if (--p->wait <= 0 && p->stop) *p->stop = 1;
        break;
    }
}

void pet_init(pet_t *p, sim_hat_t *s, const pet_frame_t *frames, int n, volatile sig_atomic_t *stop)
{
    memset(p, 0, sizeof *p);
    p->frames = frames;
    p->n = n;
    p->timeout_reads = 200;
    p->ifc_before = -1;
    p->stop = stop;
    sim_reset_pet(s);
    s->on_read = step;
    s->ctx = p;
}
