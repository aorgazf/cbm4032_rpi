/*
 * hat_selftest - REV03F board bring-up test.
 *
 *   --auto       (default) automatic checks, HAT unplugged from the PET.
 *                Uses the board's own paths as loopbacks: the Pi's internal
 *                pull-down against a translator output shows which way each
 *                translator points, and the U22/U23 NDAC hold is read back
 *                through U2/U10 in talker direction.
 *   --probe      HAT unplugged: touch each J2 contact to ground with a jumper
 *                and the tool ticks off every receive path it sees.
 *   --manual     HAT unplugged: steps through every output; check each J2
 *                contact with a meter or logic analyser.
 *   --read-only  PET may be on and connected: reads and decodes the lines,
 *                drives nothing: it only claims unclaimed pins as inputs
 *                (--rx enables U3 in receive direction only).
 *
 * The active modes refuse to run when REN reads asserted: the PET ties REN to
 * ground, so that means the HAT is plugged into a PET (--force overrides).
 */
#define _GNU_SOURCE
#include "hat_board.h"

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static gpio_backend_t *g;
static int passes, fails;

static const int dio_j1[8] = { 29, 32, 33, 35, 38, 40, 37, 11 };

static void settle(void)
{
    if (g->kind != GPIO_BACKEND_SIM) usleep(1000);
}

static uint32_t rd(void)
{
    settle();
    return gpio_read(g);
}

static void check(const char *what, int ok, const char *hint)
{
    if (ok) { passes++; printf("  PASS  %s\n", what); }
    else { fails++; printf("  FAIL  %s\n        -> %s\n", what, hint); }
}

static int lvl(uint32_t raw, int pin) { return (raw >> pin) & 1; }

/* Check every DIO GPIO reads `want`; list the ones that do not */
static void check_dio(const char *what, uint32_t raw, int want, const char *hint)
{
    char bad[160] = "";
    int n = 0;
    for (int i = 0; i < 8; i++)
        if (lvl(raw, hat_dio_pin[i]) != want)
            n += snprintf(bad + n, sizeof bad - n, "%sDIO%d (GPIO%d, J1-%d, R%d)", n ? ", " : "",
                          i + 1, hat_dio_pin[i], dio_j1[i], 34 + i);
    char msg[320];
    snprintf(msg, sizeof msg, "%s; wrong: %s", hint, bad);
    check(what, bad[0] == 0, msg);
}

static int preflight(int force)
{
    uint32_t raw = gpio_read(g);
    if (!lvl(raw, PIN_REN) || !lvl(raw, PIN_IFC) || !lvl(raw, PIN_ATN)) {
        printf("REN, IFC or ATN reads asserted (raw 0x%07x): the HAT looks plugged into a PET.\n"
               "Active tests drive bus lines. Unplug the HAT, or use --read-only.%s\n",
               raw, force ? " (--force given: continuing)" : "");
        return force;
    }
    return 1;
}

/* ---------------- automatic ---------------- */

