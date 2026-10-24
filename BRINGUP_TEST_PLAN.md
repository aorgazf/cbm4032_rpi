# REV03F HAT bring-up test plan

Board: CBM 4032 IEEE-488 HAT, PCB REV03F, assembled. Target: Raspberry Pi 4 Model B and a CBM 4032-32N.
Equipment: multimeter, a 5 V supply, a few jumper wires (Dupont female-female and one with a crocodile clip).

Each stage protects something more expensive than the one before it: first the board, then the Pi, then the PET. **Do the stages in order and stop at the first failure.** Write each reading in the "Got" column and send me the table if anything is off.

Pin references used below:

- **J1** is the 40-pin Pi header (pin 1 = 3V3, pin 2 = 5V, pin 6 = GND). On a stacking header the pins stick out underneath, so you can clip onto them.
- **J2** is the PET edge connector. Contacts 1–12 are on one row, A–N on the other. 12 and F–N are ground.
- **U1** (SN75160B, data) and **U2** (SN75161B, control) are the two DIP-20 chips. Their legs are the easiest places to probe. Pin 1 is next to the notch, and pins are numbered anticlockwise seen from above.

| U2 pin | Signal | | U2 pin | Signal |
| --- | --- | --- | --- | --- |
| 1 | TE (CTRL_TE) | | 20 | VCC (5V_HAT) |
| 2 | REN bus → J2-E | | 19 | REN terminal side |
| 3 | IFC bus → J2-9 | | 18 | IFC terminal side |
| 4 | NDAC bus → J2-8 | | 17 | NDAC terminal side |
| 5 | NRFD bus → J2-7 | | 16 | NRFD terminal side |
| 6 | DAV bus → J2-6 | | 15 | DAV terminal side |
| 7 | EOI bus → J2-5 | | 14 | EOI terminal side |
| 8 | ATN bus → J2-11 | | 13 | ATN terminal side |
| 9 | SRQ bus → J2-10 | | 12 | SRQ terminal side |
| 10 | GND | | 11 | DC (held high by R11) |

U1: pin 1 = TE (DATA_TE), pins 2–9 = bus DIO1–8 (J2-1, 2, 3, 4, A, B, C, D), pins 19 down to 12 = terminal-side DIO1–8, pin 10 = GND, pin 11 = PE (GND), pin 20 = 5V_HAT.

---

## Stage 0: Visual inspection (no power)

| # | Check | OK? |
| --- | --- | --- |
| 0.1 | U1 and U2 notches point the way the silkscreen shows. U1 is the SN75160B and U2 is the SN75161B; they are easy to swap. | OK |
| 0.2 | U3 (TSSOP-24): no solder bridges between pins (use a magnifier or phone macro photo). | OK |
| 0.3 | Resistor rows R8–R10, R20–R27 and R34–R41 (1.5 mm pitch): no bridges between neighbours. | OK |
| 0.4 | Every hand-soldered joint on J1 (40), J2 (24), U1 and U2 (20 each) is shiny and fillets fully, with no bridges. | OK |
| 0.5 | No flux balls or stray solder under J2 or between the J2 tabs. | OK |
| 0.6 | Gates U18 and U20–U23 sit flat and are oriented the same way as their silkscreen. | OK |

---

## Stage 1: Resistance checks (no power)

Use the 20 kΩ range, or the diode/continuity range where noted. Black probe on GND (J1-6) unless the table says otherwise. Readings on supply rails start low and then climb while the capacitors charge, so wait for the reading to settle.

### 1A Rails: this catches the faults that would damage the Pi

