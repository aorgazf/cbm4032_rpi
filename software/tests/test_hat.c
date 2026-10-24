/*
 * Unit tests on the sim backend: pin math, the safe state, the section 5
 * sequences (no contention), and the monitor's ack mode against a PET model.
 */
#define _GNU_SOURCE
#include "bus_monitor.h"
#include "gpio_sim.h"
#include "hat_board.h"
#include "pet_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); } } while (0)

static void test_pins(void)
{
    hat_pins_init();
    srand(1);
    for (int k = 0; k < 100000; k++) {
        uint32_t raw = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
        uint8_t ref = 0;
        for (int i = 0; i < 8; i++)
            if (raw & BIT(hat_dio_pin[i])) ref |= (uint8_t)(1 << i);
        if (hat_dio_gather(raw) != ref) { CHECK(0, "gather 0x%08x: %02x != %02x", raw, hat_dio_gather(raw), ref); break; }
    }
    for (int v = 0; v < 256; v++) {
        uint32_t raw = (0xffffffffu & ~hat_byte_clr_mask((uint8_t)v)) | hat_byte_set_mask((uint8_t)v);
        CHECK(hat_bus_byte(raw) == v, "byte %02x round trip gives %02x", v, hat_bus_byte(raw));
        CHECK((hat_byte_set_mask((uint8_t)v) | hat_byte_clr_mask((uint8_t)v)) == MASK_DIO, "masks cover DIO");
        CHECK(!(hat_byte_set_mask((uint8_t)v) & hat_byte_clr_mask((uint8_t)v)), "masks disjoint");
    }
    /* DIO1 is GPIO5: bus byte $01 asserts (clears) GPIO5 only */
    CHECK(hat_byte_clr_mask(0x01) == BIT(5), "DIO1 = GPIO5");
    CHECK(hat_byte_clr_mask(0x80) == BIT(17), "DIO8 = GPIO17");
}

static gpio_backend_t *open_sim(sim_hat_t **s)
{
    gpio_backend_t *g = gpio_open(GPIO_BACKEND_SIM);
    *s = sim_hat(g);
    return g;
}

static void test_safe_and_configure(void)
{
    sim_hat_t *s;
    gpio_backend_t *g = open_sim(&s);
    sim_bus_t bus;

    hat_safe_state(g);
    CHECK(hat_check_safe(g, NULL, 0) == 0, "safe after hat_safe_state");
    sim_levels(s, &bus);
    CHECK(bus.dav && bus.eoi && bus.ndac && bus.nrfd && bus.srq && bus.dio == 0xff, "bus released in safe state");

    /* Under ATN with no driver, U23 holds NDAC (brief section 7 table) */
    s->pet_atn = 0;
    sim_levels(s, &bus);
    CHECK(!bus.ndac && bus.nrfd, "safe state under ATN: NDAC held by U23, NRFD released");
    s->pet_atn = 1;

    hat_configure(g);
    uint32_t raw = sim_levels(s, &bus);
    CHECK(s->contention == 0, "configure: no contention (0x%07x)", s->contention);
    CHECK(!(raw & BIT(PIN_DATA_OE_N)), "configure enables U3");
    CHECK(hat_bus_byte(raw) == 0 && bus.dio == 0xff, "configure: data reads released");
    CHECK(bus.ndac && bus.nrfd && bus.dav, "configure: bus released");
    g->close(g);
}

