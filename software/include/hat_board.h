/*
 * Board-level operations for the REV03F HAT: safe state, configuration and
 * the role changes of the driver brief, section 5 ("Required sequences").
 *
 * All functions only touch GPIO registers, so hat_safe_state() can be called
 * from a signal handler.
 */
#ifndef HAT_BOARD_H
#define HAT_BOARD_H

#include <signal.h>
#include <stdint.h>
#include "gpio_backend.h"
#include "hat_pins.h"

/* Logical line states decoded from a raw level word: 1 = asserted (low) */
typedef struct {
    uint8_t atn, ren, ifc, dav, eoi, ndac, nrfd, srq;
    uint8_t data;          /* bus byte, valid only while U3 receives (GPIO8 low, DATA_TE low) */
    uint8_t ctrl_te_req, data_te_req, atna_low, oe_n;
} hat_lines_t;

void hat_decode(uint32_t raw, hat_lines_t *l);

/* Format the asserted lines of a raw word as "ATN DAV ... $xx" (buf >= 64) */
const char *hat_format_lines(uint32_t raw, char *buf, int show_data);

/*
 * Safe state of section 7: talk lines released, both TE requests low, U3
 * disabled (GPIO8 high), every other line an input with the config.txt pulls.
 * The PET then sees every line released (NDAC only held by U23 under ATN).
 */
void hat_safe_state(gpio_backend_t *g);

/* Report what is not in the safe state; returns 0 when safe. buf may be NULL. */
int hat_check_safe(gpio_backend_t *g, char *buf, int len);

/*
 * Driver idle state: claim all HAT pins, idle listener (NDAC/NRFD released
 * inputs, TE requests driven low, ATNA driven high) and, once every pin is
 * configured, U3 enabled in receive direction (GPIO8 low).
 */
int hat_configure(gpio_backend_t *g);

/* ATN fell: inputs on talk lines, TE requests low, NDAC/NRFD driven low, ATNA low */
void hat_atn_takeover(gpio_backend_t *g);

/* Stop being a listener: NDAC/NRFD released and inputs, then ATNA high */
void hat_listener_release(gpio_backend_t *g);

/* ATN rose and we stay listener: NDAC already held by GPIO18, so ATNA high */
static inline void hat_atna_high(gpio_backend_t *g) { gpio_set(g, BIT(PIN_ATNA)); }

/* Section 5, ATN rises, addressed as talker (call with NDAC held by U23 or us) */
void hat_become_talker(gpio_backend_t *g);

/* Ending a talk: released levels, TE requests low, then inputs */
void hat_end_talk(gpio_backend_t *g);

/*
 * Signals: SIGINT/SIGTERM/SIGHUP set *stop (no SA_RESTART, so blocking reads
 * return EINTR). Fatal signals put the board in the safe state and re-raise.
 */
void hat_signals_install(gpio_backend_t *g, volatile sig_atomic_t *stop);

#endif
