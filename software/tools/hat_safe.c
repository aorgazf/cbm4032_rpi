/*
 * hat_safe - put the HAT in the safe state of the driver brief, section 7,
 * or (--check) report whether it is in it. Used as systemd ExecStopPost.
 */
#define _GNU_SOURCE
#include "hat_board.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int check = 0;
    gpio_backend_kind_t kind = GPIO_BACKEND_AUTO;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--backend") && i + 1 < argc) kind = gpio_kind_from_name(argv[++i]);
        else {
            puts("usage: hat_safe [--check] [--backend auto|rp1|sim]");
            return 2;
        }
    }
    if ((int)kind < 0) return 2;
    gpio_backend_t *g = gpio_open(kind);
    if (!g) return 1;

    char why[256];
    int rc = 0;
    if (!check) {
        /* Claim first (RP1: funcsel SYS_RIO). Claiming makes inputs, which the
         * board pulls already hold safe; then set the documented state. */
        gpio_set(g, BIT(PIN_DATA_OE_N));
        g->claim(g, MASK_HAT);
        hat_safe_state(g);
    }
    if (hat_check_safe(g, why, sizeof why)) {
        printf("NOT SAFE: %s\n", why);
        rc = 1;
    } else {
        printf("safe: all bus lines released, U3 disabled, TE requests low\n");
    }
    g->close(g);
    return rc;
}
