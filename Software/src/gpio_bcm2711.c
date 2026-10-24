/*
 * Pi 4 backend: BCM2711 GPIO block through /dev/gpiomem.
 *
 * Register offsets from the BCM2711 ARM Peripherals datasheet, section 5.2
 * (GPIO register view): GPFSEL0..5 at 0x00..0x14 (3 bits per pin, 000 input,
 * 001 output), GPSET0 0x1C, GPCLR0 0x28, GPLEV0 0x34, and
 * GPIO_PUP_PDN_CNTRL_REG0..3 at 0xE4..0xF0 (2 bits per pin: 00 none, 01 up,
 * 10 down). /dev/gpiomem maps the block at offset 0.
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

#define GPFSEL0   (0x00 / 4)
#define GPSET0    (0x1C / 4)
#define GPCLR0    (0x28 / 4)
#define GPLEV0    (0x34 / 4)
#define GPPUPPDN0 (0xE4 / 4)
#define MAP_LEN   4096

typedef struct {
    volatile uint32_t *regs;
} bcm_priv_t;

static uint32_t bcm_read(gpio_backend_t *b) { return *b->lev; }

static void bcm_write(gpio_backend_t *b, uint32_t s, uint32_t c)
{
    *b->set = s;
    *b->clr = c;
}

/* Set the 3-bit function field of every pin in mask (pins 0..27, FSEL0..2) */
static void bcm_fsel(gpio_backend_t *b, uint32_t mask, uint32_t func)
{
    volatile uint32_t *r = ((bcm_priv_t *)b->priv)->regs;
    for (int reg = 0; reg < 3; reg++) {
        uint32_t clr = 0, val = 0;
        for (int i = 0; i < 10; i++) {
            int pin = reg * 10 + i;
            if (pin < 28 && (mask & (1u << pin))) {
                clr |= 7u << (i * 3);
                val |= func << (i * 3);
            }
        }
        if (clr)
            r[GPFSEL0 + reg] = (r[GPFSEL0 + reg] & ~clr) | val;
    }
}

static int bcm_claim(gpio_backend_t *b, uint32_t mask)
{
    bcm_fsel(b, mask, 0);   /* input; on BCM2711 the funcsel is the direction */
    return 0;
}

static void bcm_make_output(gpio_backend_t *b, uint32_t mask) { bcm_fsel(b, mask, 1); }
static void bcm_make_input(gpio_backend_t *b, uint32_t mask) { bcm_fsel(b, mask, 0); }

static uint32_t bcm_output_mask(gpio_backend_t *b)
{
    volatile uint32_t *r = ((bcm_priv_t *)b->priv)->regs;
    uint32_t out = 0;
    for (int pin = 0; pin < 28; pin++)
        if (((r[GPFSEL0 + pin / 10] >> ((pin % 10) * 3)) & 7) == 1)
            out |= 1u << pin;
    return out;
}

static void bcm_set_pull(gpio_backend_t *b, uint32_t mask, gpio_pull_t pull)
{
    volatile uint32_t *r = ((bcm_priv_t *)b->priv)->regs;
    for (int reg = 0; reg < 2; reg++) {
        uint32_t clr = 0, val = 0;
        for (int i = 0; i < 16; i++) {
            int pin = reg * 16 + i;
            if (pin < 28 && (mask & (1u << pin))) {
                clr |= 3u << (i * 2);
                val |= (uint32_t)pull << (i * 2);
            }
        }
        if (clr)
            r[GPPUPPDN0 + reg] = (r[GPPUPPDN0 + reg] & ~clr) | val;
    }
}

static void bcm_close(gpio_backend_t *b)
{
    bcm_priv_t *p = b->priv;
    munmap((void *)p->regs, MAP_LEN);
    free(p);
    free(b);
}

gpio_backend_t *gpio_bcm2711_open(void)
{
    int fd = open("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "bcm2711: cannot open /dev/gpiomem: %s\n", strerror(errno));
        return NULL;
    }
    void *m = mmap(NULL, MAP_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        fprintf(stderr, "bcm2711: mmap failed: %s\n", strerror(errno));
        return NULL;
    }
    gpio_backend_t *b = calloc(1, sizeof *b);
    bcm_priv_t *p = calloc(1, sizeof *p);
    if (!b || !p) {
        free(b);
        free(p);
        munmap(m, MAP_LEN);
        return NULL;
    }
    p->regs = m;
    b->name = "bcm2711";
    b->kind = GPIO_BACKEND_BCM2711;
    b->lev = p->regs + GPLEV0;
    b->set = p->regs + GPSET0;
    b->clr = p->regs + GPCLR0;
    b->read = bcm_read;
    b->write = bcm_write;
    b->claim = bcm_claim;
    b->make_output = bcm_make_output;
    b->make_input = bcm_make_input;
    b->output_mask = bcm_output_mask;
    b->set_pull = bcm_set_pull;
    b->close = bcm_close;
    b->priv = p;
    return b;
}
