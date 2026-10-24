# Boot configuration

Raspberry Pi OS Bookworm 64-bit (the brief assumes this; still an open question).

## config.txt

Append [config.txt.snippet](config.txt.snippet) to `/boot/firmware/config.txt`.

## cmdline.txt

`/boot/firmware/cmdline.txt` is one line. Make two changes to it:

1. Remove `console=serial0,115200` if it is there. GPIO15 is NRFD.
2. Add the bus core reservation at the end of the line:

   ```
   isolcpus=3 nohz_full=3 rcu_nocbs=3
   ```

## Run-time settings

These stop the kernel from parking the busy-poll thread. They come back to their defaults at reboot; the stage 2 systemd service will set them.

```bash
sudo sysctl kernel.sched_rt_runtime_us=-1
```

```bash
echo performance | sudo tee /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
```

`hat_bench`, `hat_monitor` and the other tools warn about any of these that are missing.

## Checking the result

```bash
cat /sys/devices/system/cpu/isolated
```

That should print `3`. With no driver running, `sudo hat_safe --check` should report `safe`.
