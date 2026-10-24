# REV03F hardware check against the driver brief

Date: 2026-10-16. Source: pad nets in `Hardware - REV03F/CBM4032_Pi4_Pi5_IEEE488_REV03.kicad_pcb`, extracted with a script. The change log says schematic and PCB have the same nets (75/75 multi-pin nets). Pin numbers come from the TI datasheets for the parts listed in the brief.

## Result

The GPIO map, polarities and direction rules in sections 4 and 5 of the brief match the netlist. The software uses them as written.

| Part | Checked |
| --- | --- |
| U3 (8T245) | A1–A8 = GPIO5, 12, 13, 19, 20, 21, 26, 17 through R34–R41. DIR (pin 2) = DATA\_TE. OE# (pin 22) = GPIO8 with R2 pull-up. B1–B8 go to U1 pins 19–12. |
| U1 (75160B) | TE = DATA\_TE. PE (pin 11) = GND, so the bus outputs are open collector. Bus pins 2–9 go to J2 1–4 and A–D. |
| U2 (75161B) | TE = CTRL\_TE. DC (pin 11) is pulled to 5V\_HAT by R11, so it is always in device mode. Bus and terminal pins match the J2 contacts in the brief. |
| U4, U7, U8 | DIR = GND (receive). A = GPIO27, 24, 25. |
| U6 | DIR = 3V3 (transmit). A = GPIO23. |
| U9, U5 | DIR = CTRL\_TE. A = GPIO4, GPIO22 through R32/R33 (330 Ω). |
| U10, U11 | DIR = NOT\_TE. A = GPIO18, GPIO15, with no series resistor. R6/R7 pull up to 3V3. |
| U20, U21 | CTRL\_TE = GPIO11 AND GPIO27. DATA\_TE = GPIO9 AND GPIO27. R28/R29 pull the requests down; R3/R1 pull the outputs down. |
| U18 | NOT\_TE = NOT CTRL\_TE. |
| U22, U23 | ATN\_HOLD = GPIO27 XOR GPIO16 (R30 pull-up). U23 is an open-drain output to the **bus** NDAC (J2-8). |
| U15 | 24C32 on ID\_SD/ID\_SC (J1 27/28), R8/R9 pull-ups. WP is pulled up by R10 and goes to TP1. |

## Points to raise

1. **ATN-fall sequence needs a step the brief doesn't list.** Section 5, "ATN falls", switches the talk lines to inputs and drives NDAC/NRFD, but never lowers GPIO11/GPIO9. If GPIO11 is still high when the PET releases ATN, U20 grants CTRL\_TE again at once. U10/U11 then drive GPIO18/15 while the Pi drives them low, and those two lines have no series resistor. U5/U9 would also start driving the bus from floating inputs. `hat_atn_takeover()` lowers GPIO11 and GPIO9 right after switching the inputs. This is a software ordering rule, not a hardware change. A unit test covers it: removing that line makes the contention checks fail. **Please confirm and add it to the brief.**
2. **Wording about pull-ups.** Section 4 says R12–R19 pull up the bus side of U2, and R20–R27 the bus side of U1. In the netlist they are on the **terminal** (5 V translator) side. For example, R12 is on ATN\_T (U2 pin 13) and R20 is on D1\_5V (U1 pin 19). The bus side only has the SN7516x internal terminations. Nothing changes for software: with DATA\_TE low, U1 receives, so the PET still sees DIO released.
3. **`connections.json` in the hardware folder is out of date.** It lists U12–U19 and a DC net driven by U16, which matches an earlier revision. Don't use it. The PCB file is the source.
4. **Talker interrupted by ATN.** When ATN falls, U3/U5/U9 reverse within gate delays while the Pi may still be driving DIO/DAV/EOI. Until software switches those pins to inputs, the fight is limited only by the 330 Ω resistors, about 10 mA per pin at 3.3 V (the brief says the same). With the busy-poll loop this should last well under a microsecond. Stage 1 can measure it: run `hat_bench` for the poll gap and look at the stage 2 logic-analyser traces.
5. **Same-level overlap is intended.** The brief's "become talker" sequence makes DAV/EOI/DIO outputs, driven high, before raising GPIO11/9. While that happens the translators also drive those pins high. The simulator records this as an *overlap* (same level, no current), not as contention.

## Assumptions the self-test makes

- With the HAT unplugged, the SN7516x bus terminations hold every bus line released, so ATN reads high and the talk enables can be granted. If ATN reads low with the HAT unplugged, `hat_selftest` refuses to run and reports it.
- REN reads asserted only when the HAT is on a PET (the PET ties REN to ground). The active tests use this as an interlock.