| # | Between | Expect | Got |
| --- | --- | --- | --- |
| 1.1 | J1-2 (5V) ↔ J1-6 (GND) | Climbs to > 1 kΩ. **Under 50 Ω = short: stop** | 850 |
| 1.2 | J1-1 (3V3) ↔ GND | Climbs to > 1 kΩ. **Under 50 Ω: stop** | 4570 |
| 1.3 | J1-1 (3V3) ↔ J1-2 (5V) | > 1 kΩ in both probe directions | 5780 |
| 1.4 | J1-2 ↔ U2 pin 20 (5V_HAT) | ≈ 0–1 Ω (through fuse F1) | 0.9 |
| 1.5 | J1-1 ↔ J1-17, and J1-2 ↔ J1-4 | ≈ 0 Ω | ok |
| 1.6 | All GND pins J1-6, 9, 14, 20, 25, 30, 34, 39 ↔ J2-12 | ≈ 0 Ω | ok |
| 1.7 | U1 pin 10 and U2 pin 10 ↔ GND; U1 pin 20 and U2 pin 20 ↔ 5V_HAT | ≈ 0 Ω | pk |

### 1B No GPIO pin may touch 5V_HAT

Red probe on U2 pin 20 (5V_HAT), black probe on each J1 GPIO pin: 7, 10, 11, 12, 13, 15, 16, 18, 19, 21, 22, 23, 24, 29, 32, 33, 35, 36, 37, 38, 40, 27, 28.

| # | Expect | Got |
| --- | --- | --- |
| 1.8 | Every pin reads > 1 kΩ. **Any pin under ~200 Ω is a short to 5 V and would destroy that Pi GPIO: stop.** | OK |

### 1C Pull resistors, seen from J1 (also checks the J1 joints)

| # | Between | Part | Expect | Got |
| --- | --- | --- | --- | --- |
| 1.9 | J1-24 (GPIO8) ↔ J1-1 (3V3) | R2 | ≈ 10 kΩ | 9.95k |
| 1.10 | J1-19 (GPIO10) ↔ 3V3 | R4 | ≈ 10 kΩ | 9.97k |
| 1.11 | J1-16 (GPIO23) ↔ 3V3 | R5 | ≈ 10 kΩ | 9.94 |
| 1.12 | J1-12 (GPIO18) ↔ 3V3 | R6 | ≈ 10 kΩ | 9.98 |
| 1.13 | J1-10 (GPIO15) ↔ 3V3 | R7 | ≈ 10 kΩ | 9.96 |
| 1.14 | J1-36 (GPIO16) ↔ 3V3 | R30 | ≈ 10 kΩ | 9.96 |
| 1.15 | J1-21 (GPIO9) ↔ GND | R29 | ≈ 10 kΩ | 9.97 |
| 1.16 | J1-23 (GPIO11) ↔ GND | R28 | ≈ 10 kΩ | 9.96 |
| 1.17 | J1-27 and J1-28 (ID_SD, ID_SC) ↔ 3V3 | R8, R9 | ≈ 3.9 kΩ each | 3.88 |
| 1.18 | U2 pin 1 (CTRL_TE) ↔ GND | R3 | ≈ 4.7 kΩ | 4.69 |
| 1.19 | U1 pin 1 (DATA_TE) ↔ GND | R1 | ≈ 4.7 kΩ | 4.68 |
| 1.20 | U2 pin 11 (DC) ↔ 5V_HAT | R11 | ≈ 10 kΩ | 8.52 |

Readings within about ±15 % are fine: the chips around each resistor shift the value slightly. A reading near 0 Ω or open circuit means a bridge or an open joint.

### 1D The J2 contacts

| # | Check | Expect | Got |
| --- | --- | --- | --- |
| 1.21 | J2-12 and J2-F, H, J, K, L, M, N ↔ GND | ≈ 0 Ω (beep) | OK |
| 1.22 | J2-E (REN) ↔ GND | **Not** 0 Ω. REN is grounded only on the PET side. | 59k |
| 1.23 | Each signal contact J2-1…11 and A…D ↔ GND | A few kΩ, all roughly the same (the SN7516x terminations). Note any outlier. | 53 - 59k |
| 1.24 | Each J2 contact ↔ its neighbours (1–2, 2–3 … 10–11, A–B … D–E) | No beep | OK |
| 1.25 | Each J2 contact ↔ its U1/U2 bus pin (tables above, e.g. J2-11 ↔ U2 pin 8) | ≈ 0 Ω | OK |

