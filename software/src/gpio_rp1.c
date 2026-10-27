/*
 * Pi 5 backend: RP1 through /dev/gpiomem0.
 *
 * /dev/gpiomem0 maps the 0x30000-byte RP1 GPIO window that starts at
 * IO_BANK0. Offsets and bit fields are those used by raspberrypi/utils
 * pinctrl/gpiochip_rp1.c (checked 2026-10-16) and the RP1 peripherals
 * datasheet, section 3.1:
 *   IO_BANK0   +0x00000  GPIOn STATUS at 8n, CTRL at 8n+4 (FUNCSEL bits 4:0,
 *                        OUTOVER 13:12, OEOVER 15:14); FUNCSEL 5 = SYS_RIO
 *   SYS_RIO0   +0x10000  OUT 0x0, OE 0x4, SYNC_IN 0x8
 *   PADS_BANK0 +0x20000  GPIOn at 4 + 4n: OD bit 7, IE bit 6, PUE bit 3, PDE bit 2
 * Every block has atomic aliases: +0x1000 XOR, +0x2000 SET, +0x3000 CLR.
 */
#define _GNU_SOURCE
#include "gpio_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAP_LEN        0x30000
#define IO_BANK0       0x00000
#define SYS_RIO0       0x10000
#define PADS_BANK0     0x20000
#define ALIAS_SET      0x2000
#define ALIAS_CLR      0x3000

#define RIO_OUT        0x0
#define RIO_OE         0x4
#define RIO_SYNC_IN    0x8

#define CTRL_FSEL_MASK    0x1f
#define CTRL_OUTOVER_MASK (3u << 12)
#define CTRL_OEOVER_MASK  (3u << 14)
#define FSEL_SYS_RIO      5

#define PAD_OD   (1u << 7)
#define PAD_IE   (1u << 6)
#define PAD_PUE  (1u << 3)
#define PAD_PDE  (1u << 2)

#define R(off) (p->base + (off) / 4)

typedef struct {
    volatile uint32_t *base;
} rp1_priv_t;

static uint32_t rp1_read(gpio_backend_t *b) { return *b->lev; }

static void rp1_write(gpio_backend_t *b, uint32_t s, uint32_t c)
{
    *b->set = s;
    *b->clr = c;
}

static int rp1_claim(gpio_backend_t *b, uint32_t mask)
{
    rp1_priv_t *p = b->priv;
    mask &= 0x0fffffff;
    /* Input first so the funcsel switch cannot glitch an output */
    *R(SYS_RIO0 + ALIAS_CLR + RIO_OE) = mask;
    for (int pin = 0; pin < 28; pin++) {
        if (!(mask & (1u << pin)))
            continue;
        volatile uint32_t *pad = R(PADS_BANK0 + 4 + 4 * pin);
        *pad = (*pad | PAD_IE) & ~PAD_OD;
        volatile uint32_t *ctrl = R(IO_BANK0 + 8 * pin + 4);
        *ctrl = (*ctrl & ~(CTRL_FSEL_MASK | CTRL_OUTOVER_MASK | CTRL_OEOVER_MASK)) | FSEL_SYS_RIO;
    }
    return 0;
}

static void rp1_claim_inputs(gpio_backend_t *b, uint32_t mask)
{
    rp1_priv_t *p = b->priv;
    uint32_t fresh = 0;
    for (int pin = 0; pin < 28; pin++)
        if ((mask & (1u << pin)) && (*R(IO_BANK0 + 8 * pin + 4) & CTRL_FSEL_MASK) != FSEL_SYS_RIO)
            fresh |= 1u << pin;
    if (fresh) rp1_claim(b, fresh);
}

static void rp1_make_output(gpio_backend_t *b, uint32_t mask)
{
    rp1_priv_t *p = b->priv;
    *R(SYS_RIO0 + ALIAS_SET + RIO_OE) = mask & 0x0fffffff;
}

static void rp1_make_input(gpio_backend_t *b, uint32_t mask)
{
    rp1_priv_t *p = b->priv;
    *R(SYS_RIO0 + ALIAS_CLR + RIO_OE) = mask & 0x0fffffff;
}

static uint32_t rp1_output_mask(gpio_backend_t *b)
{
    rp1_priv_t *p = b->priv;
    return *R(SYS_RIO0 + RIO_OE) & 0x0fffffff;
}

static void rp1_set_pull(gpio_backend_t *b, uint32_t mask, gpio_pull_t pull)
{
    rp1_priv_t *p = b->priv;
    for (int pin = 0; pin < 28; pin++) {
        if (!(mask & (1u << pin)))
            continue;
        volatile uint32_t *pad = R(PADS_BANK0 + 4 + 4 * pin);
        uint32_t v = *pad & ~(PAD_PUE | PAD_PDE);
        if (pull == GPIO_PULL_UP) v |= PAD_PUE;
        if (pull == GPIO_PULL_DOWN) v |= PAD_PDE;
        *pad = v;
    }
}

static void rp1_close(gpio_backend_t *b)
{
    rp1_priv_t *p = b->priv;
    munmap((void *)p->base, MAP_LEN);
    free(p);
    free(b);
}

gpio_backend_t *gpio_rp1_open(void)
{
    int fd = open("/dev/gpiomem0", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "rp1: cannot open /dev/gpiomem0: %s\n", strerror(errno));
        return NULL;
    }
    void *m = mmap(NULL, MAP_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        fprintf(stderr, "rp1: mmap failed: %s\n", strerror(errno));
        return NULL;
    }
    gpio_backend_t *b = calloc(1, sizeof *b);
    rp1_priv_t *p = calloc(1, sizeof *p);
    if (!b || !p) {
        free(b);
        free(p);
        munmap(m, MAP_LEN);
        return NULL;
    }
    p->base = m;
    b->name = "rp1";
    b->kind = GPIO_BACKEND_RP1;
    b->lev = R(SYS_RIO0 + RIO_SYNC_IN);
    b->set = R(SYS_RIO0 + ALIAS_SET + RIO_OUT);
    b->clr = R(SYS_RIO0 + ALIAS_CLR + RIO_OUT);
    b->read = rp1_read;
    b->write = rp1_write;
    b->claim = rp1_claim;
    b->claim_inputs = rp1_claim_inputs;
    b->make_output = rp1_make_output;
    b->make_input = rp1_make_input;
    b->output_mask = rp1_output_mask;
    b->set_pull = rp1_set_pull;
    b->close = rp1_close;
    b->priv = p;
    return b;
}