static void run_auto(void)
{
    uint32_t r;

    puts("T0  Idle levels from the board pulls (before the test configures anything)");
    r = gpio_read(g);
    check("GPIO8 DATA_OE_N high (R2 / config.txt): U3 disabled", lvl(r, PIN_DATA_OE_N), "R2, J1-24");
    check("GPIO9 DATA_TE_REQ low (R29)", !lvl(r, PIN_DATA_TE_REQ), "R29, J1-21; or a program left it high: run hat_safe");
    check("GPIO11 CTRL_TE_REQ low (R28)", !lvl(r, PIN_CTRL_TE_REQ), "R28, J1-23; or run hat_safe");
    check("GPIO16 ATNA high (R30)", lvl(r, PIN_ATNA), "R30, J1-36");
    check("GPIO18 NDAC, GPIO15 NRFD high (R6, R7)", lvl(r, PIN_NDAC) && lvl(r, PIN_NRFD), "R6/R7, U10/U11");
    check("GPIO23 SRQ high (R5)", lvl(r, PIN_SRQ), "R5, U6");
    check("GPIO10 spare high (R4)", lvl(r, PIN_SPARE), "R4, J1-19");
    check("GPIO27 ATN, GPIO24 REN, GPIO25 IFC released", lvl(r, PIN_ATN) && lvl(r, PIN_REN) && lvl(r, PIN_IFC),
          "U2 receive path, U4/U7/U8, R12/R15/R16, 5V_HAT and F1");
    check("GPIO4 DAV, GPIO22 EOI released (U9/U5 receive)", lvl(r, PIN_DAV) && lvl(r, PIN_EOI),
          "U9/U5 direction (CTRL_TE low via R3), R32/R33, U2");

    hat_configure(g);

    puts("T1  Idle listener with U3 enabled in receive direction");
    r = rd();
    check_dio("DIO1-8 read released (U1 receive -> U3 B->A)", r, 1, "U1/U3, R20-R27 pull-ups, GPIO8 -> U3 pin 22");

    puts("T2  Control talk enable (U20) and translator direction (U5, U9, U10, U11)");
    g->set_pull(g, MASK_TALK_CTRL, GPIO_PULL_DOWN);
    r = rd();
    check("GPIO11 low: GPIO4/22 driven high by U9/U5 against the Pi pull-down",
          lvl(r, PIN_DAV) && lvl(r, PIN_EOI), "U9/U5 not driving the A side: DIR stuck high? CTRL_TE, R3, U20");
    gpio_set(g, BIT(PIN_CTRL_TE_REQ));
    r = rd();
    check("GPIO11 high: GPIO4/22 follow the Pi pull-down (U9/U5 turned A->B)",
          !lvl(r, PIN_DAV) && !lvl(r, PIN_EOI), "CTRL_TE not granted: U20 (GPIO11 AND GPIO27), R28, U9/U5 DIR");
    check("GPIO11 high: GPIO18/15 read released bus NDAC/NRFD (U10/U11 turned B->A)",
          lvl(r, PIN_NDAC) && lvl(r, PIN_NRFD), "U18 inverter, U10/U11 DIR, U2 NDAC/NRFD receive");

    puts("T3  NDAC hold (U22 XOR, U23 open drain) read back through U2 and U10");
    gpio_clr(g, BIT(PIN_ATNA));
    r = rd();
    check("ATN released, GPIO16 low: bus NDAC held low, seen on GPIO18", !lvl(r, PIN_NDAC),
          "U22 (GPIO27 XOR GPIO16), U23 output to bus NDAC (J2-8), U2 pin 4/17, U10");
    check("NRFD unaffected", lvl(r, PIN_NRFD), "short between NDAC and NRFD paths?");
    gpio_set(g, BIT(PIN_ATNA));
    r = rd();
    check("GPIO16 high again: bus NDAC released", lvl(r, PIN_NDAC), "U22/U23 stuck");
    gpio_clr(g, BIT(PIN_CTRL_TE_REQ));
    g->set_pull(g, MASK_TALK_CTRL, GPIO_PULL_UP);
    r = rd();
    check("GPIO11 low again: GPIO4/22 driven high by U9/U5", lvl(r, PIN_DAV) && lvl(r, PIN_EOI), "U20/U9/U5");

    puts("T4  Data talk enable (U21) and U3 direction");
    g->set_pull(g, MASK_DIO, GPIO_PULL_DOWN);
    r = rd();
    check_dio("GPIO9 low: DIO driven high by U3 against the Pi pull-down", r, 1, "U3 B->A outputs, R34-R41");
    gpio_set(g, BIT(PIN_DATA_TE_REQ));
    r = rd();
    check_dio("GPIO9 high: DIO follow the Pi pull-down (U3 turned A->B)", r, 0,
              "DATA_TE not granted: U21 (GPIO9 AND GPIO27), R1, U3 pin 2; or pin shorted high");
    g->set_pull(g, MASK_DIO, GPIO_PULL_UP);
    r = rd();
    check_dio("GPIO9 high, Pi pull-up: DIO read high (no pin shorted low)", r, 1, "short to GND near U3 A side or R34-R41");
    gpio_clr(g, BIT(PIN_DATA_TE_REQ));
    gpio_set(g, BIT(PIN_DATA_OE_N));
    g->set_pull(g, MASK_DIO, GPIO_PULL_DOWN);
    r = rd();
    check_dio("GPIO8 high: U3 disabled, DIO follow the Pi pull-down", r, 0, "U3 OE# (pin 22) stuck low? R2");
    g->set_pull(g, MASK_DIO, GPIO_PULL_UP);
    gpio_clr(g, BIT(PIN_DATA_OE_N));

    puts("T5  SRQ output pad");
    gpio_clr(g, BIT(PIN_SRQ));
    g->make_output(g, BIT(PIN_SRQ));
    r = rd();
    check("GPIO23 driven low reads low", !lvl(r, PIN_SRQ), "GPIO23 shorted to 3V3?");
    gpio_set(g, BIT(PIN_SRQ));
    g->make_input(g, BIT(PIN_SRQ));

    hat_safe_state(g);
    puts("\nNot covered here: bus-side drive of each output (use --manual with a meter) and the"
         "\nreceive path of each input (use --probe with a jumper to ground).");
}