---

## Stage 2: 5 V bench test (no Pi, no PET)

This powers only the 5 V side: U1, U2 and the bus-side terminations. The 3.3 V side stays unpowered, and that is safe because every translator and gate on the board isolates its pins when its supply is off. The point of this stage is to prove the board can't damage the Pi before you plug it in.

**Setup**
- Connect the supply's **+5 V to J1-2** and **GND to J1-6** (Dupont wires onto the header pins).
- Put the multimeter **in series with the +5 V lead** on its current range: start on the 10 A range, then move to 200 mA once you know the current is low.
- If the supply has a current limit, set it to about 300 mA. If it's a USB charger, be ready to pull the plug.
- **Never connect the bench supply while the board is on a Pi.**

| # | Measure | Expect | Got |
| --- | --- | --- | --- |
| 2.1 | Supply current at switch-on | Somewhere around 50–150 mA, mostly U1 and U2. **Over 250 mA, or rising, or a chip feels hot: switch off.** | 97mA |
| 2.2 | (meter back on volts) U1 pin 20 and U2 pin 20 | 4.75–5.25 V | 4.93V |
| 2.3 | J1-1 (3V3 net, unpowered) | **Below 0.5 V.** More than that means 5 V is leaking into the 3.3 V rail, and through it into the Pi. Stop. | 0.013V |
| 2.4 | Every J1 GPIO pin from list 1.8 | **All below 0.5 V.** Any pin above 1 V would leak into a Pi GPIO. Stop. | all below 0.02V |
| 2.5 | U2 pin 11 (DC) | ≈ 5 V | 4.96V |
| 2.6 | U2 pin 1 (TE) and U1 pin 1 (TE) | ≈ 0 V (listen direction, held by R3 and R1) | OK |
| 2.7 | J2 signal contacts 1–11, A–E | Each about 2.5–3.7 V, all similar (the bus terminations' idle voltage) | 2.94V |
| 2.8 | U1 pins 12–19, U2 pins 13, 14, 15, 18, 19 (receiver outputs) | High: about 3.5–5 V | 4.92V all |

### 2B Receive paths, using a jumper wire to ground

Keep one end of a jumper on GND (J2-12 or J1-6). Touch the other end to the J2 contact, and read the chip pin at the same time. The bus outputs are open-collector, so grounding a bus line is safe; it's exactly what a device on the bus does.

| # | Ground this J2 contact | Read here | Expect | Got |
| --- | --- | --- | --- | --- |
| 2.9 | 1, 2, 3, 4 | U1 pins 19, 18, 17, 16 | Goes low (< 0.5 V) | OK |
| 2.10 | A, B, C, D | U1 pins 15, 14, 13, 12 | Goes low | OK |
| 2.11 | 11 (ATN) | U2 pin 13 | Goes low | OK |
| 2.12 | E (REN) | U2 pin 19 | Goes low | OK |
| 2.13 | 9 (IFC) | U2 pin 18 | Goes low | OK |
| 2.14 | 6 (DAV) | U2 pin 15 | Goes low | OK |
| 2.15 | 5 (EOI) | U2 pin 14 | Goes low | OK |

### 2C Transmit paths that work in this mode (SRQ, NDAC, NRFD)

With TE low and DC high, U2 drives SRQ, NDAC and NRFD onto the bus. Ground the terminal-side pin and watch the J2 contact. The translator on that pin is unpowered and high-impedance, so this is safe too.

| # | Ground this U2 pin | Read J2 | Expect | Got |
| --- | --- | --- | --- | --- |
| 2.16 | Pin 12 (SRQ) | J2-10 | Goes low (< 0.5 V); other contacts stay high | |
| 2.17 | Pin 17 (NDAC) | J2-8 | Goes low | |
| 2.18 | Pin 16 (NRFD) | J2-7 | Goes low | |

The talker direction (DAV, EOI, DIO driven onto the bus) and every 3.3 V part are tested in stage 3 with the Pi.

Switch off and disconnect the bench supply.

---

## Stage 3: On the Pi 4 (not on the PET)

### 3A Prepare the Pi without the HAT

1. Raspberry Pi OS Bookworm 64-bit. Copy the `Software` folder to the Pi.
2. Apply `Software/boot/` settings (`config.txt.snippet` and the `cmdline.txt` changes), then reboot. The `gpio=` lines put every HAT pin in its safe state from the first moment of boot.
3. Build and test the software, then install it:
   ```bash
   cd Software && make && make test && sudo make install
   ```
4. Run `sudo hat_bench` and keep the output (benchmark deliverable). The poll-gap result must say **PASS**.

### 3B First power-up with the HAT

1. **Pi switched off and unplugged.** Fit the HAT. Check that all 40 pins are in the socket and not offset by one row or column.
2. Power the Pi from its normal supply. Watch it boot. If the red LED goes off or the Pi doesn't boot, pull the power at once.
3. Measure:

| # | Measure | Expect | Got |
| --- | --- | --- | --- |
| 3.1 | U2 pin 20 (5V_HAT) | 4.75–5.25 V | |
| 3.2 | J1-1 (3V3) | 3.25–3.35 V | |
| 3.3 | Touch U1, U2, U3 after 1 min | Warm at most, not hot | |

4. Run the tools in this order:

| # | Command | Expect | Got |
| --- | --- | --- | --- |
| 3.4 | `sudo hat_safe --check` | `safe` | |
| 3.5 | `sudo hat_selftest --auto` | All PASS (T0–T5: pulls, U3 direction, U20/U21 talk enables, U9/U5/U10/U11 direction, U22/U23 NDAC hold, SRQ) | |
| 3.6 | `sudo hat_selftest --probe` | Phase 1: touch J2-11, E, 9, 6, 5, 1–4, A–D to ground in turn. Phase 2: touch **only** J2-11. Phase 3: touch **only** J2-7 and J2-8. All PASS. | |
| 3.7 | `sudo hat_selftest --manual` | Meter on J2 contacts. Every step: the named contact < 0.5 V, the others ≈ 3 V. This is the only test of the talker drive (DAV, EOI, DIO1–8) onto the bus. | |
| 3.8 | HAT EEPROM (optional): add `dtparam=i2c_vc=on` to config.txt, reboot, `sudo apt install i2c-tools`, `sudo i2cdetect -y 0` | Device at `50` | |

Shut the Pi down (`sudo poweroff`) and unplug it before going to stage 4.

---

## Stage 4: Connecting to the PET

The J2 footprint was never checked against the real connector and the PET finger orientation (README_REV03B §6), so **the first PET contact is made with everything switched off.**

### 4A Orientation check (PET off, Pi off, both unplugged from the mains)

1. Fit the HAT's J2 onto the PET's IEEE-488 edge (the rear-panel finger row on the main board).
2. Measure with the meter:

| # | Between | Expect | Got |
| --- | --- | --- | --- |
| 4.1 | HAT GND (J1-6) ↔ PET chassis or a main-board screw | ≈ 0 Ω | |
| 4.2 | U2 pin 2 (REN bus) ↔ GND | **≈ 0 Ω**: the PET ties REN to ground. This proves the HAT is the right way round. | |
| 4.3 | U2 pin 8 (ATN bus) ↔ GND | **Not** 0 Ω (several hundred Ω to a few kΩ: the PET's terminations in parallel with the HAT's) | |
| 4.4 | U1 pin 2 (DIO1 bus) ↔ GND | Not 0 Ω | |

If 4.2 isn't 0 Ω, or 4.3/4.4 are 0 Ω, the connector is reversed or offset. **Do not power anything.** Send me the readings.

### 4B PET on, Pi off: the board must not load the bus

| # | Action | Expect | Got |
| --- | --- | --- | --- |
| 4.5 | Switch the PET on (Pi stays off) | PET boots normally to the `### COMMODORE BASIC ###` screen | |
| 4.6 | U2 pin 20 (5V_HAT) and J1-1 (3V3) | Both below ~0.5 V: the PET must not power the HAT through the bus | |
| 4.7 | On the PET type `LOAD"$",8` | `?DEVICE NOT PRESENT ERROR`; the PET stays usable. **This is the acceptance test of brief §8.** | |
| 4.8 | Repeat 4.7 with a real drive on the bus, if you have one | Works normally with the HAT unpowered | |

### 4C PET on, Pi on

Power up order: PET first, then the Pi. (Either order should be safe; this one is the one we just characterised.)

| # | Command / action | Expect | Got |
| --- | --- | --- | --- |
| 4.9 | `sudo hat_selftest --read-only` | REN asserted; ATN, IFC, DAV, EOI released | |
| 4.10 | `sudo hat_selftest --read-only --watch --rx`, then press the PET's reset or switch it off and on | IFC pulses as the PET resets | |
| 4.11 | `sudo hat_monitor --raw`, type `LOAD"$",8` on the PET | ATN, then `CMD $28 LISTEN 8`, then nothing (the PET times out: expected in passive mode) | |
| 4.12 | `sudo hat_monitor --ack`, type `LOAD"$",8` | LISTEN 8, OPEN, `$` with EOI, UNLISTEN, TALK 8, DATA ch 0, then a PET timeout error. The summary shows worst-case DAV-to-NDAC-release time. | |
| 4.13 | Switch the Pi off with the PET running, type `LOAD"$",8` | `?DEVICE NOT PRESENT ERROR`; the PET stays usable | |

When 4.12 passes, the hardware is proven in both directions on the real bus: listener in hardware and firmware, talker drive in 3.7. That's the end of hardware bring-up.

---

## Stage 5: Functional tests for the two operating modes (after the stage 2 and 3 software)

Neither mode's software exists yet. The tests below are the acceptance criteria it will be written against.

### Mode 1: disk drive, device 8

| # | On the PET | Expect |
| --- | --- | --- |
| 5.1 | `LOAD"$",8` then `LIST` | Directory listing of the emulated disk |
| 5.2 | `SAVE"TEST",8` with a short BASIC program in memory | `READY.`; the file appears on the Pi |
| 5.3 | `NEW`, `LOAD"TEST",8`, `LIST` | The same program |
| 5.4 | `VERIFY"TEST",8` | `OK` |
| 5.5 | `OPEN 15,8,15:INPUT#15,A,B$,C,D:PRINT A;B$` | `0 OK` (command channel) |
| 5.6 | `LOAD"MISSING",8` | `?FILE NOT FOUND ERROR` |
| 5.7 | `DLOAD"TEST"`, `DIRECTORY`, `SCRATCH` (BASIC 4 disk commands) | Work as on a 4040 |
| 5.8 | Sequential file: `OPEN 2,8,2,"DATA,S,W"`, `PRINT#2`, `CLOSE`, read it back | Data round-trips |

### Mode 2: programmable GPIB interface

The HAT is wired as a **talker/listener only** (DC is tied high). The PET is always the controller. So the Pi can send and receive data whenever the PET addresses it, and it can request attention with SRQ. It can't address other devices or take control of the bus. "Controlling the PET" therefore means the PET runs a BASIC program that listens to the Pi and acts on what it receives.

| # | On the PET | Expect |
| --- | --- | --- |
| 5.9 | `OPEN 1,9:PRINT#1,"HELLO":CLOSE 1` | The Pi program receives `HELLO` (device 9, so device 8 stays the disk) |
| 5.10 | `OPEN 1,9:INPUT#1,A$:PRINT A$` | Shows a string the Pi program sent |
| 5.11 | Binary transfer of all 256 byte values both ways, with EOI on the last byte | Byte-exact, EOI seen on the last byte only |
| 5.12 | Pi asserts SRQ | Visible to a PET program (BASIC 4 doesn't handle SRQ itself, so this needs a PEEK of the PIA or a small machine-code routine) |
| 5.13 | Long transfer (e.g. 32 KB) while the Pi is under load | No lost bytes, no PET timeouts |
