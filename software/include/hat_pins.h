/*
 * CBM 4032 IEEE-488 HAT (PCB REV03F) - GPIO map.
 *
 * Source: pad nets of Hardware - REV03F/CBM4032_Pi4_Pi5_IEEE488_REV03.kicad_pcb,
 * cross-checked against the driver brief, section 4 (see docs/HARDWARE_CHECK.md).
 *
 * Every line is non-inverting from GPIO to bus: GPIO low = bus low = asserted.
 * A bus data byte is therefore the bitwise inverse of the gathered DIO bits.
 */
#ifndef HAT_PINS_H
#define HAT_PINS_H

#include <stdint.h>

/* Data lines DIO1..DIO8 (U3 A1..A8 through R34..R41, 330 ohm) */
#define PIN_DIO1 5
#define PIN_DIO2 12
#define PIN_DIO3 13
#define PIN_DIO4 19
#define PIN_DIO5 20
#define PIN_DIO6 21
#define PIN_DIO7 26
#define PIN_DIO8 17

/* Direction control */
#define PIN_DATA_OE_N    8   /* U3 OE#, low enables the data translator; R2 pull-up */
#define PIN_DATA_TE_REQ  9   /* U21: DATA_TE = GPIO9 AND ATN; R29 pull-down */
#define PIN_CTRL_TE_REQ 11   /* U20: CTRL_TE = GPIO11 AND ATN; R28 pull-down */
#define PIN_ATNA        16   /* U22: ATN_HOLD = ATN XOR ATNA -> U23 on bus NDAC; R30 pull-up */

/* Management lines, fixed direction */
#define PIN_ATN 27           /* in, always (U4 receive) */
#define PIN_REN 24           /* in, always (U7); tied to GND on the PET */
#define PIN_IFC 25           /* in, always (U8) */
#define PIN_SRQ 23           /* out, always (U6 transmit); R5 pull-up */

/* Handshake lines, direction follows CTRL_TE */
#define PIN_DAV  4           /* out when CTRL_TE=H (U9, R32 330 ohm), else in */
#define PIN_EOI 22           /* out when CTRL_TE=H (U5, R33 330 ohm), else in */
#define PIN_NDAC 18          /* out when CTRL_TE=L (U10), else in; R6 pull-up */
#define PIN_NRFD 15          /* out when CTRL_TE=L (U11), else in; R7 pull-up */

#define PIN_SPARE 10         /* only R4 pull-up; free for a debug strobe */

#define BIT(n) (1u << (n))

#define MASK_DIO  (BIT(PIN_DIO1) | BIT(PIN_DIO2) | BIT(PIN_DIO3) | BIT(PIN_DIO4) | \
                   BIT(PIN_DIO5) | BIT(PIN_DIO6) | BIT(PIN_DIO7) | BIT(PIN_DIO8))
#define MASK_TALK_CTRL (BIT(PIN_DAV) | BIT(PIN_EOI))       /* Pi drives when CTRL_TE=H */
#define MASK_LISTEN_CTRL (BIT(PIN_NDAC) | BIT(PIN_NRFD))   /* Pi drives when CTRL_TE=L */
#define MASK_MGMT_IN (BIT(PIN_ATN) | BIT(PIN_REN) | BIT(PIN_IFC))
#define MASK_DIRCTL (BIT(PIN_DATA_OE_N) | BIT(PIN_DATA_TE_REQ) | BIT(PIN_CTRL_TE_REQ) | BIT(PIN_ATNA))

/* Every GPIO the driver touches (GPIO10 is added only by tools that use it) */
#define MASK_HAT (MASK_DIO | MASK_TALK_CTRL | MASK_LISTEN_CTRL | MASK_MGMT_IN | \
                  BIT(PIN_SRQ) | MASK_DIRCTL)

static const uint8_t hat_dio_pin[8] = {
    PIN_DIO1, PIN_DIO2, PIN_DIO3, PIN_DIO4, PIN_DIO5, PIN_DIO6, PIN_DIO7, PIN_DIO8
};

/* J2 (PET edge connector) contact for each signal, for messages */
#define J2_DIO1 "1"
#define J2_EOI  "5"
#define J2_DAV  "6"
#define J2_NRFD "7"
#define J2_NDAC "8"
#define J2_IFC  "9"
#define J2_SRQ  "10"
#define J2_ATN  "11"
#define J2_REN  "E"
static const char *const hat_dio_j2[8] = { "1", "2", "3", "4", "A", "B", "C", "D" };

/*
 * Gather DIO1..8 from a raw level word into bits 0..7 (still raw: 1 = high
 * = released). Shift distances follow the pin numbers above.
 */
static inline uint8_t hat_dio_gather(uint32_t raw)
{
    return (uint8_t)(((raw >> 5) & 0x01) |   /* GPIO5  -> bit 0 */
                     ((raw >> 11) & 0x02) |  /* GPIO12 -> bit 1 */
                     ((raw >> 11) & 0x04) |  /* GPIO13 -> bit 2 */
                     ((raw >> 16) & 0x08) |  /* GPIO19 -> bit 3 */
                     ((raw >> 16) & 0x10) |  /* GPIO20 -> bit 4 */
                     ((raw >> 16) & 0x20) |  /* GPIO21 -> bit 5 */
                     ((raw >> 20) & 0x40) |  /* GPIO26 -> bit 6 */
                     ((raw >> 10) & 0x80));  /* GPIO17 -> bit 7 */
}

/* Bus byte (IEEE-488 logical value, 1 = asserted = low) from a raw level word */
static inline uint8_t hat_bus_byte(uint32_t raw)
{
    return (uint8_t)~hat_dio_gather(raw);
}

/*
 * Pin masks to put a bus byte on DIO1..8: bits that are 1 in the byte are
 * asserted, so they go in the clear mask; the rest go in the set mask.
 */
extern uint32_t hat_dio_spread[256];   /* byte bit i -> GPIO bit hat_dio_pin[i] */
void hat_pins_init(void);              /* fills hat_dio_spread[] */

static inline uint32_t hat_byte_clr_mask(uint8_t bus_byte) { return hat_dio_spread[bus_byte]; }
static inline uint32_t hat_byte_set_mask(uint8_t bus_byte) { return hat_dio_spread[(uint8_t)~bus_byte]; }

#endif
