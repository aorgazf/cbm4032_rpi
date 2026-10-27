#include "gpio_sim.h"
#include "hat_pins.h"

#include <stdlib.h>
#include <string.h>

/* Board pull resistors on the Pi side (10 kOhm; they beat the ~50 kOhm internal pulls) */
#define EXT_PULLUP   (BIT(PIN_DATA_OE_N) | BIT(PIN_ATNA) | BIT(PIN_NDAC) | BIT(PIN_NRFD) | \
                      BIT(PIN_SRQ) | BIT(PIN_SPARE))
#define EXT_PULLDOWN (BIT(PIN_DATA_TE_REQ) | BIT(PIN_CTRL_TE_REQ))

/* Pins the gpio= lines of boot/config.txt.snippet configure. As on RP1, every
 * other pin has no function at boot and reads 0 until a program claims it. */
#define BOOT_CONFIGURED (BIT(PIN_DATA_OE_N) | BIT(PIN_DATA_TE_REQ) | BIT(PIN_CTRL_TE_REQ) |                          BIT(PIN_ATNA) | BIT(PIN_NRFD) | BIT(PIN_NDAC) | BIT(PIN_SRQ) | MASK_DIO)

sim_hat_t *sim_hat(gpio_backend_t *b) { return b->priv; }

void sim_reset_pet(sim_hat_t *s)
{
    s->pet_atn = s->pet_ren = s->pet_ifc = s->pet_dav = s->pet_eoi = 1;
    s->pet_ndac = s->pet_nrfd = 1;
    s->pet_dio = 0xff;
    s->shorted = 0;
}

/* Level of a pin that nobody on the board drives */
static int idle_level(sim_hat_t *s, int pin)
{
    if (EXT_PULLUP & BIT(pin)) return 1;
    if (EXT_PULLDOWN & BIT(pin)) return 0;
    if (s->pull[pin] == GPIO_PULL_DOWN) return 0;
    return 1;  /* internal pull-up, or floating (reads as 1 here) */
}

/* Level of a pin the Pi might drive, ignoring the board drivers */
static int pi_level(sim_hat_t *s, int pin)
{
    if (s->oe & BIT(pin)) return (s->out >> pin) & 1;
    return idle_level(s, pin);
}

static int sh(sim_hat_t *s, uint32_t line) { return (s->shorted & line) ? 0 : 1; }

uint32_t sim_levels(sim_hat_t *s, sim_bus_t *busp)
{
    sim_bus_t bus;
    uint32_t hw = 0, hwval = 0;

    bus.atn = s->pet_atn & sh(s, SIM_LINE_ATN);
    bus.ren = s->pet_ren & sh(s, SIM_LINE_REN);
    bus.ifc = s->pet_ifc & sh(s, SIM_LINE_IFC);

    int atn = bus.atn;                       /* GPIO27 through U2/U4 */
    int ctrl_te = pi_level(s, PIN_CTRL_TE_REQ) & atn;     /* U20 */
    int data_te = pi_level(s, PIN_DATA_TE_REQ) & atn;     /* U21 */
    int oe_n = pi_level(s, PIN_DATA_OE_N);
    int atn_hold = atn ^ pi_level(s, PIN_ATNA);           /* U22 -> U23 */

    bus.dav = s->pet_dav & sh(s, SIM_LINE_DAV) & (ctrl_te ? pi_level(s, PIN_DAV) : 1);
    bus.eoi = s->pet_eoi & sh(s, SIM_LINE_EOI) & (ctrl_te ? pi_level(s, PIN_EOI) : 1);
    bus.ndac = s->pet_ndac & sh(s, SIM_LINE_NDAC) & (ctrl_te ? 1 : pi_level(s, PIN_NDAC)) & !atn_hold;
    bus.nrfd = s->pet_nrfd & sh(s, SIM_LINE_NRFD) & (ctrl_te ? 1 : pi_level(s, PIN_NRFD));
    bus.srq = sh(s, SIM_LINE_SRQ) & pi_level(s, PIN_SRQ);
    bus.dio = 0;
    for (int i = 0; i < 8; i++) {
        int v = ((s->pet_dio >> i) & 1) & sh(s, SIM_LINE_DIO1 << i);
        if (data_te && !oe_n) v &= pi_level(s, hat_dio_pin[i]);
        bus.dio |= (uint8_t)(v << i);
    }

    /* Board drivers on the Pi side */
#define DRIVE(pin, v) do { hw |= BIT(pin); if (v) hwval |= BIT(pin); } while (0)
    DRIVE(PIN_ATN, bus.atn);
    DRIVE(PIN_REN, bus.ren);
    DRIVE(PIN_IFC, bus.ifc);
    if (!ctrl_te) { DRIVE(PIN_DAV, bus.dav); DRIVE(PIN_EOI, bus.eoi); }
    else { DRIVE(PIN_NDAC, bus.ndac); DRIVE(PIN_NRFD, bus.nrfd); }
    if (!data_te && !oe_n)
        for (int i = 0; i < 8; i++) DRIVE(hat_dio_pin[i], (bus.dio >> i) & 1);
#undef DRIVE

    uint32_t lev = 0;
    for (int pin = 0; pin < 28; pin++) {
        int v;
        if (s->oe & BIT(pin)) v = (s->out >> pin) & 1;
        else if (hw & BIT(pin)) v = (hwval >> pin) & 1;
        else v = idle_level(s, pin);
        lev |= (uint32_t)v << pin;
    }
    s->overlap |= s->oe & hw;
    s->contention_now = s->oe & hw & (s->out ^ hwval);
    s->contention |= s->contention_now;
    if (busp) *busp = bus;
    return lev;
}

