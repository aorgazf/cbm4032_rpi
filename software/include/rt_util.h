/*
 * Real-time helpers for the bus thread: CPU pinning, SCHED_FIFO, memory
 * locking, a cheap timestamp counter, and checks of the system settings the
 * 64 us handshake budget depends on (driver brief, section 6).
 */
#ifndef RT_UTIL_H
#define RT_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* First CPU in /sys/devices/system/cpu/isolated, or -1 */
int rt_isolated_cpu(void);

/*
 * Pin the calling thread to cpu (-1: isolated CPU if any, else the last
 * online CPU), switch it to SCHED_FIFO at prio (0: leave SCHED_OTHER) and
 * lock all memory. Prints warnings for anything that fails; returns the CPU.
 */
int rt_setup_thread(int cpu, int prio);

/*
 * Print warnings about settings that cause long stalls on the bus CPU: isolation,
 * kernel type and command line, rcuc/ktimers priority against the bus thread's
 * SCHED_FIFO prio (0: not checked), governor, RT throttling. See boot/README.md.
 */
void rt_check_environment(FILE *f, int cpu, int prio);

/* Timestamp counter: the ARM generic timer on arm64 (54 MHz on BCM2712),
 * CLOCK_MONOTONIC_RAW nanoseconds elsewhere. */
#if defined(__aarch64__)
static inline uint64_t rt_ticks(void)
{
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v) :: "memory");
    return v;
}
static inline uint64_t rt_tick_hz(void)
{
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f;
}
#else
static inline uint64_t rt_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline uint64_t rt_tick_hz(void) { return 1000000000ull; }
#endif

static inline double rt_ticks_to_us(uint64_t t) { return (double)t * 1e6 / (double)rt_tick_hz(); }
static inline uint64_t rt_us_to_ticks(double us) { return (uint64_t)(us * (double)rt_tick_hz() / 1e6); }

#endif