/* ---------------- probe ---------------- */

static int enter_pressed(int timeout_ms)
{
    struct pollfd p = { .fd = 0, .events = POLLIN };
    if (poll(&p, 1, timeout_ms) > 0) {
        char buf[64];
        if (!fgets(buf, sizeof buf, stdin)) return 1;
        return 1;
    }
    return 0;
}

typedef struct { int pin; const char *name, *j2; } probe_line_t;

static int probe_phase(const probe_line_t *lines, int n)
{
    int seen[16] = { 0 }, nseen = 0;
    while (!stop && nseen < n) {
        uint32_t r = gpio_read(g);
        for (int i = 0; i < n; i++) {
            if (!seen[i] && !lvl(r, lines[i].pin)) {
                seen[i] = 1;
                nseen++;
                printf("  seen  %-5s J2-%-2s GPIO%d  (%d/%d)\n", lines[i].name, lines[i].j2, lines[i].pin, nseen, n);
            }
        }
        if (enter_pressed(20)) break;
    }
    for (int i = 0; i < n; i++) {
        char what[64];
        snprintf(what, sizeof what, "%s receive path (J2-%s -> GPIO%d)", lines[i].name, lines[i].j2, lines[i].pin);
        check(what, seen[i], "not seen: check J2 contact, U1/U2 receiver, translator and series resistor");
    }
    return nseen;
}

static void run_probe(void)
{
    hat_configure(g);
    probe_line_t rx[13] = {
        { PIN_ATN, "ATN", J2_ATN }, { PIN_REN, "REN", J2_REN }, { PIN_IFC, "IFC", J2_IFC },
        { PIN_DAV, "DAV", J2_DAV }, { PIN_EOI, "EOI", J2_EOI },
    };
    static const char *dn[8] = { "DIO1", "DIO2", "DIO3", "DIO4", "DIO5", "DIO6", "DIO7", "DIO8" };
    for (int i = 0; i < 8; i++) rx[5 + i] = (probe_line_t){ hat_dio_pin[i], dn[i], hat_dio_j2[i] };

    puts("Phase 1: listener direction. Touch each of J2 contacts 11, E, 9, 6, 5, 1-4, A-D to a\n"
         "ground contact (12 or F-N) with a jumper. Enter ends the phase early.");
    probe_phase(rx, 13);
    if (stop) goto out;

    puts("\nPhase 2: ATN withdraws the talk enables (U20, U21). Both enables are now requested, so\n"
         "DAV, EOI and DIO are driven low on J2. Touch ONLY contact 11 (ATN) to ground.");
    g->set_pull(g, MASK_TALK_CTRL | MASK_DIO, GPIO_PULL_DOWN);
    gpio_set(g, BIT(PIN_CTRL_TE_REQ) | BIT(PIN_DATA_TE_REQ));
    {
        int ok_ctrl = 0, ok_data = 0;
        while (!stop && !(ok_ctrl && ok_data)) {
            uint32_t r = gpio_read(g);
            if (!lvl(r, PIN_ATN)) {
                ok_ctrl |= lvl(r, PIN_DAV) && lvl(r, PIN_EOI);
                ok_data |= (hat_dio_gather(r) == 0xFF);
            }
            if (enter_pressed(20)) break;
        }
        check("ATN low forces CTRL_TE low (GPIO4/22 driven by U9/U5 again)", ok_ctrl, "U20 gate, GPIO27 net");
        check("ATN low forces DATA_TE low (DIO driven by U3 again)", ok_data, "U21 gate, U3 DIR");
    }
    gpio_clr(g, BIT(PIN_CTRL_TE_REQ) | BIT(PIN_DATA_TE_REQ));
    g->set_pull(g, MASK_TALK_CTRL | MASK_DIO, GPIO_PULL_UP);
    if (stop) goto out;

    puts("\nPhase 3: talker direction for NRFD/NDAC. DAV and EOI are now actively driven high:\n"
         "touch ONLY contacts 7 (NRFD) and 8 (NDAC) to ground.");
    gpio_set(g, BIT(PIN_CTRL_TE_REQ));
    probe_line_t hs[2] = { { PIN_NRFD, "NRFD", J2_NRFD }, { PIN_NDAC, "NDAC", J2_NDAC } };
    probe_phase(hs, 2);
out:
    hat_safe_state(g);
}

