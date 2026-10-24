# Boot configuration

Raspberry Pi OS 64-bit. Tested on Trixie with the Raspberry Pi PREEMPT_RT kernel 6.18 on a Pi 5.

The bus thread busy-polls on CPU 3 and must never stop for 30 µs or more (half the PET's 64 µs timeout). Four things together get there: a real-time kernel, three `cmdline.txt` settings, the `config.txt` settings, and the run-time settings applied by `hat_rt_setup`.

## Kernel

The stock kernel (`kernel_2712.img` or `kernel8.img`) is not real-time and was built without `NO_HZ_FULL` and `RCU_NOCB_CPU`. Timer and RCU work runs inside the tick on CPU 3 and stalls the poll loop for 30–50 µs. Install the Raspberry Pi real-time kernel:

```bash
sudo apt install linux-image-rpi-v8-rt
```

`config.txt.snippet` selects it with `kernel=kernel8_rt.img`. The same image boots on the Pi 4 and the Pi 5. The Pi 5 then uses 4 KB pages instead of 16 KB, and the GPIO backends work with either.

After the reboot, `uname -v` must contain `PREEMPT_RT`.

## config.txt

Append [config.txt.snippet](config.txt.snippet) to `/boot/firmware/config.txt`.

## cmdline.txt

`/boot/firmware/cmdline.txt` is one line. Make two changes to it:

1. Remove `console=serial0,115200` if it is there. GPIO15 is NRFD.
2. Add the bus core settings at the end of the line:

   ```
   isolcpus=3 rcutree.kthread_prio=81 nohz=off
   ```

| Setting | Why |
| --- | --- |
| `isolcpus=3` | Keeps other tasks off CPU 3. |
| `rcutree.kthread_prio=81` | On PREEMPT_RT, RCU work for CPU 3 runs in the `rcuc/3` thread. At the default priority 1 the bus thread (SCHED_FIFO 80) starves it, and the kernel reports "rcu_preempt self-detected stall" within a minute. At 81 it runs for a few µs when needed. |
| `nohz=off` | Every CPU keeps its tick. Without it, idle CPUs hand their timers to the only busy CPU, which is CPU 3. Driver polls (thermal, Ethernet PHY, SD card detect, hwmon) then run there and re-arm there for good, each costing 20–40 µs. Neither `kernel.timer_migration=0` nor a cpuset isolated partition prevents this. |

Do not add `nohz_full=3` or `rcu_nocbs=3`. These kernels do not recognise them and pass them to user space (`dmesg` reports "Unknown kernel command line parameters"). The tools warn if they are present.

## Run-time settings

These come back to their defaults at every boot. Apply them with:

```bash
sudo hat_rt_setup
```

`make install` installs it from [rt_setup.sh](rt_setup.sh). The stage 2 systemd service will run it before the bus engine starts. It does the following:

| Setting | Value |
| --- | --- |
| `kernel.sched_rt_runtime_us` | `-1` (no RT throttling of the poll loop) |
| CPU frequency governor, every policy | `performance` |
| `vm.stat_interval` | `120` (vmstat work every 2 minutes instead of every second) |
| `/sys/devices/virtual/workqueue/cpumask` | every CPU except CPU 3 |
| `ktimers/3`, `rcuc/3` | SCHED_FIFO 81, above the bus thread. `rcuc/3` already has it from `rcutree.kthread_prio`. `ktimers/3` has no boot setting. If it stays at 1 it is starved, and kernel timers queued on CPU 3 never fire. |

`hat_bench`, `hat_monitor` and the other tools warn about any of these that are missing.

## Checking the result

```bash
cat /sys/devices/system/cpu/isolated
```

That should print `3`.

```bash
ps -eLo pid,cls,rtprio,psr,comm | awk 'NR==1 || $4==3'
```

`rcuc/3` and `ktimers/3` should show `FF 81`.

`sudo hat_bench` should print no warnings and say PASS. With no driver running, `sudo hat_safe --check` should report `safe`.

## Known limit

Measured on a Pi 5 (60 s runs):

| Load on CPUs 0–2 | Worst gap |
| --- | --- |
| None | 9–14 µs |
| One kind of load at a time (CPU spinners, continuous SD writes, filesystem walk) | 8–30 µs |
| All three at once | 39–51 µs, with many gaps of 30 µs or more |

Under that combined load, CPU 3's own tick handler slows from about 1 µs to 20–25 µs, through contention with the other cores. Only a kernel built with `NO_HZ_FULL` removes the tick from CPU 3. Until there is one, avoid heavy background jobs, such as `apt upgrade`, while the bus is active.
