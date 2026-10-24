# CBM 4032 IEEE-488 HAT software

Raspberry Pi 4 / Pi 5 software for the REV03F IEEE-488 HAT. This is **stage 1** of the driver brief: bring-up tools. Stage 2 (bus engine) and stage 3 (disk-drive emulation) start once stage 1 has been accepted on real hardware.

Language: C11, plain Makefile, no dependencies beyond libc and pthreads. (The brief leaves the language open; C was chosen for direct register access and timing.)

## Layout

| Path | What |
| --- | --- |
| `include/hat_pins.h` | GPIO map from the REV03F netlist, masks, DIO gather/spread |
| `include/gpio_backend.h`, `src/gpio_*.c` | One GPIO API with three backends: `bcm2711` (Pi 4, `/dev/gpiomem`), `rp1` (Pi 5, `/dev/gpiomem0`), and `sim` (a software model of the board for tests) |
| `include/hat_board.h`, `src/hat_board.c` | Safe state, configuration, and the role changes of brief section 5 |
| `include/rt_util.h`, `src/rt_util.c` | CPU pinning, SCHED\_FIFO, timestamp counter, system checks |
| `src/bus_monitor.c` | Busy-poll capture loop, with an optional listener-acknowledge mode |
| `tools/` | `hat_bench`, `hat_selftest`, `hat_monitor`, `hat_safe` |
| `tests/` | Unit tests and a step-driven PET model, run on the `sim` backend |
| `boot/` | `config.txt` / `cmdline.txt` settings (brief section 7) |
| `docs/HARDWARE_CHECK.md` | Netlist check of the brief, and points that need a decision |

## Build

On the Pi (Raspberry Pi OS Bookworm 64-bit):

```bash
sudo apt install build-essential
```

```bash
make && make test && sudo make install
```

Cross-compiling from another Linux machine: `make CC=aarch64-linux-gnu-gcc AR=aarch64-linux-gnu-ar`. Every tool also runs on any Linux host with `--backend sim`.

The tools need access to `/dev/gpiomem` (Pi 4) or `/dev/gpiomem0` (Pi 5). The `gpio` group has it, but SCHED\_FIFO and `mlockall` need root, so run them with `sudo`.

## Tools

| Tool | Drives | Use |
| --- | --- | --- |
| `hat_safe [--check]` | Safe state only | Puts every line in the brief's safe state, or reports whether it is in it |
| `hat_bench` | GPIO10 only (spare pin) | Read, write, write-to-visible and direction-change latency, plus a worst-case poll-gap histogram. Exits non-zero if any gap is ≥ 30 µs |
| `hat_selftest --auto` | Bus lines; **HAT unplugged** | Automatic board checks that use the board's own paths as loopbacks |
| `hat_selftest --probe` | Bus lines; **HAT unplugged** | Touch each J2 contact to ground with a jumper; checks every receive path and the ATN gating of both talk enables |
| `hat_selftest --manual` | Bus lines; **HAT unplugged** | Steps through every output, for checking with a meter or logic analyser on J2 |
| `hat_selftest --read-only [--watch] [--rx]` | Nothing (`--rx` enables U3 in receive direction only) | Decodes the lines with the PET on |
| `hat_monitor [--ack] [--raw] [--csv F]` | Passive: idle state only. `--ack`: NDAC/NRFD | Timestamped log of bus activity |

The active self-test modes refuse to run if REN reads asserted, because the PET ties REN to ground, so that means the HAT is on a PET. `--force` overrides this.

Every tool returns the board to the safe state when it exits, when it gets SIGINT/SIGTERM/SIGHUP, and on fatal signals. SIGKILL can't be caught, so if a tool is killed that way, run `hat_safe`.

## Bring-up procedure

Do the steps in order. Stop at the first failure and report it with the tool output, plus a photo or analyser trace (brief, section 9).

1. **Pi without the HAT.** Install the RT kernel and apply `boot/` settings ([boot/README.md](boot/README.md)), reboot, run `sudo hat_rt_setup`. Run `sudo hat_bench` on the Pi 4 and the Pi 5, and keep both outputs: they are the benchmark deliverable. The poll-gap result must say PASS.
2. **HAT on the Pi, not on the PET.** Run `sudo hat_safe --check`, then `sudo hat_selftest`. All checks should pass.
3. Run `sudo hat_selftest --probe` with a jumper wire. Phase 1 touches every listed contact. Phase 2 touches only contact 11. Phase 3 touches only contacts 7 and 8 (DAV/EOI are being driven then).
4. Run `sudo hat_selftest --manual` with a meter or logic analyser on J2.
5. **HAT on the PET, PET off.** Run `sudo hat_selftest --read-only`: REN should read asserted. Switch the PET on and use `--read-only --watch --rx` to watch the lines.
6. **Monitor.** Run `sudo hat_monitor --raw`, and on the PET type `LOAD"$",8`. You should see ATN, then `CMD $28 LISTEN 8`, then nothing, because the PET times out. That is expected in passive mode.
7. **Ack mode.** Run `sudo hat_monitor --ack` and type `LOAD"$",8` again. You should see the whole command sequence: LISTEN 8, OPEN, the `$` filename with EOI, UNLISTEN, then TALK 8 and DATA ch 0. The PET then times out waiting for data and reports an error. Each acknowledged byte shows its DAV-seen-to-NDAC-release time, and the summary shows the worst case.

Switch the Pi off with the HAT on the PET: the PET must report DEVICE NOT PRESENT and stay usable (acceptance test, section 8).

## Tests

`make test` runs:

- the DIO gather/spread math against a reference, for 100,000 random words and all 256 bytes;
- the safe state, configuration and every section 5 sequence on the board model, checking that the Pi never drives a pin to a level the board is driving the other way;
- the monitor's ack mode against a PET model running a LOAD-like script, an absent device and an IFC reset, plus passive mode (every byte times out);
- `hat_selftest --auto` and `hat_safe` against the board model.

The board model and PET model in `src/gpio_sim.c` and `tests/pet_model.c` are the starting point for the stage 2 simulated PET.

## Not done yet (stages 2 and 3)

Talker handshake engine, systemd service, recorded-trace tests, disk-drive emulation, command channel, HAT+ EEPROM image and device-tree overlay.
