# CBM 4032 HAT: bring-up handover

As of 2026-10-27. For the software team starting Stage 2.

Hardware bring-up of the REV03F IEEE-488 HAT passed on a Raspberry Pi 5 connected to a real CBM 4032. The HAT receives every bus line, and as a listener it acknowledges each byte in 1.00 µs. The full test log is in [BRINGUP_TEST_PLAN.md](../../BRINGUP_TEST_PLAN.md).

## Status

| Stage | Result | Evidence |
| --- | --- | --- |
| 1 Resistance checks | Pass | Values recorded in the plan |
| 2 5 V bench test | Pass | 97 mA at switch-on; receive paths 2.9–2.15 OK; 2.16–2.18 not recorded |
| 3A Pi 5 latency benchmark | Pass, on the custom kernel | Worst poll gap 5.9–7.2 µs idle, 11.9 µs under 5 min of heavy load |
| 3B HAT on the Pi | Pass | 5V_HAT 5.13 V, 3V3 3.31 V; `hat_safe`, `hat_selftest --auto/--probe/--manual`, EEPROM all pass |
| 4B PET on, Pi off | Partly recorded | 4.5 and 4.6 OK; 4.7, 4.8 not recorded |
| 4C PET and Pi on | Pass, after a PET contact repair | 4.9–4.12 pass; 4.13 not recorded |

## Decisions

| Decision | Why | Where |
| --- | --- | --- |
| Pi 5 only; the Pi 4 (BCM2711) backend is removed | All latency work was done on the Pi 5 RP1 backend | Commit `a1a4515` |
| Custom PREEMPT_RT kernel with `NO_HZ_FULL` and `RCU_NOCB_CPU` | The stock kernel gave 30–80 µs gaps. The packaged RT kernel gave 39–51 µs gaps under load, all from CPU 3's own timer tick. | `software/boot/build_kernel.sh`, `software/boot/README.md` |
| CPU 3 is the bus core: `isolcpus=3 nohz_full=3 rcu_nocbs=3 rcutree.kthread_prio=81` | Removes the tick, RCU callbacks and other tasks from CPU 3 | `software/boot/README.md` |
| The bus thread runs at SCHED_FIFO 80; `rcuc/3` and `ktimers/3` run at 81 | At priority 1 they were starved, and the kernel reported RCU stalls within a minute | `hat_rt_setup`, `rcutree.kthread_prio` |
| `hat_rt_setup` applies the run-time settings after every boot | Sysctls, the performance governor, the workqueue mask and kthread priorities all reset at boot | `software/boot/rt_setup.sh` |
| Pins are claimed before they are read (`claim_inputs`) | On RP1 an unclaimed pin reads 0 | Commit `0af87d0` |
| The simulator models RP1: unclaimed pins read 0 | So `make test` catches reads before claiming | `software/src/gpio_sim.c` |
| Tools check the environment at startup and warn | Every problem above was silent until measured | `rt_check_environment` in `software/src/rt_util.c` |

## Pi setup the bus engine depends on

1. Build and install the kernel with `software/boot/build_kernel.sh` (about an hour on a Pi 5). It installs `kernel8_rtnohz.img`. `apt` kernel updates never replace it, so re-run the script to move to newer source.
2. Append `software/boot/config.txt.snippet` to `config.txt`. It selects the kernel, and its `gpio=` lines put the HAT in its safe state at power-up.
3. Add `isolcpus=3 nohz_full=3 rcu_nocbs=3 rcutree.kthread_prio=81` to `cmdline.txt`. Do not add `nohz=off`: it disables `nohz_full`.
4. Run `sudo hat_rt_setup` after every boot, before the bus engine starts.
5. Check: `/sys/devices/system/cpu/nohz_full` prints `3`, and `sudo hat_bench` prints no warnings.

## What the hardware tests taught us

