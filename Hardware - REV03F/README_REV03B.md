# REV03B: what changed and what to do next

REV03B starts from REV03A and implements your two approved decisions: the hardware ATN response and DC tied permanently high. It also moves every SMD part to a JLCPCB part with confirmed stock. Open this folder, not the REV03 or REV03A folder.

## 1. Changes from REV03A

| Area | Change |
| --- | --- |
| DC strap | U2 pin 11 (DC) is now held high by R11 alone, so the board is always a talker/listener. U16, U13, U17, U19, U12 and U14 are removed. GPIO10 is spare (R4 pull-up) |
| Fixed directions | U4/U7/U8 (ATN, REN, IFC): DIR tied to GND, always receive. U6 (SRQ): DIR tied to 3V3, always transmit. U5 (EOI): DIR = CTRL_TE |
| Talker release on ATN | U20 (74LVC1G08): CTRL_TE = GPIO11 AND GPIO27. U21: DATA_TE = GPIO9 AND GPIO27. While the PET holds ATN low, both are forced low in hardware. R28/R29 10 kΩ pull-downs on the GPIO9/GPIO11 request nets |
| ATN acknowledge | U22 (74LVC1G86): ATN_HOLD = GPIO27 XOR GPIO16 (ATNA). U23 (74LVC1G06, open-drain) pulls the bus NDAC line low while ATN_HOLD = 1. R30 10 kΩ pull-up on ATNA |
| Series resistors | R32–R41 330 Ω between the Pi and the translator for GPIO4, 22, 5, 12, 13, 19, 20, 21, 26, 17 (the pins whose translator can reverse on its own) |
| Parts | All SMD parts moved to confirmed JLCPCB stock (table in §3) |

### NDAC hold truth table (GPIO27 = ATN as the Pi sees it: low = asserted)

| GPIO27 (ATN) | GPIO16 (ATNA) | Bus NDAC |
| --- | --- | --- |
| H (released) | H (idle, R30) | Follows GPIO18 |
| L (asserted) | H | Pulled low at once by U23 (hardware acknowledge) |
| L | L | Follows GPIO18 (firmware has taken over) |
| H | L | Held low until firmware sets ATNA high again |

**Firmware rule:** after handling each ATN edge, set GPIO16 equal to the ATN level you just saw. GPIO27 is always an input.

## 2. Checks run on REV03B

| Check | Result |
| --- | --- |
| Independent netlist | 95 parts, 360 pins, 82 nets. No unwired pins, no single-pin nets, no single-use labels, no dangling wire ends |
| Symbol ↔ footprint pad sets | 0 mismatches |
| Logic vs SN75161B function table | 16/16 combinations of (TE request, DATA_TE request, ATN, ATNA) match |
| KiCad ERC | Not runnable here. Run it yourself: §4 step 4 |

## 3. JLCPCB availability (all SMD parts)

Stock figures for Extended parts come from the jlcparts snapshot of JLCPCB's catalogue, collected 2026-09-22 to 2026-10-11. "Basic" means the part was on JLCPCB's Basic list on 2026-10-11 (CDFER/jlcpcb-parts-database scrape). Re-check everything with JLCPCB's BOM tool when you order (§4 step 9).

| Refs | Part | JLCPCB # | Type | JLC stock | Need |
| --- | --- | --- | --- | --- | --- |
| U3 | NCA8T245-DTSXR (SN74LVC8T245 equivalent) | C40086844 | Extended | 1,975 | 1 |
| U4–U11 | SN74LVC1T45DB(LX), lingxingic | C22469931 | Extended | 2,914 | 8 |
| U18 | 74LVC1G04GV, TECH PUBLIC | C19829589 | Extended | 2,737 | 1 |
| U20, U21 | 74LVC1G08GV, TECH PUBLIC | C19829591 | Extended | 12,581 | 2 |
| U22 | 74LVC1G86GV, TECH PUBLIC | C19829595 | Extended | 2,546 | 1 |
| U23 | SN74LVC1G06DB(LX), lingxingic | C22469967 | Extended | 2,649 | 1 |
| U15 | 24C32BN, XBLW | C18723546 | Extended | 21,170 | 1 |
| F1 | 1206L050/6NR, LUTE (0.5 A hold, 150–700 mΩ) | C7542942 | Extended | 36,979 | 1 |
| C1–C34 except C3, C4 | CC0805KRX7R9BB104 100 nF | C49678 | Basic | — | 32 |
| C4 | CL21A106KAYNNNE 10 µF | C15850 | Basic | — | 1 |
| C3 | CL31A226KAHNNNE 22 µF | C12891 | Basic | — | 1 |
| 25 × 10 kΩ / 2 × 4.7 kΩ / 2 × 3.9 kΩ / 1 kΩ / 10 × 330 Ω | UNI-ROYAL 0805 | C17414 / C17673 / C17614 / C17513 / C17630 | Basic | — | — |

**Caveats:**
- The second-source ICs (lingxingic, TECH PUBLIC, the NCA8T245) use the same part numbers and packages as the TI/Nexperia originals. I could not open their datasheets, because JLCPCB's datasheet server blocks automated download. Before ordering, open each part page on jlcpcb.com, look at the pin diagram, and confirm the pins match the table in §5.
- U1/U2 (SN75160BN/SN75161BN) and the connectors are hand-soldered and not bought from JLCPCB. Buy them from a distributor.
- I could not read JLCPCB stock for the TI/Nexperia originals (C7843, C27643 and similar). If the BOM tool shows them in stock, they are drop-in alternatives with no schematic change.

## 4. KiCad steps (KiCad 10)

