#define _GNU_SOURCE
#include "rt_util.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int read_line(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)n, f)) buf[0] = 0;
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;
    return 0;
}

int rt_isolated_cpu(void)
{
    char buf[128];
    if (read_line("/sys/devices/system/cpu/isolated", buf, sizeof buf) < 0 || !buf[0])
        return -1;
    return atoi(buf);   /* "3" or "2-3" or "2,3": first CPU */
}

int rt_setup_thread(int cpu, int prio)
{
    if (cpu < 0) cpu = rt_isolated_cpu();
    if (cpu < 0) cpu = (int)sysconf(_SC_NPROCESSORS_ONLN) - 1;

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    int e = pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    if (e) fprintf(stderr, "warning: cannot pin to CPU %d: %s\n", cpu, strerror(e));

    if (prio > 0) {
        struct sched_param sp = { .sched_priority = prio };
        e = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
        if (e) fprintf(stderr, "warning: SCHED_FIFO %d refused (%s); run as root or with CAP_SYS_NICE\n",
                       prio, strerror(e));
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        fprintf(stderr, "warning: mlockall failed: %s\n", strerror(errno));
    return cpu;
}

void rt_check_environment(FILE *f, int cpu)
{
    char buf[128], path[128];
    int iso = rt_isolated_cpu();
    if (iso < 0)
        fprintf(f, "warning: no isolated CPU (add isolcpus=3 nohz_full=3 to cmdline.txt)\n");
    else if (cpu >= 0 && cpu != iso)
        fprintf(f, "warning: running on CPU %d, isolated CPU is %d\n", cpu, iso);

    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu < 0 ? 0 : cpu);
    if (read_line(path, buf, sizeof buf) == 0 && strcmp(buf, "performance"))
        fprintf(f, "warning: CPU governor is '%s'; 'performance' avoids frequency-switch stalls\n", buf);

    /* With RT throttling a SCHED_FIFO busy loop is parked for 50 ms every second */
    if (read_line("/proc/sys/kernel/sched_rt_runtime_us", buf, sizeof buf) == 0 && strcmp(buf, "-1"))
        fprintf(f, "warning: kernel.sched_rt_runtime_us=%s; set it to -1 or the SCHED_FIFO "
                   "poll loop can be throttled\n", buf);
}