| Finding | Measured | Consequence |
| --- | --- | --- |
| **The PET's handshake timeout is about 65 ms, not 64 µs** | The PET held DAV for 65.6–66.0 ms per byte before giving up | `hat_bench`'s 30 µs pass limit is based on a wrong premise; the real deadline is about 1,000 times looser |
| Listener acknowledge time | 0.96–1.00 µs from DAV seen to NDAC released | About 65,000 times inside the timeout |
| GPIO cost on RP1 (over PCIe) | Read 482 ns; write issue 25 ns; write visible on read after 1.16 µs | Reads dominate the poll loop: about 1 µs per iteration |
| Hardware NDAC hold (U22/U23) works on the real PET | With the hold, the PET finds a device instead of reporting `DEVICE NOT PRESENT` | Software does not need to react to ATN within IEEE-488's 200 ns |
| A full `LOAD"$",8` command exchange in ack mode | 0.76 ms; the PET took 37 µs from ATN to the first DAV | |
| An unclaimed RP1 pin reads 0 | ATN, REN, IFC, DAV, EOI and GPIO10 read low until claimed | This made `hat_selftest` think it was on a PET; fixed |
| IFC pulses when the PET is switched off and when it is switched on | Seen in 4.10 | The engine must drop all roles on IFC at both events |
| Sources of delay on CPU 3 | The timer tick; starved `rcuc`/`ktimers`; driver polls moving to the only busy core; kworkers run by the scheduler's fair server | All handled by the kernel and settings above |
| Boot transient | One gap of up to 29 µs in the first minutes after boot; 23 µs max in a `hat_monitor` run | Start the bus engine after boot has settled |
| CPU 3 must not be taken offline | On this kernel it fails to come back online until a reboot | Never use CPU hotplug to reset CPU 3 |
| The PET's IEEE-488 edge contacts were oxidised | ATN reached the HAT but DAV did not, until they were resoldered | Check a PET's contacts before blaming the HAT |

Debug aids from the PET side, typed in BASIC:
- **Assert ATN:** `POKE 59456,PEEK(59456) AND 251`; release with `OR 4`.
- **Assert DAV:** `POKE 59427,52`; release with `POKE 59427,60`.
- **Read the bus from the PET:** `PEEK(59456)` shows NDAC as bit 0 (1), NRFD as bit 6 (64) and DAV as bit 7 (128). A set bit means the line is high (released).

## Rules for the Stage 2 software

- Claim every HAT pin (`hat_configure` or `claim_inputs`) before reading any of them.
- On ATN falling, lower GPIO11 and GPIO9 right after switching the talk lines to inputs (`hat_atn_takeover`; `software/docs/HARDWARE_CHECK.md`, point 1). A unit test enforces this.
- Run exactly one task on CPU 3, the bus thread. A second runnable task brings the tick back.
- Keep the bus thread at SCHED_FIFO 80 or lower, below `rcuc/3` and `ktimers/3` at 81.
- The systemd service runs `hat_rt_setup` before the engine and `hat_safe` as `ExecStopPost`.
- Use 65 ms as the hard deadline, and keep acknowledgements fast for throughput.

## Open items

- [ ] Systemd service: `hat_rt_setup`, the bus engine, `hat_safe` on stop
- [ ] Talker handshake engine (Stage 2), then disk-drive emulation (Stage 3)
- [ ] Confirm the 65 ms timeout against the PET ROM, then fix the 64 µs wording and the 30 µs limit in `hat_bench`, `rt_util.h` and the READMEs
- [ ] A long `hat_monitor --ack` run to explain the 23 µs poll gap
- [ ] Confirm `HARDWARE_CHECK.md` point 1 (the ATN-fall ordering rule) and add it to the driver brief
- [ ] Record the missing plan rows: 3.3, 4.1–4.4, 4.7, 4.8, 4.13
- [ ] Remove the bring-up SSH key and passwordless sudo from the Pi

Commits from bring-up: `ad14414` (run-time settings and checks), `7773a74` (LF endings), `a1a4515` (custom kernel, Pi 4 removed), `0af87d0` (`claim_inputs` fix and test results).