1. **Unzip** `CBM4032_Pi4_Pi5_IEEE488_REV03B.zip` into a new folder. Keep your old folders untouched.
2. **Open the project:** start KiCad → File → Open Project → choose `CBM4032_Pi4_Pi5_IEEE488_REV03.kicad_pro` inside the REV03B folder. The file name still says REV03; the title block says 0.3B.
3. **Look at the schematic:** double-click the schematic in the project window, then open sheet "Direction control logic". The new ATN block (U20–U23, R28–R30) and the series resistors (R32–R41) are on the right-hand side. Press Home to fit the page.
4. **Run ERC:** Inspect → Electrical Rules Checker → Run ERC. Expect 0 errors. If anything appears, click Save… and send me the report file.
5. **Update the PCB from the schematic:** open the PCB Editor (the PCB icon in the project window), then Tools → Update PCB from Schematic… (F8).
    - Tick **Delete footprints with no symbols**. This removes U12, U13, U14, U16, U17 and U19.
    - Tick **Replace footprints with those specified in the schematic**.
    - Click Update PCB. The new parts appear bunched together, ready for placement.
6. **Refresh footprints from the corrected library:** Tools → Update Footprints from Library… → choose **All footprints**, leave the options at their defaults, click Update. This pulls in the fixed DIP20, J1, SOT23-5, SOT23-6 and TSSOP24 footprints. The board copy still holds the old ones until you do this.
7. **Place J1 (Pi header):** click J1, press E. Set Position X = 123.37 mm, Y = 80.77 mm, Orientation = 90°, then OK. Check by hovering: pad 2 should be at Y ≈ 78.23 mm (towards the board's top edge), and pad 39 at X ≈ 171.63 mm.
8. **Check the mounting holes:** H1 (118.5, 79.5), H2 (176.5, 79.5), H3 (118.5, 128.5), H4 (176.5, 128.5).
9. **Check JLCPCB stock (any time):** on jlcpcb.com go to Parts → BOM Tool. Upload `JLCPCB_BOM_REV03B_upload.csv`; it shows live stock for every line.
10. **Stop there:** don't place or route J2 until the PET connector measurements below are done.

## 5. Pinouts to confirm on the JLCPCB part pages

| Part | Pins that must match |
| --- | --- |
| SN74LVC1T45DB(LX), SOT-23-6 | 1 VCCA, 2 GND, 3 A, 4 B, 5 DIR, 6 VCCB |
| NCA8T245-DTSXR, TSSOP-24 | 1 VCCA, 2 DIR, 3–10 A1–A8, 11–13 GND, 14–21 B8–B1, 22 OE, 23–24 VCCB |
| 74LVC1G08GV / 74LVC1G86GV, SOT-23-5 | 1, 2 inputs, 3 GND, 4 Y, 5 VCC |
| 74LVC1G04GV / SN74LVC1G06DB(LX), SOT-23-5 | 1 NC, 2 A, 3 GND, 4 Y, 5 VCC (the 1G06 output must be open-drain) |
| 24C32BN (XBLW), SOP-8 | 1–3 A0–A2, 4 GND, 5 SDA, 6 SCL, 7 WP, 8 VCC |

## 6. Identifying the PET connector contacts (decision 3)

Yes: a multimeter tells you which contact is which, and that fixes the orientation. Do it in this order.

### Step A: resistance test, PET switched OFF and unplugged (safe, needs no power)

1. Set the meter to continuity (beep) or the 200 Ω range.
2. Put the black probe on PET ground: a metal screw holding the main board, or the metal frame.
3. Touch each of the 24 edge-connector fingers in turn with the red probe. Write down which ones beep (≈ 0 Ω).

What you should find:

| Row | Fingers that beep (≈ 0 Ω) | Fingers that don't |
| --- | --- | --- |
| Letter row (A–N) | 8 in a row at one end: E (REN, tied to ground on the PET), F, H, J, K, L, M, N | 4 at the other end: A–D (DIO5–8) |
| Number row (1–12) | Only the last finger: 12 (shield/ground) | 1–11 |

So the row with 8 grounded fingers is the letter row, and the row with a single grounded end finger is the number row. Pin 12 and pin N are at the grounded end. Pin 1 is at the opposite end, directly opposite pin A. Non-ground fingers read hundreds of ohms to several kΩ (the PET's bus terminators), not 0 Ω.

### Step B: optional voltage check, PET switched ON, nothing plugged in

Put the black probe on ground and measure each finger. The signal fingers (1–11, A–D) should sit high, roughly 3–5 V. E and F–N, and 12, should read 0 V. Hold the probe steady: fingers are 3.96 mm apart. Touching two at once won't harm IEEE-488 lines, but touch nothing else on the PET board.

### Step C: the connector itself, off the PET

With continuity mode, find which solder tab connects to which spring contact. Is the top row of tabs wired to the top row of contacts? Is tab 1 at the same end as contact 1? Then hold the connector on a PET finger row the way the HAT will hang, and note which HAT face (component side or underside) ends up facing the PET's number row.

### Step D: dimensions with calipers

| Measure | Why |
| --- | --- |
| Distance from tab 1 to tab 12 centres (expect 43.56 mm = 11 × 3.96) | Confirms pitch |
| Tab width and length | Pad size (currently 2.6 × 6.0 mm) |
| Gap between the upper and lower tab rows | The board thickness the tabs straddle (1.6 mm assumed) |
| Distance from the connector body to the tab tips | Where the board edge goes |

Send me the results of Steps A, C and D (a photo of your notes is fine) and I'll fix the J2 footprint and the board edge to match.
