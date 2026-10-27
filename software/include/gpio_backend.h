/*
 * GPIO backend: one API over the RP1 IO_BANK0 / SYS_RIO / PADS_BANK0 blocks
 * (Pi 5, /dev/gpiomem0), plus a software model of the HAT for tests ("sim").
 *
 * Only GPIO 0..27 (header bank) are handled; masks are bit n = GPIO n.
 *
 * Hot path: gpio_read(), gpio_set(), gpio_clr() are inline register accesses
 * on the hardware backends. Direction and pull changes go through function
 * pointers; they happen per ATN edge, not per byte.
 */
#ifndef GPIO_BACKEND_H
#define GPIO_BACKEND_H

#include <stdint.h>

typedef enum { GPIO_PULL_NONE = 0, GPIO_PULL_UP = 1, GPIO_PULL_DOWN = 2 } gpio_pull_t;

typedef enum {
    GPIO_BACKEND_AUTO = 0,
    GPIO_BACKEND_RP1,
    GPIO_BACKEND_SIM,
} gpio_backend_kind_t;

typedef struct gpio_backend gpio_backend_t;

struct gpio_backend {
    const char *name;
    gpio_backend_kind_t kind;

    /* Fast-path registers; NULL on the sim backend (then the ops below are used) */
    volatile uint32_t *lev;   /* read: bit n = level of GPIO n */
    volatile uint32_t *set;   /* write 1s: output latch high */
    volatile uint32_t *clr;   /* write 1s: output latch low  */

    uint32_t (*read)(gpio_backend_t *b);
    void (*write)(gpio_backend_t *b, uint32_t set_mask, uint32_t clr_mask);

    /* Take the pins in mask under direct control (funcsel), as inputs. */
    int (*claim)(gpio_backend_t *b, uint32_t mask);
    /* Claim, as inputs, only the pins in mask that nothing has claimed yet, so
     * they can be read; claimed pins keep their direction. Drives nothing, so it
     * is safe on a live bus. RP1: an unclaimed pin (no funcsel) reads 0. */
    void (*claim_inputs)(gpio_backend_t *b, uint32_t mask);
    /* Direction. Set the output latch first (gpio_set/gpio_clr) to avoid glitches. */
    void (*make_output)(gpio_backend_t *b, uint32_t mask);
    void (*make_input)(gpio_backend_t *b, uint32_t mask);
    uint32_t (*output_mask)(gpio_backend_t *b);  /* pins currently driven by the Pi */
    void (*set_pull)(gpio_backend_t *b, uint32_t mask, gpio_pull_t pull);
    void (*close)(gpio_backend_t *b);

    void *priv;
};

/* Open a backend. AUTO picks from /proc/device-tree/compatible, or the
 * HAT_BACKEND environment variable (rp1 | sim). Returns NULL and
 * prints the reason on failure. */
gpio_backend_t *gpio_open(gpio_backend_kind_t kind);
gpio_backend_kind_t gpio_kind_from_name(const char *name); /* -1 if unknown */
const char *gpio_kind_name(gpio_backend_kind_t kind);

static inline uint32_t gpio_read(gpio_backend_t *b)
{
    return b->lev ? *b->lev : b->read(b);
}

static inline void gpio_set(gpio_backend_t *b, uint32_t mask)
{
    if (b->set) *b->set = mask; else b->write(b, mask, 0);
}

static inline void gpio_clr(gpio_backend_t *b, uint32_t mask)
{
    if (b->clr) *b->clr = mask; else b->write(b, 0, mask);
}

/* Set and clear in one go (two register writes on hardware, set first). */
static inline void gpio_write(gpio_backend_t *b, uint32_t set_mask, uint32_t clr_mask)
{
    if (b->set) { *b->set = set_mask; *b->clr = clr_mask; }
    else b->write(b, set_mask, clr_mask);
}

/* Backend constructors (gpio_open() dispatches to these) */
gpio_backend_t *gpio_rp1_open(void);
gpio_backend_t *gpio_sim_open(void);

#endif
