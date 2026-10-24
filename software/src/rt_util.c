#define _GNU_SOURCE
#include "rt_util.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
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

/* Is name (bare or name=value) on the kernel command line? Copies the value. */
static int cmdline_param(const char *name, char *val, size_t n)
{
    char buf[4096];
    if (read_line("/proc/cmdline", buf, sizeof buf) < 0) return 0;
    size_t len = strlen(name);
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        if (strncmp(tok, name, len) || (tok[len] && tok[len] != '=')) continue;
        if (val) snprintf(val, n, "%s", tok[len] ? tok + len + 1 : "");
        return 1;
    }
    return 0;
}

/* Is cpu in a sysfs CPU list such as "3", "2-3" or "1,3"? */
static int cpu_in_list(const char *list, int cpu)
{
    const char *p = list;
    while (*p >= '0' && *p <= '9') {
        char *end;
        long lo = strtol(p, &end, 10), hi = lo;
        if (*end == '-') hi = strtol(end + 1, &end, 10);
        if (cpu >= lo && cpu <= hi) return 1;
        if (*end != ',') break;
        p = end + 1;
    }
    return 0;
}

/* SCHED_FIFO/RR priority of the kernel thread called name: 0 if not RT, -1 if absent */
static int kthread_prio(const char *name)
{
    DIR *d = opendir("/proc");
    if (!d) return -1;
    int prio = -1;
    struct dirent *e;
    char path[300], comm[32];
    while ((e = readdir(d))) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        if (read_line(path, comm, sizeof comm) < 0 || strcmp(comm, name)) continue;
        pid_t pid = (pid_t)atoi(e->d_name);
        int pol = sched_getscheduler(pid);
        struct sched_param sp;
        prio = (pol == SCHED_FIFO || pol == SCHED_RR) && sched_getparam(pid, &sp) == 0 ? sp.sched_priority : 0;
        break;
    }
    closedir(d);
    return prio;
}

void rt_check_environment(FILE *f, int cpu, int prio)
{
    char buf[128], path[128];
    int iso = rt_isolated_cpu();
    if (iso < 0)
        fprintf(f, "warning: no isolated CPU (add isolcpus=3 to cmdline.txt, see boot/README.md)\n");
    else if (cpu >= 0 && cpu != iso)
        fprintf(f, "warning: running on CPU %d, isolated CPU is %d\n", cpu, iso);

    /* Without PREEMPT_RT, timer and RCU work runs inside the tick on the bus CPU */
    struct utsname u;
    int rt = uname(&u) == 0 && strstr(u.version, "PREEMPT_RT");
    if (!rt)
        fprintf(f, "warning: kernel is not PREEMPT_RT; timer and RCU work stalls the poll loop for "
                   "30-50 us (build the kernel with boot/build_kernel.sh)\n");

    /* The sysfs file only exists when the kernel was built with NO_HZ_FULL */
    if (read_line("/sys/devices/system/cpu/nohz_full", buf, sizeof buf) < 0)
        fprintf(f, "warning: kernel built without NO_HZ_FULL; the bus CPU keeps its tick, which "
                   "stalls the poll loop for 20-50 us under load (build the kernel with "
                   "boot/build_kernel.sh)\n");
    else if (cpu >= 0 && !cpu_in_list(buf, cpu)) {
        char v[16];
        int nohz_off = cmdline_param("nohz", v, sizeof v) && !strcmp(v, "off");
        fprintf(f, "warning: CPU %d is not in nohz_full (add nohz_full=%d rcu_nocbs=%d to "
                   "cmdline.txt%s, see boot/README.md)\n", cpu, cpu, cpu,
                nohz_off ? " and remove nohz=off" : "");
    }

    /* On PREEMPT_RT these per-CPU threads must outrank the bus thread or they starve */
    if (rt && cpu >= 0 && prio > 0) {
        static const char *const kt[][2] = {
            { "rcuc", "RCU stalls" }, { "ktimers", "kernel timers on that CPU never fire" },
        };
        for (size_t i = 0; i < sizeof kt / sizeof kt[0]; i++) {
            char name[32];
            snprintf(name, sizeof name, "%s/%d", kt[i][0], cpu);
            int p = kthread_prio(name);
            if (p >= 0 && p <= prio)
                fprintf(f, "warning: %s has RT priority %d, not above the bus thread's %d: %s "
                           "(run sudo hat_rt_setup)\n", name, p, prio, kt[i][1]);
        }
    }

    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu < 0 ? 0 : cpu);
    if (read_line(path, buf, sizeof buf) == 0 && strcmp(buf, "performance"))
        fprintf(f, "warning: CPU governor is '%s'; 'performance' avoids frequency-switch stalls "
                   "(run sudo hat_rt_setup)\n", buf);

    /* With RT throttling a SCHED_FIFO busy loop is parked for 50 ms every second */
    if (read_line("/proc/sys/kernel/sched_rt_runtime_us", buf, sizeof buf) == 0 && strcmp(buf, "-1"))
        fprintf(f, "warning: kernel.sched_rt_runtime_us=%s; set it to -1 or the SCHED_FIFO "
                   "poll loop can be throttled (run sudo hat_rt_setup)\n", buf);
}