static void test_sequences(void)
{
    sim_hat_t *s;
    gpio_backend_t *g = open_sim(&s);
    sim_bus_t bus;
    hat_configure(g);

    /* ATN falls on an idle listener: takeover keeps NDAC low throughout */
    s->pet_atn = 0;
    sim_levels(s, &bus);
    CHECK(!bus.ndac, "hardware acknowledges ATN");
    hat_atn_takeover(g);
    sim_levels(s, &bus);
    CHECK(!bus.ndac && !bus.nrfd, "takeover: NDAC and NRFD low");
    CHECK(s->contention == 0, "takeover from idle: no contention (0x%07x)", s->contention);

    /* ATN rises, addressed as talker: U23 holds NDAC while roles change */
    s->pet_atn = 1;
    sim_levels(s, &bus);
    CHECK(!bus.ndac, "ATN rise: NDAC still held by U23");
    hat_become_talker(g);
    sim_levels(s, &bus);
    CHECK(s->contention == 0, "become talker: no contention (0x%07x)", s->contention);
    CHECK(bus.dav && bus.eoi && bus.dio == 0xff && bus.ndac && bus.nrfd, "talker idle: all released");

    /* Talk a byte with EOI */
    gpio_write(g, hat_byte_set_mask(0x41), hat_byte_clr_mask(0x41));
    gpio_clr(g, BIT(PIN_DAV) | BIT(PIN_EOI));
    sim_levels(s, &bus);
    CHECK((uint8_t)~bus.dio == 0x41 && !bus.dav && !bus.eoi, "talker drives $41, DAV, EOI");

    /* PET asserts ATN in the middle of the byte: the board turns the
     * translators round, so there is a fight until software reacts */
    s->contention = 0;
    s->pet_atn = 0;
    sim_levels(s, &bus);
    CHECK(s->contention_now != 0, "ATN during talk: fight expected before takeover");
    CHECK(bus.dav && bus.eoi && bus.dio == 0xff, "ATN withdraws the talk enables at once");
    hat_atn_takeover(g);
    sim_levels(s, &bus);
    CHECK(s->contention_now == 0, "takeover ends the fight (0x%07x)", s->contention_now);
    CHECK(!bus.ndac, "NDAC low after takeover");
    CHECK(!(gpio_read(g) & BIT(PIN_CTRL_TE_REQ)), "takeover drops GPIO11");

    /* ATN rises, not addressed: everything released */
    s->contention = 0;
    s->pet_atn = 1;
    hat_listener_release(g);
    sim_levels(s, &bus);
    CHECK(bus.ndac && bus.nrfd, "not addressed: NDAC/NRFD released");
    CHECK(s->contention == 0, "release: no contention");

    /* Talk again, then end the talk normally */
    s->pet_atn = 0; hat_atn_takeover(g); s->pet_atn = 1;
    hat_become_talker(g);
    hat_end_talk(g);
    hat_listener_release(g);
    sim_levels(s, &bus);
    CHECK(s->contention == 0, "end talk: no contention (0x%07x)", s->contention);
    CHECK(bus.dav && bus.eoi && bus.dio == 0xff && bus.ndac, "after talk: released");

    /* Safe state from the talker role */
    s->pet_atn = 0; hat_atn_takeover(g); s->pet_atn = 1;
    hat_become_talker(g);
    gpio_clr(g, BIT(PIN_DAV));
    hat_safe_state(g);
    sim_levels(s, &bus);
    CHECK(hat_check_safe(g, NULL, 0) == 0 && bus.dav && bus.ndac, "safe state from talker");
    g->close(g);
}

static int collect_bytes(mon_ring_t *r, mon_event_t *out, int max, int *atn_ends)
{
    mon_event_t e;
    int n = 0;
    *atn_ends = 0;
    while (mon_pop(r, &e)) {
        if (e.kind == MON_EV_BYTE && n < max) out[n++] = e;
        if (e.kind == MON_EV_ATN_END) (*atn_ends)++;
    }
    return n;
}