static uint32_t sim_read(gpio_backend_t *b)
{
    sim_hat_t *s = b->priv;
    s->reads++;
    if (s->on_read) s->on_read(s, s->ctx);
    return sim_levels(s, NULL) & s->claimed;
}

/* Contention is sampled after every change the Pi makes */
static void sim_write(gpio_backend_t *b, uint32_t set, uint32_t clr)
{
    sim_hat_t *s = b->priv;
    s->out = (s->out | set) & ~clr;
    sim_levels(s, NULL);
}

static int sim_claim(gpio_backend_t *b, uint32_t mask)
{
    sim_hat_t *s = b->priv;
    s->oe &= ~mask;
    s->claimed |= mask;
    sim_levels(s, NULL);
    return 0;
}

static void sim_claim_inputs(gpio_backend_t *b, uint32_t mask)
{
    sim_claim(b, mask & ~((sim_hat_t *)b->priv)->claimed);
}

static void sim_make_output(gpio_backend_t *b, uint32_t mask)
{
    sim_hat_t *s = b->priv;
    s->oe |= mask & 0x0fffffff;
    sim_levels(s, NULL);
}

static void sim_make_input(gpio_backend_t *b, uint32_t mask)
{
    sim_hat_t *s = b->priv;
    s->oe &= ~mask;
    sim_levels(s, NULL);
}

static uint32_t sim_output_mask(gpio_backend_t *b) { return ((sim_hat_t *)b->priv)->oe; }

static void sim_set_pull(gpio_backend_t *b, uint32_t mask, gpio_pull_t pull)
{
    sim_hat_t *s = b->priv;
    for (int pin = 0; pin < 28; pin++)
        if (mask & BIT(pin)) s->pull[pin] = (uint8_t)pull;
}

static void sim_close(gpio_backend_t *b)
{
    free(b->priv);
    free(b);
}

gpio_backend_t *gpio_sim_open(void)
{
    gpio_backend_t *b = calloc(1, sizeof *b);
    sim_hat_t *s = calloc(1, sizeof *s);
    if (!b || !s) {
        free(b);
        free(s);
        return NULL;
    }
    sim_reset_pet(s);
    s->claimed = BOOT_CONFIGURED;
    b->name = "sim";
    b->kind = GPIO_BACKEND_SIM;
    b->read = sim_read;
    b->write = sim_write;
    b->claim = sim_claim;
    b->claim_inputs = sim_claim_inputs;
    b->make_output = sim_make_output;
    b->make_input = sim_make_input;
    b->output_mask = sim_output_mask;
    b->set_pull = sim_set_pull;
    b->close = sim_close;
    b->priv = s;
    return b;
}