/* ---------------- manual ---------------- */

/* Returns 1 ok, 0 fail, -1 quit */
static int ask(const char *what)
{
    char buf[32];
    printf("\n  %s\n  Enter = as expected, f = fail, q = quit: ", what);
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin) || stop) return -1;
    if (buf[0] == 'q') return -1;
    int ok = buf[0] != 'f';
    check(what, ok, "recorded as failed by the operator");
    return ok;
}

static void run_manual(void)
{
    char msg[160];
    puts("Released lines read about 3 V or more at J2 (SN7516x bus terminations, no load);\n"
         "asserted lines read below 0.5 V. Ground: J2 contact 12 or F-N.");
    hat_configure(g);
    if (ask("IDLE: J2 contacts 1-11 and A-E all HIGH") < 0) goto out;

    gpio_clr(g, BIT(PIN_SRQ));
    g->make_output(g, BIT(PIN_SRQ));
    if (ask("SRQ asserted: J2-10 LOW, others HIGH") < 0) goto out;
    gpio_set(g, BIT(PIN_SRQ));
    g->make_input(g, BIT(PIN_SRQ));

    gpio_clr(g, BIT(PIN_NRFD));
    g->make_output(g, BIT(PIN_NRFD));
    if (ask("NRFD asserted (GPIO15 low): J2-7 LOW, others HIGH") < 0) goto out;
    gpio_set(g, BIT(PIN_NRFD));
    g->make_input(g, BIT(PIN_NRFD));

    gpio_clr(g, BIT(PIN_NDAC));
    g->make_output(g, BIT(PIN_NDAC));
    if (ask("NDAC asserted (GPIO18 low): J2-8 LOW, others HIGH") < 0) goto out;
    gpio_set(g, BIT(PIN_NDAC));
    g->make_input(g, BIT(PIN_NDAC));

    gpio_clr(g, BIT(PIN_ATNA));
    if (ask("NDAC hold (GPIO16 low, ATN released): J2-8 LOW, others HIGH") < 0) goto out;
    gpio_set(g, BIT(PIN_ATNA));

    hat_become_talker(g);
    if (ask("TALKER idle: J2 contacts 1-11 and A-E all HIGH") < 0) goto out;
    gpio_clr(g, BIT(PIN_DAV));
    if (ask("DAV asserted: J2-6 LOW, others HIGH") < 0) goto out;
    gpio_set(g, BIT(PIN_DAV));
    gpio_clr(g, BIT(PIN_EOI));
    if (ask("EOI asserted: J2-5 LOW, others HIGH") < 0) goto out;
    gpio_set(g, BIT(PIN_EOI));
    for (int i = 0; i < 8; i++) {
        uint8_t b = (uint8_t)(1 << i);
        gpio_write(g, hat_byte_set_mask(b), hat_byte_clr_mask(b));
        snprintf(msg, sizeof msg, "DIO%d asserted (byte $%02X): J2-%s LOW, other DIO contacts HIGH",
                 i + 1, b, hat_dio_j2[i]);
        if (ask(msg) < 0) goto out;
    }
    gpio_write(g, hat_byte_set_mask(0xFF), hat_byte_clr_mask(0xFF));
    if (ask("All DIO asserted (byte $FF): J2-1..4 and A..D LOW") < 0) goto out;
    gpio_write(g, hat_byte_set_mask(0), hat_byte_clr_mask(0));
    hat_end_talk(g);
out:
    hat_safe_state(g);
}

/* ---------------- read-only ---------------- */

