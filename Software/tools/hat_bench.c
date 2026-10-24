/*
 * hat_bench - GPIO backend latency benchmark.
 *
 * Touches only GPIO10 (J1 pin 19), which on REV03F goes to the R4 pull-up
 * and nothing else, so it is safe with or without the HAT and the PET.
 *
 * Measures: level-register read time, set/clear write time, write-to-visible
 * time (write, then poll the level register until it shows), direction
 * change time, and the worst gap of a busy-poll loop over a period - the
 * figure that decides whether the 64 us handshake budget holds.
 */
#define _GNU_SOURCE
#include "gpio_backend.h"
#include "hat_pins.h"
#include "rt_util.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

#define PIN BIT(PIN_SPARE)

static double per_op_ns(uint64_t ticks, long n) { return rt_ticks_to_us(ticks) * 1000.0 / (double)n; }

int main(int argc, char **argv)
{
    long n = 1000000;
    double duration = 10;
    int cpu = -1, prio = 80;
    gpio_backend_kind_t kind = GPIO_BACKEND_AUTO;
    static const struct option opts[] = {
        { "count", required_argument, 0, 'n' }, { "duration", required_argument, 0, 't' },
        { "cpu", required_argument, 0, 'C' }, { "prio", required_argument, 0, 'p' },
        { "backend", required_argument, 0, 'b' }, { "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "n:t:C:p:b:h", opts, NULL)) != -1) {
        switch (o) {
        case 'n': n = atol(optarg); break;
        case 't': duration = atof(optarg); break;
        case 'C': cpu = atoi(optarg); break;
        case 'p': prio = atoi(optarg); break;
        case 'b': kind = gpio_kind_from_name(optarg); break;
        default:
            puts("usage: hat_bench [-n COUNT] [--duration S] [--cpu N] [--prio N] [--backend B]\n"
                 "Uses GPIO10 only (spare, R4 pull-up).");
            return o == 'h' ? 0 : 2;
        }
    }
    if ((int)kind < 0 || n < 1) return 2;
    signal(SIGINT, on_sig);

    gpio_backend_t *g = gpio_open(kind);
    if (!g) return 1;
    cpu = rt_setup_thread(cpu, prio);
    rt_check_environment(stdout, cpu);
    printf("backend %s, CPU %d, SCHED_FIFO %d, timer %.1f MHz, %ld iterations\n\n",
           g->name, cpu, prio, rt_tick_hz() / 1e6, n);

    gpio_set(g, PIN);
    g->claim(g, PIN);
    g->set_pull(g, PIN, GPIO_PULL_UP);
    g->make_output(g, PIN);

    volatile uint32_t sink = 0;
    uint64_t t0, t1;

    /* 1. Read */
    t0 = rt_ticks();
    for (long i = 0; i < n; i++) sink += gpio_read(g);
    t1 = rt_ticks();
    printf("read level register      %8.1f ns/op\n", per_op_ns(t1 - t0, n));

    /* 2. Write (posted writes on RP1/PCIe: this is issue rate, not completion) */
    t0 = rt_ticks();
    for (long i = 0; i < n; i++) { gpio_clr(g, PIN); gpio_set(g, PIN); }
    t1 = rt_ticks();
    printf("set/clear write          %8.1f ns/op  (issue rate)\n", per_op_ns(t1 - t0, 2 * n));

    /* 3. Write until visible in the level register */
    uint64_t wmin = UINT64_MAX, wmax = 0, wsum = 0;
    long wn = n / 10 ? n / 10 : 1;
    for (long i = 0; i < wn && !stop; i++) {
        int want_low = i & 1;
        t0 = rt_ticks();
        if (want_low) gpio_clr(g, PIN); else gpio_set(g, PIN);
        while (((gpio_read(g) & PIN) != 0) == want_low) {}
        uint64_t d = rt_ticks() - t0;
        if (d < wmin) wmin = d;
        if (d > wmax) wmax = d;
        wsum += d;
    }
    printf("write -> visible on read %8.1f ns avg, %.1f min, %.1f max\n",
           per_op_ns(wsum, wn), per_op_ns(wmin, 1), per_op_ns(wmax, 1));

    /* 4. Direction change (pin high on both sides: no current) */
    gpio_set(g, PIN);
    long dn = n / 10 ? n / 10 : 1;
    t0 = rt_ticks();
    for (long i = 0; i < dn; i++) { g->make_input(g, PIN); g->make_output(g, PIN); }
    t1 = rt_ticks();
    printf("direction change         %8.1f ns/op\n", per_op_ns(t1 - t0, 2 * dn));
    g->make_input(g, PIN);

    /* 5. Busy-poll gap histogram */
    static const double edges_us[] = { 1, 2, 5, 10, 20, 30, 64, 100, 1000 };
    enum { NB = sizeof edges_us / sizeof edges_us[0] };
    uint64_t edges[NB], hist[NB + 1] = { 0 }, maxgap = 0, polls = 0;
    for (int i = 0; i < NB; i++) edges[i] = rt_us_to_ticks(edges_us[i]);
    printf("\nbusy-poll loop for %.0f s (Ctrl-C to cut short)...\n", duration);
    uint64_t end = rt_ticks() + rt_us_to_ticks(duration * 1e6), prev = rt_ticks();
    for (;;) {
        sink += gpio_read(g);
        uint64_t t = rt_ticks(), gap = t - prev;
        prev = t;
        polls++;
        int b = 0;
        while (b < NB && gap >= edges[b]) b++;
        hist[b]++;
        if (gap > maxgap) maxgap = gap;
        if (t >= end || ((polls & 0xffff) == 0 && stop)) break;
    }
    printf("polls %llu, max gap %.2f us\n", (unsigned long long)polls, rt_ticks_to_us(maxgap));
    for (int b = 0; b <= NB; b++) {
        if (!hist[b]) continue;
        if (b == 0) printf("   < %6.0f us  %llu\n", edges_us[0], (unsigned long long)hist[b]);
        else if (b == NB) printf("  >= %6.0f us  %llu\n", edges_us[NB - 1], (unsigned long long)hist[b]);
        else printf("  %4.0f-%4.0f us  %llu\n", edges_us[b - 1], edges_us[b], (unsigned long long)hist[b]);
    }
    uint64_t over30 = 0;
    for (int b = 0; b <= NB; b++) if (b > 5) over30 += hist[b];
    printf("%s: %llu gaps >= 30 us (acceptance budget is half the PET's 64 us timeout)\n",
           over30 ? "FAIL" : "PASS", (unsigned long long)over30);

    (void)sink;
    g->close(g);
    return over30 ? 1 : 0;
}