static void test_monitor_ack(void)
{
    /* LOAD-like: open a file on device 8, send the name, unlisten; then a
     * LISTEN to device 9, which is not there; then device 8 again */
    static const pet_frame_t script[] = {
        { 0x28, 1, 0 }, { 0xF0, 1, 0 },                         /* LISTEN 8, OPEN 0 */
        { 'H', 0, 0 }, { 'E', 0, 0 }, { 'L', 0, 0 }, { 'L', 0, 0 }, { 'O', 0, 1 },
        { 0x3F, 1, 0 },                                         /* UNLISTEN */
        { 0x29, 1, 0 }, { 0x61, 1, 0 },                         /* LISTEN 9, DATA 1 */
        { 'X', 0, 1 },                                          /* nobody acknowledges */
        { 0x3F, 1, 0 },
        { 0x28, 1, 0 }, { 0x61, 1, 0 }, { 'A', 0, 0 },           /* IFC before 'B' */
        { 'B', 0, 1 },
        { 0x28, 1, 0 }, { 0x61, 1, 0 }, { 'C', 0, 1 }, { 0x3F, 1, 0 },
    };
    enum { N = sizeof script / sizeof script[0] };
    static volatile sig_atomic_t stop;
    sim_hat_t *s;
    gpio_backend_t *g = open_sim(&s);
    pet_t pet;
    pet_init(&pet, s, script, N, &stop);
    pet.ifc_before = 15;
    s->pet_ren = 0;   /* the PET ties REN to ground */

    mon_ring_t *ring = calloc(1, sizeof *ring);
    mon_ctx_t mon = { .g = g, .ring = ring, .stop = &stop, .ack = 1, .device = 8 };
    stop = 0;
    mon_run(&mon);
    hat_safe_state(g);

    CHECK(pet.not_present == 0, "device present at every ATN (%d)", pet.not_present);
    CHECK(s->contention == 0, "ack run: no contention (0x%07x)", s->contention);
    /* 'X' to absent device 9 and 'B' (after IFC unaddressed the HAT) find no
     * listener: NDAC and NRFD both released, so the PET's handshake falls through */
    CHECK(pet.timeouts == 0, "timeouts: %d", pet.timeouts);
    CHECK(pet.no_listener == 2, "bytes with no listener: %d (expected 2: 'X' and 'B')", pet.no_listener);
    CHECK(pet.delivered == N, "delivered %d of %d", pet.delivered, N);

    mon_event_t ev[64];
    int atn_ends;
    int nb = collect_bytes(ring, ev, 64, &atn_ends);
    CHECK(nb == N - 2, "monitor saw %d bytes", nb);
    int j = 0;
    for (int i = 0; i < N && j < nb; i++) {
        if (i == 10 || i == 15) continue;   /* the two timed-out bytes */
        CHECK(ev[j].byte == script[i].byte, "byte %d: $%02X != $%02X", j, ev[j].byte, script[i].byte);
        CHECK(!!(ev[j].flags & MON_F_ATN) == script[i].atn, "byte %d ATN flag", j);
        CHECK(!!(ev[j].flags & MON_F_EOI) == script[i].eoi, "byte %d EOI flag", j);
        j++;
    }
    CHECK(atn_ends >= 5, "ATN phases ended: %d", atn_ends);
    free(ring);
    g->close(g);
}

static void test_monitor_passive(void)
{
    static const pet_frame_t script[] = { { 0x28, 1, 0 }, { 0xF0, 1, 0 } };
    static volatile sig_atomic_t stop;
    sim_hat_t *s;
    gpio_backend_t *g = open_sim(&s);
    pet_t pet;
    pet_init(&pet, s, script, 2, &stop);

    mon_ring_t *ring = calloc(1, sizeof *ring);
    mon_ctx_t mon = { .g = g, .ring = ring, .stop = &stop, .ack = 0 };
    stop = 0;
    mon_run(&mon);
    hat_safe_state(g);

    /* U23 makes the HAT look present, but nobody releases NDAC */
    CHECK(pet.not_present == 0, "passive: present via U23");
    CHECK(pet.delivered == 0 && pet.timeouts == 2, "passive: every byte times out (%d/%d)",
          pet.delivered, pet.timeouts);
    CHECK(s->contention == 0, "passive: no contention");
    mon_event_t ev[8];
    int atn_ends;
    int nb = collect_bytes(ring, ev, 8, &atn_ends);
    CHECK(nb == 2 && ev[0].byte == 0x28 && (ev[0].flags & MON_F_ATN), "passive saw the bytes (%d)", nb);
    free(ring);
    g->close(g);
}

int main(void)
{
    test_pins();
    test_safe_and_configure();
    test_sequences();
    test_monitor_ack();
    test_monitor_passive();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
