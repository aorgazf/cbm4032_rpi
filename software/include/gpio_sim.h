/*
 * Software model of the REV03F HAT for tests without hardware.
 *
 * Models the direction logic (U20, U21, U18), the NDAC hold (U22, U23), the
 * translator directions (U3..U11), the board pull resistors and the bus
 * wired-AND, and records contention: any GPIO the Pi drives while the board
 * drives it too. The PET side is a set of levels a test can change, plus an
 * optional hook called on every GPIO read (a step-driven PET model).
 * Like RP1, a pin reads 0 until it is claimed; the pins config.txt sets start
 * out claimed.
 *
 * Levels are electrical: 1 = high = released, 0 = low = asserted.
 */
#ifndef GPIO_SIM_H
#define GPIO_SIM_H

#include "gpio_backend.h"

/* J2 lines that a test can short to ground (probe tests) */
enum {
    SIM_LINE_ATN = 1 << 0, SIM_LINE_REN = 1 << 1, SIM_LINE_IFC = 1 << 2,
    SIM_LINE_DAV = 1 << 3, SIM_LINE_EOI = 1 << 4, SIM_LINE_NDAC = 1 << 5,
    SIM_LINE_NRFD = 1 << 6, SIM_LINE_SRQ = 1 << 7,
    SIM_LINE_DIO1 = 1 << 8,  /* DIOn = SIM_LINE_DIO1 << (n-1) */
};

typedef struct sim_hat sim_hat_t;

typedef struct {
    int atn, ren, ifc, dav, eoi, ndac, nrfd, srq;
    uint8_t dio;            /* electrical level of DIO1..8 (bit i = DIO i+1), 1 = released */
} sim_bus_t;

struct sim_hat {
    /* Pi side */
    uint32_t out, oe, claimed;
    uint8_t pull[28];

    /* PET side drivers, electrical levels (1 = released) */
    int pet_atn, pet_ren, pet_ifc, pet_dav, pet_eoi, pet_ndac, pet_nrfd;
    uint8_t pet_dio;
    uint32_t shorted;       /* SIM_LINE_* lines held to ground */

    /* Results */
    uint32_t contention;    /* sticky: GPIOs both sides drive to different levels */
    uint32_t contention_now;
    uint32_t overlap;       /* sticky: GPIOs both sides drive, same level (no current) */
    uint64_t reads;

    void (*on_read)(sim_hat_t *s, void *ctx);
    void *ctx;
};

sim_hat_t *sim_hat(gpio_backend_t *b);
/* Evaluate the model: GPIO levels as the Pi would read them, and bus levels */
uint32_t sim_levels(sim_hat_t *s, sim_bus_t *bus);
void sim_reset_pet(sim_hat_t *s);   /* all PET lines released, REN released (HAT unplugged) */

#endif
