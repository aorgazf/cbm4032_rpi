# Boot configuration

Raspberry Pi 5 only, Raspberry Pi OS 64-bit (tested on Trixie).

The bus thread busy-polls on CPU 3 and must never stop for 30 µs or more (half the PET's 64 µs timeout). Four things together get there: a custom kernel, the `cmdline.txt` settings, the `config.txt` settings, and the run-time settings applied by `hat_rt_setup`.

## Kernel

CPU 3 needs a PREEMPT_RT kernel built with `NO_HZ_FULL` and `RCU_NOCB_CPU`. Raspberry Pi ships none:

| Kernel | Problem |
| --- | --- |
| Stock (`kernel_2712.img`) | Not real-time. No `NO_HZ_FULL` or `RCU_NOCB_CPU`, so it ignores `nohz_full=` and `rcu_nocbs=`. Timer and RCU work runs inside the tick on CPU 3: gaps of 30–50 µs. |
| Raspberry Pi RT (`linux-image-rpi-v8-rt`) | Real-time, but still no `NO_HZ_FULL`. CPU 3 keeps its 250 Hz tick. With the other cores busy, the tick handler slows to 20–25 µs, giving gaps of 39–51 µs. |
| RT + `NO_HZ_FULL` (built here) | No tick on CPU 3 while the bus thread runs. |

Build it on the Pi with [build_kernel.sh](build_kernel.sh). It takes about an hour on a Pi 5:

```bash
sudo apt install linux-image-rpi-v8-rt
```

```bash
boot/build_kernel.sh
```

The script starts from the packaged RT kernel's config (`/boot/config-*-v8-rt`), adds `NO_HZ_FULL` and `RCU_NOCB_CPU`, builds the `rpi-6.18.y` branch of the Raspberry Pi kernel source, and installs `/boot/firmware/kernel8_rtnohz.img`, its initramfs and its modules. It does not change `config.txt` or `cmdline.txt`.

It is the generic arm64 configuration, so the Pi 5 uses 4 KB pages instead of 16 KB. The RP1 backend works with either. `apt` kernel updates do not replace this kernel. Re-run the script to move to a newer source.

To try the kernel once before making it the default, put the new `kernel=` line in `/boot/firmware/tryboot.txt` (a copy of `config.txt`) together with `cmdline=` pointing at a copy of `cmdline.txt` that has the new settings. Then run `sudo reboot '0 tryboot'`. A power cycle returns to the normal files.

After the reboot, `uname -v` must contain `PREEMPT_RT` and `cat /sys/devices/system/cpu/nohz_full` must print `3`.

## config.txt

Append [config.txt.snippet](config.txt.snippet) to `/boot/firmware/config.txt`. It selects the kernel with `kernel=kernel8_rtnohz.img`.

## cmdline.txt

`/boot/firmware/cmdline.txt` is one line. Make two changes to it:

1. Remove `console=serial0,115200` if it is there. GPIO15 is NRFD.
2. Add the bus core settings at the end of the line:

   ```
   isolcpus=3 nohz_full=3 rcu_nocbs=3 rcutree.kthread_prio=81
   ```

| Setting | Why |
| --- | --- |
| `isolcpus=3` | Keeps other tasks off CPU 3. |
| `nohz_full=3` | Stops the tick on CPU 3 while a single task runs there. It also keeps unbound timers and driver polls on the other CPUs. |
| `rcu_nocbs=3` | Runs CPU 3's RCU callbacks on the other CPUs. |
| `rcutree.kthread_prio=81` | On PREEMPT_RT the RCU threads run at SCHED_FIFO 1 by default. The bus thread (SCHED_FIFO 80) starves them, and the kernel reports "rcu_preempt self-detected stall" within a minute. At 81 they run for a few µs when needed. |

Do not add `nohz=off`. It disables `nohz_full`. (It was the workaround on the RT kernel without `NO_HZ_FULL`: it stopped idle CPUs handing their timers to CPU 3.)

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
cat /sys/devices/system/cpu/isolated /sys/devices/system/cpu/nohz_full
```

Both should print `3`.

```bash
ps -eLo pid,cls,rtprio,psr,comm | awk 'NR==1 || $4==3'
```

`rcuc/3` and `ktimers/3` should show `FF 81`.

`sudo hat_bench` should print no warnings and say PASS. With no driver running, `sudo hat_safe --check` should report `safe`.

## Measured results

Pi 5, kernel `6.18.58-v8-rt-nohzfull`, 60 s runs unless noted:

| Load on CPUs 0–2 | Worst gap |
| --- | --- |
| None | 5.9–6.7 µs |
| CPU spinners, continuous SD writes and a filesystem walk together, 5 min | 11.9 µs |
| None, in the first minutes after boot | up to 29 µs, while boot services finish |

The ticks on CPU 3 (the 2–5 µs bin of the histogram) dropped from about 15,000 per minute to about 10. Start the bus engine after the system has finished booting.
