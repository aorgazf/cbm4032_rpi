#define _GNU_SOURCE
#include "hat_board.h"

#include <stdio.h>
#include <string.h>

#define TE_REQS (BIT(PIN_CTRL_TE_REQ) | BIT(PIN_DATA_TE_REQ))
#define TALK_LINES (MASK_TALK_CTRL | MASK_DIO)

void hat_decode(uint32_t raw, hat_lines_t *l)
{
    l->atn = !(raw & BIT(PIN_ATN));
    l->ren = !(raw & BIT(PIN_REN));
    l->ifc = !(raw & BIT(PIN_IFC));
    l->dav = !(raw & BIT(PIN_DAV));
    l->eoi = !(raw & BIT(PIN_EOI));
    l->ndac = !(raw & BIT(PIN_NDAC));
    l->nrfd = !(raw & BIT(PIN_NRFD));
    l->srq = !(raw & BIT(PIN_SRQ));
    l->data = hat_bus_byte(raw);
    l->ctrl_te_req = !!(raw & BIT(PIN_CTRL_TE_REQ));
    l->data_te_req = !!(raw & BIT(PIN_DATA_TE_REQ));
    l->atna_low = !(raw & BIT(PIN_ATNA));
    l->oe_n = !!(raw & BIT(PIN_DATA_OE_N));
}

const char *hat_format_lines(uint32_t raw, char *buf, int show_data)
{
    hat_lines_t l;
    hat_decode(raw, &l);
    snprintf(buf, 64, "%s %s %s %s %s %s %s%s",
             l.atn ? "ATN" : "...", l.dav ? "DAV" : "...", l.eoi ? "EOI" : "...",
             l.nrfd ? "NRFD" : "....", l.ndac ? "NDAC" : "....", l.ifc ? "IFC" : "...",
             l.srq ? "SRQ" : "...", "");
    if (show_data) {
        size_t n = strlen(buf);
        snprintf(buf + n, 64 - n, " $%02X", l.data);
    }
    return buf;
}

void hat_safe_state(gpio_backend_t *g)
{
    /* Release anything driven, then drop the TE requests, then go to inputs */
    gpio_set(g, TALK_LINES | MASK_LISTEN_CTRL | BIT(PIN_SRQ) | BIT(PIN_DATA_OE_N) | BIT(PIN_ATNA));
    gpio_clr(g, TE_REQS);
    g->make_input(g, TALK_LINES | MASK_LISTEN_CTRL | BIT(PIN_SRQ));
    g->make_output(g, BIT(PIN_DATA_OE_N));            /* config.txt: gpio=8=op,dh */
    g->make_input(g, TE_REQS | BIT(PIN_ATNA));         /* R28/R29 down, R30 up */
    g->set_pull(g, TE_REQS, GPIO_PULL_DOWN);           /* gpio=9,11=ip,pd */
    g->set_pull(g, BIT(PIN_ATNA) | MASK_LISTEN_CTRL | BIT(PIN_SRQ), GPIO_PULL_UP);
    g->set_pull(g, MASK_DIO | MASK_TALK_CTRL, GPIO_PULL_UP);
}

int hat_check_safe(gpio_backend_t *g, char *buf, int len)
{
    uint32_t out = g->output_mask(g) & MASK_HAT;
    uint32_t raw = gpio_read(g);
    int bad = 0, n = 0;
    if (buf && len) buf[0] = 0;
#define NOTE(...) do { bad++; if (buf && n < len) n += snprintf(buf + n, len - n, __VA_ARGS__); } while (0)
    if (out & ~BIT(PIN_DATA_OE_N))
        NOTE("Pi drives GPIOs other than GPIO8 (mask 0x%07x). ", out & ~BIT(PIN_DATA_OE_N));
    if (!(raw & BIT(PIN_DATA_OE_N))) NOTE("GPIO8 low: U3 enabled. ");
    if (raw & BIT(PIN_DATA_TE_REQ)) NOTE("GPIO9 high: data talk requested. ");
    if (raw & BIT(PIN_CTRL_TE_REQ)) NOTE("GPIO11 high: control talk requested. ");
    if (!(raw & BIT(PIN_ATNA))) NOTE("GPIO16 low: NDAC hold inverted. ");
#undef NOTE
    return bad;
}

int hat_configure(gpio_backend_t *g)
{
    /* Output latches first, so pins come up at their idle level */
    gpio_set(g, TALK_LINES | MASK_LISTEN_CTRL | BIT(PIN_SRQ) | BIT(PIN_DATA_OE_N) | BIT(PIN_ATNA));
    gpio_clr(g, TE_REQS);
    if (g->claim(g, MASK_HAT) < 0)
        return -1;
    g->set_pull(g, TE_REQS, GPIO_PULL_DOWN);
    g->set_pull(g, BIT(PIN_ATNA) | MASK_LISTEN_CTRL | BIT(PIN_SRQ) | BIT(PIN_DATA_OE_N), GPIO_PULL_UP);
    g->set_pull(g, MASK_DIO | MASK_TALK_CTRL, GPIO_PULL_UP);
    g->set_pull(g, MASK_MGMT_IN, GPIO_PULL_NONE);       /* driven by U4/U7/U8 */
    g->make_output(g, BIT(PIN_DATA_OE_N) | TE_REQS | BIT(PIN_ATNA));
    /* Every pin configured: enable U3. DATA_TE is low, so it drives the Pi side. */
    gpio_clr(g, BIT(PIN_DATA_OE_N));
    return 0;
}

void hat_atn_takeover(gpio_backend_t *g)
{
    /* Step 2: the board already reversed U3/U5/U9; stop driving into them */
    g->make_input(g, TALK_LINES);
    /* GPIO11/9 low before ATN can rise again, or the talk enables come back */
    gpio_clr(g, TE_REQS);
    /* Step 3: NDAC and NRFD driven low, then software owns NDAC */
    gpio_clr(g, MASK_LISTEN_CTRL);
    g->make_output(g, MASK_LISTEN_CTRL);
    gpio_clr(g, BIT(PIN_ATNA));
}

void hat_listener_release(gpio_backend_t *g)
{
    gpio_set(g, MASK_LISTEN_CTRL);
    g->make_input(g, MASK_LISTEN_CTRL);
    gpio_set(g, BIT(PIN_ATNA));
}

void hat_become_talker(gpio_backend_t *g)
{
    gpio_set(g, MASK_LISTEN_CTRL);
    g->make_input(g, MASK_LISTEN_CTRL);
    gpio_set(g, TALK_LINES);                 /* DAV, EOI, DIO released */
    g->make_output(g, TALK_LINES);
    gpio_set(g, TE_REQS);
    gpio_clr(g, BIT(PIN_DATA_OE_N));
    gpio_set(g, BIT(PIN_ATNA));
}

void hat_end_talk(gpio_backend_t *g)
{
    gpio_set(g, TALK_LINES);
    gpio_clr(g, TE_REQS);
    g->make_input(g, TALK_LINES);
}

/* ---- signals ---- */

static gpio_backend_t *sig_g;
static volatile sig_atomic_t *sig_stop;

static void on_stop(int sig)
{
    (void)sig;
    if (sig_stop) *sig_stop = 1;
}

static void on_fatal(int sig)
{
    if (sig_g) hat_safe_state(sig_g);
    signal(sig, SIG_DFL);
    raise(sig);
}

void hat_signals_install(gpio_backend_t *g, volatile sig_atomic_t *stop)
{
    struct sigaction sa;
    sig_g = g;
    sig_stop = stop;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_stop;              /* no SA_RESTART */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = on_fatal;
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}
