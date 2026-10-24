#!/bin/sh
# hat_rt_setup - run-time settings for the bus core (see boot/README.md).
#
#   sudo hat_rt_setup [CPU]
#
# CPU defaults to the first CPU in /sys/devices/system/cpu/isolated.
# Everything here resets at boot; the stage 2 systemd service runs it.

# Kernel threads that must outrank the bus thread (SCHED_FIFO 80 by default)
KTHREAD_PRIO=81

if [ "$(id -u)" -ne 0 ]; then
    echo "hat_rt_setup: run as root (sudo hat_rt_setup)" >&2
    exit 1
fi

cpu=$1
if [ -z "$cpu" ]; then
    cpu=$(sed 's/[,-].*//' /sys/devices/system/cpu/isolated)
fi
if [ -z "$cpu" ]; then
    echo "hat_rt_setup: no isolated CPU (add isolcpus=3 to cmdline.txt) and none given" >&2
    exit 1
fi

fail=0
set_file() {    # path value
    if ! echo "$2" > "$1" 2>/dev/null; then
        echo "hat_rt_setup: cannot write $2 to $1" >&2
        fail=1
    fi
}

# No RT throttling: it parks a SCHED_FIFO busy loop for 50 ms every second
sysctl -q -w kernel.sched_rt_runtime_us=-1 || fail=1
# vmstat work every 2 minutes instead of every second
sysctl -q -w vm.stat_interval=120 || fail=1

for g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
    [ -e "$g" ] && set_file "$g" performance
done

# Unbound workqueues on every CPU except the bus core
ncpu=$(getconf _NPROCESSORS_CONF)
set_file /sys/devices/virtual/workqueue/cpumask \
    "$(printf '%x' $(( ((1 << ncpu) - 1) & ~(1 << cpu) )))"

# Per-CPU timer and RCU threads (PREEMPT_RT only)
for name in ktimers rcuc; do
    pid=$(pgrep -x "$name/$cpu")
    if [ -n "$pid" ]; then
        chrt -f -p "$KTHREAD_PRIO" "$pid" || fail=1
    fi
done

echo "hat_rt_setup: CPU $cpu, workqueue mask $(cat /sys/devices/virtual/workqueue/cpumask)"
ps -eLo pid,cls,rtprio,comm | awk -v c="$cpu" 'NR == 1 || $4 == "ktimers/" c || $4 == "rcuc/" c'
exit $fail