static void print_state(uint32_t r)
{
    hat_lines_t l;
    hat_decode(r, &l);
    int data_valid = !l.oe_n && !l.data_te_req;
    printf("raw 0x%07x\n", r);
    printf("  ATN  %-8s GPIO27   REN %-8s GPIO24 (always asserted on a PET)   IFC %-8s GPIO25\n",
           l.atn ? "asserted" : "released", l.ren ? "asserted" : "released", l.ifc ? "asserted" : "released");
    printf("  DAV  %-8s GPIO4    EOI %-8s GPIO22   (bus values while GPIO11 is low)\n",
           l.dav ? "asserted" : "released", l.eoi ? "asserted" : "released");
    printf("  NDAC %-8s GPIO18   NRFD %-8s GPIO15  (own outputs while GPIO11 is low)\n",
           l.ndac ? "low" : "high", l.nrfd ? "low" : "high");
    printf("  SRQ  %-8s GPIO23   ATNA %s   CTRL_TE_REQ %d   DATA_TE_REQ %d   DATA_OE_N %d\n",
           l.srq ? "asserted" : "released", l.atna_low ? "low (software holds NDAC)" : "high",
           l.ctrl_te_req, l.data_te_req, l.oe_n);
    if (data_valid) printf("  DATA $%02X\n", l.data);
    else printf("  DATA not valid: U3 %s (use --rx to enable it in receive direction)\n",
                l.oe_n ? "disabled" : "pointing Pi->bus");
}

static void run_read_only(int watch, int rx)
{
    if (rx) {
        uint32_t r = gpio_read(g);
        if (lvl(r, PIN_DATA_TE_REQ) || (g->output_mask(g) & MASK_DIO)) {
            puts("--rx refused: GPIO9 is high or a DIO pin is an output (another program running?)");
            return;
        }
        gpio_clr(g, BIT(PIN_DATA_OE_N));
        g->claim(g, BIT(PIN_DATA_OE_N));
        g->make_output(g, BIT(PIN_DATA_OE_N));
    }
    uint32_t prev = ~0u;
    char buf[64];
    print_state(gpio_read(g));
    while (watch && !stop) {
        uint32_t r = gpio_read(g) & (MASK_HAT & ~BIT(PIN_DATA_OE_N));
        if (r != prev) {
            printf("%s\n", hat_format_lines(r, buf, rx));
            fflush(stdout);
            prev = r;
        }
        usleep(1000);
    }
    if (rx) gpio_set(g, BIT(PIN_DATA_OE_N));
}

int main(int argc, char **argv)
{
    enum { AUTO, PROBE, MANUAL, READ_ONLY } mode = AUTO;
    int force = 0, watch = 0, rx = 0;
    gpio_backend_kind_t kind = GPIO_BACKEND_AUTO;
    static const struct option opts[] = {
        { "auto", no_argument, 0, 'a' }, { "probe", no_argument, 0, 'p' },
        { "manual", no_argument, 0, 'm' }, { "read-only", no_argument, 0, 'r' },
        { "watch", no_argument, 0, 'w' }, { "rx", no_argument, 0, 'x' },
        { "force", no_argument, 0, 'f' }, { "backend", required_argument, 0, 'b' },
        { "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "apmrwxfb:h", opts, NULL)) != -1) {
        switch (o) {
        case 'a': mode = AUTO; break;
        case 'p': mode = PROBE; break;
        case 'm': mode = MANUAL; break;
        case 'r': mode = READ_ONLY; break;
        case 'w': watch = 1; break;
        case 'x': rx = 1; break;
        case 'f': force = 1; break;
        case 'b': kind = gpio_kind_from_name(optarg); break;
        default:
            puts("usage: hat_selftest [--auto | --probe | --manual | --read-only [--watch] [--rx]]\n"
                 "                    [--force] [--backend auto|rp1|sim]");
            return o == 'h' ? 0 : 2;
        }
    }
    if ((int)kind < 0) return 2;
    g = gpio_open(kind);
    if (!g) return 1;
    hat_signals_install(g, &stop);
    printf("hat_selftest: backend %s\n", g->name);
    /* Pins config.txt leaves alone read 0 on RP1 until claimed (ATN, REN, IFC,
     * DAV, EOI, GPIO10). As inputs only, so --read-only still drives nothing. */
    g->claim_inputs(g, MASK_HAT | BIT(PIN_SPARE));

    if (mode == READ_ONLY) {
        run_read_only(watch, rx);
        g->close(g);
        return 0;
    }
    if (!preflight(force)) { g->close(g); return 1; }
    if (mode == AUTO) run_auto();
    else if (mode == PROBE) run_probe();
    else run_manual();

    printf("\n%d passed, %d failed%s\n", passes, fails, stop ? " (interrupted)" : "");
    g->close(g);
    return fails || stop ? 1 : 0;
}
