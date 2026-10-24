/*
 * hat_monitor - log IEEE-488 activity seen by the HAT with timestamps.
 *
 *   hat_monitor            passive: drives nothing beyond the idle state
 *   hat_monitor --ack      minimal listener: acknowledges command bytes and,
 *                          when addressed (LISTEN <device>), data bytes
 *
 * The poll loop runs on one CPU at SCHED_FIFO; this thread prints.
 */
#define _GNU_SOURCE
#include "bus_monitor.h"
#include "hat_board.h"
#include "rt_util.h"

#include <ctype.h>
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop;

typedef struct {
    mon_ctx_t *mon;
    int cpu, prio;
} poll_args_t;

static void *poll_thread(void *arg)
{
    poll_args_t *a = arg;
    a->cpu = rt_setup_thread(a->cpu, a->prio);
    mon_run(a->mon);
    return NULL;
}

static void usage(void)
{
    puts("usage: hat_monitor [options]\n"
         "  --ack           acknowledge bytes as a listener (never talks)\n"
         "  --device N      primary address for --ack (default 8)\n"
         "  --raw           print every line change, not only bytes and ATN phases\n"
         "  --csv FILE      also write every event to FILE (t_us,raw,kind,byte,flags,lat_us)\n"
         "  --duration S    stop after S seconds (default: until Ctrl-C)\n"
         "  --cpu N         CPU for the poll loop (default: isolated CPU, else last)\n"
         "  --prio N        SCHED_FIFO priority (default 80, 0 = SCHED_OTHER)\n"
         "  --backend B     auto | rp1 | sim");
}

int main(int argc, char **argv)
{
    int ack = 0, device = 8, raw = 0, cpu = -1, prio = 80;
    double duration = 0;
    const char *csv = NULL;
    gpio_backend_kind_t kind = GPIO_BACKEND_AUTO;
    static const struct option opts[] = {
        { "ack", no_argument, 0, 'a' }, { "device", required_argument, 0, 'd' },
        { "raw", no_argument, 0, 'r' }, { "csv", required_argument, 0, 'c' },
        { "duration", required_argument, 0, 't' }, { "cpu", required_argument, 0, 'C' },
        { "prio", required_argument, 0, 'p' }, { "backend", required_argument, 0, 'b' },
        { "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "ad:rc:t:C:p:b:h", opts, NULL)) != -1) {
        switch (o) {
        case 'a': ack = 1; break;
        case 'd': device = atoi(optarg); break;
        case 'r': raw = 1; break;
        case 'c': csv = optarg; break;
        case 't': duration = atof(optarg); break;
        case 'C': cpu = atoi(optarg); break;
        case 'p': prio = atoi(optarg); break;
        case 'b': kind = gpio_kind_from_name(optarg); break;
        default: usage(); return o == 'h' ? 0 : 2;
        }
    }
    if ((int)kind < 0 || device < 0 || device > 30) { usage(); return 2; }

    gpio_backend_t *g = gpio_open(kind);
    if (!g) return 1;
    hat_signals_install(g, &stop);

    FILE *cf = NULL;
    if (csv && !(cf = fopen(csv, "w"))) { perror(csv); g->close(g); return 1; }
    if (cf) fputs("t_us,raw,kind,byte,flags,lat_us\n", cf);

    mon_ring_t *ring = calloc(1, sizeof *ring);
    mon_ctx_t mon = { .g = g, .ring = ring, .stop = &stop, .ack = ack, .device = device };
    poll_args_t pa = { .mon = &mon, .cpu = cpu, .prio = prio };
    rt_check_environment(stderr, cpu < 0 ? rt_isolated_cpu() : cpu, prio);
    fprintf(stderr, "hat_monitor: backend %s, %s mode%s; Ctrl-C to stop\n", g->name,
            ack ? "ack" : "passive", ack ? "" : " (the PET will time out after the first byte)");
    if (ack) fprintf(stderr, "hat_monitor: listening as device %d\n", device);

    pthread_t th;
    pthread_create(&th, NULL, poll_thread, &pa);

    uint64_t t_start = 0, deadline = 0;
    int started = 0;
    char lines[64], name[24];
    mon_event_t e;
    while (!stop) {
        int any = 0;
        while (mon_pop(ring, &e)) {
            any = 1;
            if (!started) { t_start = e.t; started = 1; if (duration > 0) deadline = t_start + rt_us_to_ticks(duration * 1e6); }
            double t = rt_ticks_to_us(e.t - t_start) / 1e6;
            if (cf)
                fprintf(cf, "%.3f,%07x,%d,%d,%d,%.3f\n", t * 1e6, e.raw, e.kind, e.byte, e.flags,
                        rt_ticks_to_us(e.lat));
            switch (e.kind) {
            case MON_EV_START:
                printf("%12.6f  START  %s\n", t, hat_format_lines(e.raw, lines, 1));
                break;
            case MON_EV_EDGE:
                if (raw) printf("%12.6f  edge   %s\n", t, hat_format_lines(e.raw, lines, 1));
                break;
            case MON_EV_BYTE: {
                int c = e.byte;
                printf("%12.6f  %s $%02X", t, (e.flags & MON_F_ATN) ? "CMD " : "DATA", c);
                if (e.flags & MON_F_ATN) printf("  %-12s", mon_command_name((uint8_t)c, name));
                else printf("  '%c'         ", isprint(c) ? c : '.');
                if (e.flags & MON_F_EOI) printf(" EOI");
                if (e.flags & MON_F_ACKED) printf("  ack %.2f us", rt_ticks_to_us(e.lat));
                putchar('\n');
                break;
            }
            case MON_EV_ATN_START: printf("%12.6f  ATN    command phase\n", t); break;
            case MON_EV_ATN_END:
                printf("%12.6f  ATN    released: %s\n", t,
                       e.byte == MON_ROLE_LISTEN ? "listener" : e.byte == MON_ROLE_TALK ?
                       "talker requested (not supported by the monitor; PET will time out)" : "not addressed");
                break;
            case MON_EV_IFC: printf("%12.6f  IFC    interface clear: roles dropped\n", t); break;
            }
        }
        if (deadline && rt_ticks() >= deadline) stop = 1;
        if (!any) { fflush(stdout); usleep(2000); }
    }
    pthread_join(th, NULL);
    while (mon_pop(ring, &e)) {}   /* drain */
    hat_safe_state(g);

    fprintf(stderr, "\nhat_monitor: %llu bytes, max poll gap %.2f us", (unsigned long long)mon.bytes,
            rt_ticks_to_us(mon.max_gap));
    if (ack) fprintf(stderr, ", max DAV-seen to NDAC-release %.2f us", rt_ticks_to_us(mon.max_lat));
    fprintf(stderr, ", %u events dropped; poll CPU %d\n", atomic_load(&ring->dropped), pa.cpu);
    if (cf) fclose(cf);
    free(ring);
    g->close(g);
    return 0;
}
