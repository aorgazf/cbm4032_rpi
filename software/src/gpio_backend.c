#define _GNU_SOURCE
#include "gpio_backend.h"
#include "hat_pins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t hat_dio_spread[256];

void hat_pins_init(void)
{
    for (int v = 0; v < 256; v++) {
        uint32_t m = 0;
        for (int i = 0; i < 8; i++)
            if (v & (1 << i))
                m |= BIT(hat_dio_pin[i]);
        hat_dio_spread[v] = m;
    }
}

gpio_backend_kind_t gpio_kind_from_name(const char *name)
{
    if (!name || !strcmp(name, "auto")) return GPIO_BACKEND_AUTO;
    if (!strcmp(name, "rp1") || !strcmp(name, "pi5")) return GPIO_BACKEND_RP1;
    if (!strcmp(name, "sim")) return GPIO_BACKEND_SIM;
    return (gpio_backend_kind_t)-1;
}

const char *gpio_kind_name(gpio_backend_kind_t kind)
{
    switch (kind) {
    case GPIO_BACKEND_RP1: return "rp1";
    case GPIO_BACKEND_SIM: return "sim";
    default: return "auto";
    }
}

/* /proc/device-tree/compatible is a list of NUL-separated strings */
static gpio_backend_kind_t detect(void)
{
    char buf[512];
    FILE *f = fopen("/proc/device-tree/compatible", "rb");
    if (!f)
        return GPIO_BACKEND_AUTO;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    for (size_t i = 0; i < n; i += strlen(buf + i) + 1) {
        if (!strcmp(buf + i, "brcm,bcm2712")) return GPIO_BACKEND_RP1;
    }
    return GPIO_BACKEND_AUTO;
}

gpio_backend_t *gpio_open(gpio_backend_kind_t kind)
{
    hat_pins_init();
    if (kind == GPIO_BACKEND_AUTO) {
        const char *env = getenv("HAT_BACKEND");
        if (env) {
            kind = gpio_kind_from_name(env);
            if ((int)kind == -1) {
                fprintf(stderr, "HAT_BACKEND=%s: unknown backend\n", env);
                return NULL;
            }
        }
    }
    if (kind == GPIO_BACKEND_AUTO)
        kind = detect();
    switch (kind) {
    case GPIO_BACKEND_RP1: return gpio_rp1_open();
    case GPIO_BACKEND_SIM: return gpio_sim_open();
    default:
        fprintf(stderr, "Not a Raspberry Pi 5 (no brcm,bcm2712 in device tree); "
                        "use --backend sim to test without hardware\n");
        return NULL;
    }
}
