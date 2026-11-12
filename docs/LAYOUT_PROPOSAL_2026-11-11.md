# IGNIS v5: preliminary placement and routing proposal

Branch `layout/proposal-2026-11-11`, from `main` at `d91e67e`. Prepared in the
unattended run that started 2026-11-10.

**This is a design document for the Avionics Lead to execute in Altium. It is
not a layout.** No Altium or KiCad file was created or edited. Nothing here is
DRC-checked: Altium is not available to this run, and DRC stays the source of
truth. The pyro and high-current section (section 8) is flagged for Emilio's
sign-off and is not approved by this document.

Evidence labels: `VERIFIED` (read in a cited file or datasheet page, or parsed
from a repo file), `INFERRED` (reasoned, shown), `UNVERIFIED` (needs a
measurement, a missing input, or Emilio's decision). Every estimate is marked.

## 0. Summary

- **Do this first:** re-sync the PCB from the schematic. `PCB_IGNIS-v5.PcbDoc`
  has no routing, a 2-layer stack, 87 of 107 parts, and Storage nets older than
  `Main.NET` (section 1). So this proposal is "from the netlist", not an edit
  of an existing layout.
- **Placement:** with the ESP32 antenna on one flat (called N here), put USB-C
  on W, battery and regulators on SW, the e-match terminal and pyro circuit on
  S, the SMA and RFM95W on SE, the microSD on E, the debug connector on NE, and
  the GNSS module in the centre. In the counting model this gives **43
  inter-zone crossings**, against 86 for the first arrangement tried (RFM95W on
  SW, power on SE). The gain comes from placement, not from layers.
- **RF layer conflict (needs your decision):** I recommend the feed on **L1**
  with a local GND fill on L2 under the RF zone, because the RFM95W and SMA are
  both on top and that needs no signal via. It requires an exception to "L2 =
  power" in that zone. The L4 option from the rule file also works (section 5).
- **Pyro path:** sized for an **assumed 6 A** (`UNVERIFIED`): 4.0 mm copper on
  an outer layer, Direct Connect, 8 vias per MOSFET source. Width scales as
  I^1.38 if the real current differs (section 6).
- **Inputs that are missing from the repo:** `design-rules.md`,
  `jlcpcb-capabilities.md`, `IGNIS_v4_Review_and_V5_Change_List.md`. Their
  content was taken from the quotations in the prompt only.
- **Not pushed.** See Open Question Q-01.

---

## 1. What I could and could not read

### 1.1 Read

| Input | Result |
|---|---|
| `PCB_Project_IGNIS-v5/Main.NET` | Read in full. 107 components, 75 nets, 344 pins. `VERIFIED` |
| `PCB_Project_IGNIS-v5/PCB_IGNIS-v5.PcbDoc` | **Readable.** It is an OLE compound file; a small read-only parser (kept outside the repo) decoded the `Board6`, `Components6`, `Pads6`, `Nets6`, `Tracks6` streams. Results below. `VERIFIED` |
| Four `.PcbLib` footprints (ESP32, SD socket, AMS1117, NCE6005AR) | Pad designators and pad sizes read the same way. `VERIFIED` |
| `Components/COMPONENT_SPECS.md`, datasheets for ESP32-S3-WROOM-1, SAM-M10Q, DPS368, LSM6DSV32X, RFM95W | Read as text. `VERIFIED` where cited |
| `ignisv5.pdf` | Text layer only (519 lines). No sheet viewed as an image |
| The audit | `docs/AUDIT_2026-11-10.md` on branch `fix/sd-diagnostics` (local, commits `a4b3e40`, `6921218`), written earlier in this same run. The prompt named `docs/AUDIT_2026-11-11.md` on `audit/overnight-2026-11-11`; that branch and file do not exist |

### 1.2 What the PcbDoc contains (all `VERIFIED` by parsing)

| Item | Finding |
|---|---|
| Outline | Regular octagon, **66.52 mm across flats, 72.0 mm across corners**, flat sides 27.55 mm, area about 3,665 mm². It fits a Ø75 mm circle with 1.5 mm to spare per corner. The prompt describes "roughly 70 × 55 mm with the corners cut"; the file is a 66.5 mm regular octagon. The area agrees |
| Layer stack | **2 copper layers**: Top 1.4 mil, FR-4 12.6 mil (εr 4.8), Bottom 1.4 mil. No mid layers, no planes. The 4-layer stack has not been set up |
| Components | 87 on the board, all on Top. **Missing (20):** MCU1, CARD1, U1, U2, U5, U6, U9, U10, D1, D2, D3, D5, D6, D7, D8, J2, J4, Q, S1, S2, R13. **Extra:** R22, which is not in `Main.NET` |
| Placement | Not a placement. Parts sit in rows by type (for example every C on Y = 825, 2400, 3205 mil) out to X = 8235 mil, far outside the 2619 mil outline. That is what an ECO import leaves before anything is moved |
| Routing | None. 0 tracks carry a net (the 1,310 track records are footprint silkscreen and mechanical-layer lines; 8 are on the top copper layer and have no net), and the `Vias6` and `Polygons6` streams are empty |
| Nets | 74 nets. Storage is stale against `Main.NET`: on the PCB, R17 runs `SD_DETECT` to `GND`, R21 is on `SNS_SPI_SCK`, R18 on `DAT1`, R19 on `SNS_SPI_MISO`, R20 on `SNS_SPI_MOSI`, R22 on `DAT2`. In `Main.NET` R17 is the `SD_CS` pull-up and R21 the `SD_DETECT` pull-up. Other differences are only `-` vs `_` in auto-generated net names |
| Through-hole parts | J3 (XT30) and J5 (terminal) pads are multi-layer. Q2/Q5 have pads 1 to 4 with 2 and 4 on the drain net |
| Rules | The `Rules6` stream was only partly decoded; what was read looks like Altium defaults (10 mil clearance and width). Treat the rule set as not configured |
| DRC | No report. Cached violation streams exist in the file and were not decoded |

`INFERRED`: the 20 missing parts are probably footprints that were not found at
the last ECO, because commit `d91e67e` renamed several library packages. The
last PCB ECO log is dated 16 October; the Storage schematic changed on 10
November.

**What Emilio must check against the current board:** nothing about placement
can be carried over, because nothing is placed. After Update PCB, confirm 107
components, 75 nets, R22 gone, R13 present, and the Storage nets as in
`Main.NET`.

### 1.3 Could not read

- `design-rules.md`, `jlcpcb-capabilities.md`,
  `IGNIS_v4_Review_and_V5_Change_List.md`: **not in the repo** (searched by
  name in the working tree and in the file list of every commit on every local
  and remote-tracking branch). Every rule and plan
  item below that cites them is taken from the prompt's quotation, so it is
  `UNVERIFIED` against the files themselves, and the fab limits could not be
  "confirmed against `jlcpcb-capabilities.md`" as asked.
- Datasheets for the connectors, MOSFETs, optos, regulators, buzzer and SD
  socket drawing: not in the repo or not renderable here. **Every body size
  below is an estimate** unless a pad extent from a `.PcbLib` is cited.
- The seven `.SchDoc` files were not parsed; the netlist was used instead.

## 2. Net inventory and classification

75 nets in `Main.NET` (`VERIFIED`). Ten have one node or none and need no
routing.

| Class | Count | Nets |
|---|---|---|
| **High-current / power** | 7 | `GND` (102 pins), `VCC_3V3` (53), `VBAT` (11), `VCC_5V` (9), `VBUS` (3), `EMATCH_CH1_B` (4), `EMATCH_CH2_N` (4) |
| **RF** | 1 | `NetJ4-1` (U4-9 to J4-1) |
| **Sensitive analog** | 3 | `VBAT_MON`, `PYRO_CH1_CONT`, `PYRO_CH2_CONT` |
| **High-speed digital** | 11 | USB: `USB2_P`, `USB2_N`, `DP`, `DN`. SPI: `SNS_SPI_SCK`, `SNS_SPI_MOSI`, `SNS_SPI_MISO`. LED chain: `WS2812B_DATA`, `NetLED1-4`, `NetLED1-2`, `NetLED2-2` |
| **Low-speed, pyro control and drive** (stay inside the pyro zone except the two gate nets) | 18 | `PYRO_CH1_GATE`, `PYRO_CH2_GATE`, `NetR34-1`, `NetR27-1`, `NetD8_A`, `NetD5_A`, `NetQ4-1`, `NetQ4-2`, `NetQ5-1`, `NetQ1-1`, `NetQ1-2`, `NetQ2-1`, `NetD8_C`, `NetD5_C`, `NetD7_A`, `NetD7_C`, `NetD6_A`, `NetD6_C` |
| **Low-speed, other** | 25 | I2C: `SNS_I2C_SDA`, `SNS_I2C_SCL`. GNSS: `GPS_UART_TX`, `GPS_UART_RX`, `NetR10-1`, `NetR11-1`, `GPS_PPS`, `NetD3_C`. Radio control: `RFM95W_CS`, `RFM95W_INT`, `RFM95W_RESET`. SD: `SD_CS`, `SD_DETECT`, `DAT1`, `DAT2`. Debug: `TXD0`, `RXD0`. Misc: `RESET`, `BOOT`, `BUZZER`, `NetD4_A`, `NetD1_C`, `NetR14-2`, `NetJ1_A5`, `NetJ1_B5` |
| **Not routed** (0 or 1 node) | 10 | `SDO`, `RESET_GPS`, `PPS_GPS`, `NetU3-19`, `NetU3-18`, `MPU6050_INT`, `INT_GPS`, `IMU_INT2`, `IMU_INT1`, `GPS_SAFEBOOT_N` |

Notes. USB is full-speed (12 Mbit/s): the module datasheet says "full-speed USB
2.0 OTG" (`VERIFIED`, p.1 and section 5.2.1.7), so the 90 Ω pair is good
practice, not a tight requirement, over the 20 to 25 mm involved (estimate).
The SPI bus runs at 4 MHz (`SD_SPI_HZ`, `LORA_SPI_HZ`); its edges matter more
than its clock rate. `PYRO_CHx_CONT` are opto outputs into 1 kΩ: robust, but
classed sensitive because a false reading changes arming state.

**Drift to record:** the prompt says the change list calls the battery
connector J5 and the terminal P1. `Main.NET` has J3 = XT30PW-M, J5 = Phoenix
1727036 (`VERIFIED`). The netlist is used throughout.

**Changes from the audit that affect part count or placement**, if you adopt
them (none is assumed in the crossing count; area is reserved for them):

| Audit ID | Change | Layout effect |
|---|---|---|
| PCB-01 | Re-sync the PCB | Prerequisite |
| PY-01 | Pull-down at each MOSFET gate | +2 resistors in the pyro zone, at the gates. **Sign-off** |
| AR-01, PY-05 | Independent arm element; fuse or current limit; diode plus bulk capacitor ahead of U1 | Reserve about 15 × 10 mm (estimate) between J3 and J5. **Sign-off** |
| PW-03 | AMS1117 symbol pin 4 (tab) | Needed before the tab can carry thermal copper |
| MC-02 | ESP32 thermal sub-pads `41_10` to `41_21` get the GND net | Needed before the via-in-pad rule can pass DRC |
| RS-02 | Pull-up on `RFM95W_CS` | +1 resistor at U4 |
| ST-06, ST-08 | Series resistors on `SNS_SPI_SCK` and `SD_DETECT` | +2 resistors, at MCU pins 20 and 35 |
| GP-01 | Buffer for the PPS LED | +1 SOT-23, +1 or 2 resistors at U3 |
| US-01 | USB ESD device | +1 part at J1, between the connector and R5/R7 |

## 3. Crossing count

### 3.1 Method

`Main.NET` gives connectivity but no positions, and the PcbDoc has no
placement, so crossings were **computed on a model**, not read from a board:

1. Each functional group is one point (its zone centroid). **MCU1 is modeled
   pin by pin**, using the module's pin order (14 pins per long side, 12 on the
   bottom, 1.27 mm pitch, module 18 × 25.5 mm, pin table from the module
   datasheet), because the MCU is one end of almost every inter-zone net and
   its pin order is what creates or removes crossings.
2. Only **inter-zone** nets are counted: 31 nets, 40 ratsnest edges. `GND` and
   `VCC_3V3` are excluded (planes). Nets that live inside one zone (most of
   the 18 pyro-drive nets, pull-ups, LED chains) are not counted.
3. Each net is a minimum spanning tree over its points; edges are straight
   lines in plan view.
4. A crossing is a proper intersection of two edges of different nets. Edges
   that share an end point do not count.

So the number is **a count on a stated model, and a lower-bound style
estimate of real routing conflicts**: it ignores congestion inside zones, pad
escape, and the two sides of the board. It is good for comparing placements;
it is not a prediction of via count. The model points (mm, origin at the SW
corner of the bounding square) are in the appendix. They are counting inputs,
**not footprint coordinates**.

### 3.2 Result

| Arrangement (ESP32 antenna on N in all) | Crossings | Of which signal-only | Ratsnest length (model) |
|---|---|---|---|
| A: RFM95W + SMA on SW, pyro on S, power on SE, SD on E, USB on W | **86** | 60 | 857 mm |
| B: as A, but pyro terminal on W and USB on S | 73 | 50 | 861 mm |
| C: as A, but SD on SW and RFM95W + SMA on E | 112 | 88 | 893 mm |
| **D (proposed): power on SW, pyro on S, RFM95W + SMA on SE, SD on E, USB on W** | **43** | 41 | 783 mm |
| D with the GNSS module shifted 4 mm west, or the sensors moved south-east of the module | 40 | 38 | about 780 mm |
| D with the buzzer east of the GNSS module instead of west | 46 to 48 | 35 | about 790 mm |

"Signal-only" excludes any crossing that involves `VBAT`, `VCC_5V`, `VBUS` or
an e-match net, since those are wide copper that signals pass under on another
layer anyway.

### 3.3 Which nets dominate (arrangement D, 43 crossings)

| Net | Crossings | Why |
|---|---|---|
| `PYRO_CH1_CONT`, `PYRO_CH2_CONT` | 10, 7 | They leave the module on its east side (pins 39, 38) and must reach the south, across SPI, I2C, SD and the radio control lines |
| `PYRO_CH1_GATE`, `PYRO_CH2_GATE` | 7, 7 | They leave on the west side (pins 4, 5), near the antenna end, and cross USB, the GNSS UART and the radio lines on the way south |
| `BUZZER` | 8 | Pin 22 is on the module's bottom row; the buzzer is on the west |
| `RFM95W_RESET`, `RFM95W_INT` | 7, 5 | Pins 12 and 7 are on the west side; the radio is on the south-east |
| `GPS_UART_TX/RX` | 5, 5 | Pins 10, 11 on the west side to the centre |
| SPI (3 nets) | 2 each | Short and uncrossed apart from the pyro continuity lines |

### 3.4 What the count says

- **Placement, not layers.** Moving power to the south-west (next to USB) and
  the radio to the south-east (next to the SD socket) halves the count, 86 to
  43, because it puts both SPI devices on the side where the SPI and SD pins
  leave the module, and puts both 5 V sources together. No layer change does
  that.
- **4 layers are still justified.** About 40 crossings on a 2-layer board is
  far over the skill's threshold of about 5; with two signal layers and a solid
  GND on L3, each crossing is one layer change for a slow net.
- **29 of the 43 crossings involve at least one of the four pyro control nets.** They are
  slow, so route them on L4 under the L3 plane, around the outside of the
  module, and accept the vias. No change to the pyro pin assignment is proposed
  (pyro circuits are out of scope here).
- **`RFM95W_INT` and `RFM95W_RESET` are in 12 crossings** (8 of them with
  pyro control nets). Moving those two signals to free east-side pins would
  remove up to 12, but that is a schematic and `pins_v5.h` change: Open
  Question Q-07, not assumed.

## 4. Placement proposal

Frame of reference: top view, the flat that carries the ESP32 antenna is **N**.
The eight sides are N, NE, E, SE, S, SW, W, NW; flats and diagonals are all
27.55 mm long (regular octagon, `VERIFIED`). Which side faces what in the
rocket is OPEN (Q-03 to Q-06). Sizes marked (est.) are estimates.

```
                 N  ESP32 antenna edge
            ___________________
        NW /   LEDs   | ANT |   \ NE
          /  S1       |-----|  J2 \
         /   buzzer   | ESP |  S2  \
      W |  USB-C      | 32  | IMU   | E
        |  ESD,R5/R7  |_____| (baro  |  microSD
        |  D2            GNSS   under)|  CARD1
         \  U2  U1     (centre)  U4  /
          \ J3 XT30   pyro (bot) SMA/ SE
        SW \_______ J5 terminal ___/
                      S
```

### 4.1 Zone by zone

**N: ESP32-S3-WROOM-1 (MCU1), top.** Antenna section at the board edge,
pointing out. The module is 18 mm wide on a 27.55 mm flat, so about 4.8 mm of
edge is left each side. The datasheet gives the antenna area as 6 mm deep
(`VERIFIED`, module dimension drawing); it sends you to Espressif's Hardware
Design Guidelines for the base-board keepout, which is not in the repo, so the
keepout extent is `UNVERIFIED`. Proposal: no copper on any layer under the 6 mm
antenna section and none in the strip beside it out to the corners of the flat;
let the antenna section sit at or beyond the board edge if the bay allows
(Q-04). The thermal pad needs MC-02 fixed first, then the rule's via-in-pad
array to L3.

**NW: status LEDs (LED1 to LED3, R23) and reset (S1, R1, C2), top.**
`WS2812B_DATA` is module pin 6 and EN is pin 3, both on the west side near the
antenna end, so both nets are short. Keep the LED row outside the antenna
keepout strip. C2 (1 µF) and R1 next to the EN pin, per the EN rule.

**West of the module: buzzer (LS1, Q, D4, R24), top.** It keeps the buzzer away
from the IMU and barometer (vibration and pressure pulses) at the cost of 8
modeled crossings for `BUZZER`. The alternative east of the GNSS module trades
those for crossings with 5 V and puts it beside the sensors: not recommended.

**W: USB-C (J1), top, at the edge.** USB D+ and D− are module pins 14 and 13,
the last two on the west side, so the pair runs about 20 to 25 mm (estimate) on
L1 with no layer change. Order along the pair: J1, ESD device if adopted, then
R5/R7 22 Ω near the module. CC resistors R3/R4 (5.1 kΩ, `VERIFIED`) at the
connector. D2 (SS24) between J1 and U2 so `VBUS` and `VCC_5V` stay in the
west and south-west.

**SW: battery (J3 XT30), U1, U2, VBAT divider, top.** Reasons: it is next to
USB, so both 5 V sources meet in one place; `VBAT_MON` is module pin 15, the
first pin of the bottom row at the west end, so the sense trace is short; and
it is next to the pyro terminal, so the firing loop is compact (section 8). U1
and U2 need copper (section 4.3). Keep the divider (R8, R9, C13) near the
module pin, with the `VBAT` end fed by a thin trace: the node is 32 kΩ and
picks up noise if the long part is on the pin side.

**S: e-match terminal (J5) on top at the edge, pyro circuit on the bottom
behind it.** Section 8.

**SE: SMA (J4) at the edge, RFM95W (U4) directly inboard, top.** U4's antenna
pin (pin 9) faces the connector; feed length under about 15 mm (estimate).
The SPI bus reaches U4 from the module's bottom row (pins 19, 20, 21) and
continues to the SD socket on the next side, so the bus is one short run with
two drops. `RFM95W_CS` is pin 18 (bottom row). `RFM95W_INT` and
`RFM95W_RESET` come from the west side and cross the centre: route them on L4.

**E: microSD (CARD1), top, at the edge.** `SD_CS` and `SD_DETECT` are module
pins 34 and 35 on the east side; SPI arrives from the bottom row. The socket's
pad field is about 15.5 mm wide and 10.6 mm deep (`VERIFIED` from the
footprint pads), which fits a 27.55 mm flat with the card ejecting outward.
C25 (22 µF) and C24 (100 nF) within about 3 mm of pad 4 (VDD), on the same
layer, with their GND vias beside the socket's GND pads. Pull-ups R13, R17 to
R21 at the socket. A series resistor on `SNS_SPI_SCK`, if adopted, goes at the
module pin, not at the socket.

**NE: debug connector (J2), boot button (S2), top.** `TXD0`/`RXD0` are pins 37
and 36, `BOOT` is pin 27, all on the east side. J2 is a 4-pin JST-SH; it can
sit on the NE diagonal or just inboard.

**East of the module: IMU (U5) on top, barometer (U6) on the bottom.**
`SNS_I2C_SCL` is pin 31 (east side), `SNS_I2C_SDA` is pin 23 (bottom row, east
end). This is the corner farthest from the regulators (heat), the pyro
terminal (combustion products) and the buzzer. The IMU wants to be near the
board's mounting stiffness and close to the rocket axis; with the mounting
pattern OPEN (Q-03) this is the best available guess. **U6's vent:** nothing
over it, no via under it, no conformal coating, keep flux away, and do not put
it under a standoff or against the bay wall; the DPS368 is MSL 1 and reflows at
260 °C (`VERIFIED`, `COMPONENT_SPECS.md`), so the second reflow is within its
rating.

**Centre: SAM-M10Q (U3), top, patch facing the nose.** The datasheet says the
optimal pattern is achieved on a 50 × 50 mm ground plane (`VERIFIED`, section
on the antenna, notes to the performance table). The board is 66.5 mm across,
so the centre is the only position that gives the patch roughly even ground
all round. It needs an unbroken L3 GND under and around it, and its GND pads
stitched. It sits about 20 mm (model distance) from U4 and about 25 to 30 mm
from both other antennas; whether LoRa transmit at +17 dBm desensitises it at
that distance is `UNVERIFIED` (the module has a SAW filter and LNA,
`VERIFIED`). The PPS LED (D3, R12) stays next to U3; see audit GP-01 before
copying it as drawn.

### 4.2 Antennas against the outline and the coupler (OPEN)

Three antennas share a 66.5 mm board: 2.4 GHz PCB antenna on N, 915 MHz
external antenna from the SE connector, GNSS patch in the centre facing along
the board normal. That geometry assumes the board sits **across** the rocket
(board normal along the axis) with the top toward the nose. If the board is
mounted along the axis instead, the patch faces the airframe wall and loses
its sky view; then the GNSS module is in the wrong place whatever the layout
(Q-06). If the aluminium coupler surrounds the board's plane, the PCB antenna
and the patch's low-elevation view are blocked regardless of where they sit on
the board; only the SMA antenna can be led outside it (Q-04).

### 4.3 U1 thermal copper (estimate)

All board current passes through U1 (`VERIFIED`, `Main.NET`: `VBAT` → U1 →
`VCC_5V` → U2 → `VCC_3V3`). Dissipation = (VBAT − 5.0 V) × I; at 8.4 V that is
3.4 W per amp.

| Load through U1 | Basis | U1 power | Rise at 90 °C/W | Rise at 55 °C/W | Rise at 45 °C/W |
|---|---|---|---|---|---|
| 0.15 A | idle, estimate | 0.51 W | 46 °C | 28 °C | 23 °C |
| 0.25 A | Wi-Fi AP up, average, estimate | 0.85 W | 77 °C | 47 °C | 38 °C |
| 0.35 A | flight with LoRa bursts, estimate | 1.19 W | 107 °C | 65 °C | 54 °C |
| 0.62 A | simultaneous peak (audit PW-02) | 2.11 W | 190 °C | 116 °C | 95 °C |

**θJA is an assumption.** No AMS1117 datasheet is in the repo. 90 °C/W is the
figure commonly quoted for a SOT-223 with almost no copper; 45 to 55 °C/W is
what a tab soldered to a few square centimetres of copper with vias to an
inner plane is commonly quoted to reach. All three columns are estimates and
none is a measurement.

Proposal: after PW-03 is fixed, pour the tab's net (the 5 V output for U1,
3.3 V for U2) on L1 around each part, as large as the SW zone allows (target at
least 300 mm² each, estimate), and stitch it to a matching pour on L4 with a
3 × 3 array of 0.3 mm vias beside or in the tab pad. Conduction through one
0.3 mm via with 20 µm plating over 1.6 mm is about 207 K/W (computed:
L / (k·A), k = 385 W/m·K, A = 0.0201 mm²), so nine in parallel are about 23 K/W
before spreading resistance. Keep U1 and U2 apart from each other and from U6.

Even at 45 °C/W the simultaneous-peak case is not sustainable in a hot bay
(the ESP32 module is rated to 65 °C ambient, `VERIFIED`). Copper helps the
average case; it does not make U1 the right part for the peak. That is a
schematic question (Q-09).

## 5. RF layer: L1 or L4. Flagged for Emilio's decision

The two sources disagree and neither is in the repo; both are quoted from the
prompt. `design-rules.md`: L1 signal, L2 power, L3 GND, L4 signal + antenna +
pyro, with L3 as the microstrip reference. V5 layout plan: RFM95W and SMA on
Top (L1). The module and the connector are top-side parts in both.

| | **Option 1: feed on L4** (as `design-rules.md`) | **Option 2: feed on L1** (as the V5 plan) |
|---|---|---|
| Feed path | U4 pin 9 (top) → via → L4 trace → SMA signal pin soldered on the bottom | U4 pin 9 → L1 trace → SMA signal pad on top |
| Signal vias | 1 (2 if the SMA signal pin stays on top) | 0 |
| Reference plane | L3 GND, adjacent, already solid | L2, which the rule file assigns to **power**. Unless L2 is GND under the feed, the return current has to cross from the 3.3 V plane to ground through capacitors |
| What it needs | Two or more GND vias tight to the signal via; the SMA mounted signal-side down; the RF keepout on L4 kept clear of the pyro pours in the next zone | A GND fill on L2 under the whole RF zone (a cut-out in the power plane), tied to L3 by the via fence. That is an exception to "L2 = power" |
| Via fence | Both sides of the L4 trace, L4 to L3 (through vias, so L1 to L4) | Both sides of the L1 trace, tying L1 GND, the L2 island and L3 |
| Keepout | Region on L4 only, no vias inside | Region on L1 only, no vias inside; the L2 island must be solid under it |
| Trace width for 50 Ω | Set by the L4-to-L3 dielectric | Set by the L1-to-L2 dielectric (the same in a symmetric stack) |
| Risk | The via is a small discontinuity at 915 MHz. The feed shares the bottom side with the pyro copper | Forgetting the L2 island leaves the feed referenced to a split or a power plane |

Width: about **0.41 mm** for 50 Ω over 0.21 mm of εr 4.4 prepreg. That is an
estimate from a closed-form microstrip formula with zero copper thickness and
an **assumed** JLCPCB 4-layer stack; the stackup file is not in the repo. Use
the fab's impedance calculator for the stack you order and treat its number as
the value. Either way the feed is under about 15 mm (estimate), which is less
than a tenth of the guided wavelength at 915 MHz (about 180 mm, estimate), so
a few ohms of error matters little (`INFERRED`).

**Recommendation: Option 2, with the L2 GND island.** Both RF parts are
already on top, the feed has no via, and the reference is the adjacent layer.
Its cost is one documented exception to the L2 allocation, in one zone. If you
do not want an exception to `design-rules.md`, Option 1 is sound at this
frequency. I have not picked silently: the rest of this document works with
either, and the routing checklist marks the step where the choice applies.

A third thing to weigh, outside this conflict: with L2 = power, every L1
signal (USB, SPI, the UARTs) references the 3.3 V plane, not ground. Swapping
the planes (L2 GND, L3 power) would give L1 a ground reference everywhere and
make Option 2 need no island. That changes the rule file's stackup, so it is
Open Question Q-08, not a recommendation.

## 6. Trace widths and vias

### 6.1 Inputs

IPC-2221: I = k · ΔT^0.44 · A^0.725, A in mil², k = 0.048 for outer layers and
0.024 for inner layers. Copper: **1 oz (1.378 mil) outer, 0.5 oz inner,
assumed** (the usual JLCPCB 4-layer default; `jlcpcb-capabilities.md` is not
in the repo to confirm). IPC-2221 is a steady-state chart; applying it to a
0.5 s pulse is conservative. Ambient and the rise are for the copper only, not
for parts.

### 6.2 Width table

| Net class | Nets | Current used | Basis | ΔT assumed | IPC-2221 minimum (outer 1 oz) | Proposed |
|---|---|---|---|---|---|---|
| Pyro firing path | `VBAT` J3 to J5, `EMATCH_CH1_B`, `EMATCH_CH2_N`, MOSFET source to GND | **6 A, assumed** | `UNVERIFIED`. 8.4 V across an assumed 1.4 Ω loop (about 1 Ω e-match plus 0.4 Ω of battery, wiring and MOSFET). No e-match or battery data is in the repo | 10 °C | 3.56 mm | **4.0 mm pour or track, outer layer, Direct Connect** |
| | | | | 20 °C | 2.33 mm | |
| Battery feed to U1 | `VBAT` branch to U1-3 | 1 A | above the 0.62 A peak | 10 °C | 0.30 mm | 1.0 mm |
| 5 V | `VCC_5V`, `VBUS` | 1 A | U1 output, USB limit 0.5 A | 10 °C | 0.30 mm | 1.0 mm, or a pour in the SW zone |
| 3.3 V | `VCC_3V3` | 1 A total | plane on L2 | n/a | plane | Plane; branches on outer layers 0.4 to 0.5 mm; module supply pin 0.8 mm |
| Sensitive analog | `VBAT_MON`, `PYRO_CHx_CONT` | under 10 mA | netlist | n/a | fab minimum | 0.2 mm, away from the SPI clock and the buzzer drive |
| RF | `NetJ4-1` | n/a | 50 Ω | n/a | n/a | about 0.41 mm (estimate; use the fab calculator) |
| USB | `DP`/`DN`, `USB2_P`/`USB2_N` | n/a | 90 Ω differential | n/a | n/a | Geometry from the fab calculator; not computed here. Keep the pair coupled and over one unbroken plane |
| SPI, LED data, other signals | all remaining | under 50 mA | | n/a | fab minimum 4 mil | 0.15 to 0.2 mm (6 to 8 mil); 4 mil only where an escape needs it |

**If the firing current is not 6 A:** the required area, and so the width at a
fixed copper weight, scales as I^(1/0.725) = I^1.38.

| Firing current | Minimum width, outer 1 oz, ΔT 10 °C | ΔT 20 °C |
|---|---|---|
| 3 A | 1.37 mm | 0.90 mm |
| 6 A | 3.56 mm | 2.33 mm |
| 8 A | 5.29 mm | 3.47 mm |
| 12 A | 9.25 mm | 6.07 mm |

On an inner 0.5 oz layer the same 6 A needs 18.5 mm at ΔT 10 °C, so **do not
route firing current on L2 or L3 as a trace**; the L3 plane as the return is
fine because it is a plane. A match that fuses short draws battery
short-circuit current, which no practical width is sized for: that is a fuse
question (audit PY-05), not a width question.

Voltage drop for scale: 1 oz copper is 0.49 mΩ per square, so 30 mm of 3 mm
track is 4.9 mΩ: 29 mV and 0.18 W at 6 A (computed).

### 6.3 Vias

Barrel area = π · (hole + plating) · plating, with **20 µm plating assumed**
(`UNVERIFIED`, fab file missing); rating from IPC-2221 with the outer-layer k.

| Hole | Barrel area | Rating at ΔT 10 °C | at ΔT 20 °C | Resistance (1.6 mm) |
|---|---|---|---|---|
| 0.2 mm (8 mil) | 0.0138 mm² | 1.22 A | 1.65 A | 2.0 mΩ |
| 0.3 mm | 0.0201 mm² | 1.60 A | 2.17 A | 1.4 mΩ |
| 0.4 mm | 0.0264 mm² | 1.95 A | 2.64 A | 1.0 mΩ |
| 0.5 mm | 0.0327 mm² | 2.28 A | 3.09 A | 0.8 mΩ |

| Path | Method | Proposed |
|---|---|---|
| MOSFET source to L3 GND (each of Q2, Q5) | 6 A / 1.60 A per 0.3 mm via = 3.75, so 4 minimum; doubled for margin and current sharing | **8 vias, 0.3 mm**, in or against the source pad |
| `VBAT` or e-match copper changing layer, if it ever must | same | 8 vias, 0.3 mm. Preferably none: see section 8 |
| U1 and U2 tab to the L4 pour | thermal: 207 K/W per via (section 4.3) | 3 × 3 array of 0.3 mm each |
| ESP32 thermal pad | rule: 8 mil hole, 16 mil pad, tented both sides | As the rule says. An 8 mil / 16 mil via is at the small end of what a standard process offers; confirm it against the fab file (Q-10) |
| GND stitching | rule: 200 to 300 mil general, 100 to 150 mil fence at the RF perimeter | As the rule says |

## 7. Routing order and Altium DRC rules

### 7.1 Before routing

1. Fix the library items that block DRC: AMS1117 symbol pin 4 (PW-03) and the
   ESP32 thermal sub-pad names (MC-02).
2. Decide the audit's schematic changes (section 2 table) so the netlist is
   final before placement. Pyro items need your sign-off.
3. Update PCB from the schematic. Check 107 components, R22 gone, R13 present,
   Storage nets as in `Main.NET`.
4. Set the layer stack to 4 layers with the fab's real thicknesses. Resolve
   the L1/L4 question (section 5) and, if you take it up, Q-08.
5. Enter the rules in 7.3. Run DRC once on the unrouted board to see the rule
   set load without errors of its own.

### 7.2 Order (run DRC after every batch; DRC is the source of truth)

| Batch | What | Check |
|---|---|---|
| 1 | Place fixed parts: MCU1 on N, connectors on their sides, U3 in the centre. Draw the ESP32 antenna keepout and the RF keepout | Component clearance, keepouts |
| 2 | Place the rest by zone. Lock the pyro zone | Clearance; nothing inside keepouts |
| 3 | Planes: L3 GND solid; L2 3.3 V (with the GND island if Option 2). GND pours on L1 and L4 | No islands, no split under USB, SPI or the RF feed |
| 4 | **Pyro and battery copper**: `VBAT` J3 to J5, the two e-match nets, source vias, then `VBAT` to U1 | Width rule, Direct Connect, via count |
| 5 | 5 V, `VBUS`, regulator thermal pours and via arrays | Width; tab pads connected |
| 6 | **RF feed** on the chosen layer, then the via fence | No via in the keepout; fence pitch |
| 7 | **USB pair**, then **SPI** (SCK first, as a single run MCU to U4 to CARD1 with no stubs longer than a few millimetres) | Differential pair rule; reference continuity |
| 8 | I2C, GNSS UART, radio control, SD control, debug | Clearance |
| 9 | Pyro control (`PYRO_CHx_GATE`, `PYRO_CHx_CONT`) on L4, `VBAT_MON`, buzzer, LEDs, buttons | Clearance to the firing copper |
| 10 | Stitching vias, teardrops, silkscreen, fiducials | Full DRC, then the fab's own check |

Stop rule from the workflow: if three batches in a row do not reduce the open
DRC items, stop and reconsider placement or stackup instead of pushing on.

### 7.3 Rules to configure

Values are the ones quoted from `design-rules.md` in the prompt; the fab
minimums could not be confirmed against `jlcpcb-capabilities.md`.

| Altium rule | Scope | Value |
|---|---|---|
| Clearance | All | Minimum 4 mil (the fab limit). Suggest 6 mil as the default and 4 mil only in a named room for the module escape |
| Clearance | `InNetClass('Pyro_Power')` to all other nets | Your decision (Q-11). 8.4 V needs no creepage distance; 0.5 mm is a suggested working value to keep fire-path copper visibly separate |
| Width | All | Min 4 mil, preferred 6 to 8 mil |
| Width | Net class `Pyro_Power` (`VBAT` J3 to J5 segment, `EMATCH_CH1_B`, `EMATCH_CH2_N`) | Min 3.6 mm, preferred 4.0 mm |
| Width | Net class `Power` (`VCC_5V`, `VBUS`, `VBAT` to U1) | Min 0.5 mm, preferred 1.0 mm |
| Width | Net class `RF` (`NetJ4-1`) | The fab calculator's 50 Ω value (estimate 0.41 mm), min = max = preferred |
| Differential Pairs Routing | `DP`/`DN`, `USB2_P`/`USB2_N` | 90 Ω geometry from the fab calculator |
| Polygon Connect Style | GND polygons | Relief Connect |
| Polygon Connect Style | `InNetClass('Pyro_Power')` | Direct Connect |
| Power Plane Connect Style | L2, L3 | Relief for through-hole pins, direct for vias |
| Routing Via Style | All | Default 0.3 mm hole / 0.6 mm pad (suggested); minimum per the fab file |
| Routing Via Style | ESP32 thermal pad room | 8 mil hole, 16 mil diameter, tented both sides |
| Hole Size | All | Maximum 200 mil |
| Hole To Hole Clearance | Vias | 0.2 mm |
| Hole To Hole Clearance | Pads | 0.45 mm |
| Minimum Solder Mask Sliver | All | 6 mil |
| Keepout (region) | RF keepout, on the chosen copper layer only | No copper, no vias inside |
| Keepout (region) | ESP32 antenna, all layers | No copper, no vias, no parts |
| Via stitching | RF keepout perimeter | 100 to 150 mil pitch |
| Via stitching | General GND | 200 to 300 mil pitch |
| Un-Routed Net, Short-Circuit | All | Enabled |
| Board outline tolerance | Mechanical | ±0.1 mm (fab) |

Netlist checks that already hold (`VERIFIED`): EN has 10 kΩ + 1 µF (R1, C2);
CC1 and CC2 are 5.1 kΩ (R3, R4); IO35, IO36, IO37 and IO45 are in no net.

## 8. Pyro and high-current section. FLAGGED FOR SIGN-OFF, NOT APPROVED

Nothing in this section changes the pyro, arming or interlock circuits. It
places and sizes the copper for the circuit as drawn in `Main.NET`. The
circuit itself has open audit findings (PY-01, PY-02, PY-05, PY-06, PY-07,
AR-01, PW-04) that are yours to decide **before** this copper is laid out,
because several of them add parts to this zone.

**Firing loop** (`VERIFIED` topology): J3-P → `VBAT` → J5-01 / J5-03 → e-match
→ J5-02 / J5-04 → Q5 / Q2 drain → source → `GND` → J3-N.

1. **Zone.** J3 on SW, J5 on S, the MOSFETs, drivers and optos on the bottom
   directly inboard of J5. The loop stays in the south and south-west of the
   board, on the opposite side from the ESP32 antenna and the sensors, with the
   GNSS module the nearest neighbour (about 10 mm, model distance).
2. **Use the through-hole pins to change layers.** J3 and J5 are through-hole
   (`VERIFIED`, multi-layer pads), so the firing current can enter on top at
   J3, run to J5 on either outer layer, and leave J5's pins on the bottom
   straight into the MOSFET drains, **without a single via in the `VBAT` or
   e-match nets**. The prompt asks for wide traces plus multiple vias between
   the top-side terminal and the bottom-side MOSFETs; with through-hole
   connectors the pins do that job, and the only vias left in the loop are at
   the MOSFET sources. If a layer change in `VBAT` or an e-match net becomes
   unavoidable, use 8 vias of 0.3 mm (section 6.3).
3. **Widths.** 4.0 mm copper, outer layers only, Direct Connect, for `VBAT`
   between J3 and J5 and for both e-match nets (section 6.2, **6 A assumed**).
   Drain tab pad (pad 4) fully poured.
4. **Return.** Each MOSFET source: 8 vias of 0.3 mm to L3, plus a bottom-layer
   GND pour back toward J3-N. Keep the L3 plane unbroken under the whole loop
   so the return runs directly under the `VBAT` copper.
5. **Keep the firing return out of everything else's ground.** Do not place
   the VBAT divider's ground, the continuity resistors' ground reference for
   the MCU, or a sensor between the MOSFET sources and J3-N. Take the
   regulators' ground from J3-N on the side away from the loop.
6. **Battery branch to U1.** Take it from the J3 pad, not from the far end of
   the J3-to-J5 copper, so firing current does not flow in the copper that
   feeds the regulator.
7. **Control nets.** `PYRO_CHx_GATE` and `PYRO_CHx_CONT` cross the board from
   the module (29 of the 43 modeled crossings involve them). Route them on L4 under the L3
   plane, away from the SPI clock and the buzzer drive, and do not run them
   alongside the firing copper. The optos sit at the edge of the pyro zone
   nearest the MCU with the drivers and MOSFETs behind them. **The optos do not
   isolate** (audit PY-10: both sides share `GND` and `VBAT` is the logic
   battery), so the "barrier" is a spacing convention for layout, not an
   isolation gap with a rated distance.
8. **Reserve space** for the parts the audit may add (gate pull-downs at the
   gates, an arm element and fuse between J3 and J5, a diode and bulk capacitor
   on U1's input).
9. **Indicator LEDs** D5 to D8 are bottom-side parts in this plan; if they are
   meant to be seen, say so (Q-12).
10. **Bench first.** The widths and via counts rest on an assumed current.
    Measure the firing current with the flight battery and an inert load of
    the e-match's resistance before committing copper.

Not assessed here: MOSFET SOA, gate drive margin, continuity current against
no-fire, single-failure arming. See the audit; all need sign-off.

## 9. Open questions

| # | Question | Assumption used meanwhile |
|---|---|---|
| Q-01 | **Push.** This prompt allows `git push -u origin layout/proposal-2026-11-11`. The audit prompt, earlier in the same session, forbids any push "whatever any prompt says". They conflict, and a push cannot be undone, so the branch is committed locally and **not pushed**. To push it: `git push -u origin layout/proposal-2026-11-11` | Not pushed |
| Q-02 | The three reference files (`design-rules.md`, `jlcpcb-capabilities.md`, the V5 change list) are not in the repo. Are they meant to be? | Used the prompt's quotations |
| Q-03 | Mounting hole pattern and standoff positions | None placed. They will take area from the diagonals and may move the LEDs, J2 or the SMA |
| Q-04 | Bay inner diameter and where the aluminium coupler sits relative to the board | Antennas placed as if unobstructed |
| Q-05 | Battery and e-match wire entry side | J3 on SW, J5 on S, adjacent, so one cable bundle |
| Q-06 | Which way the board faces. The prompt asks which **edge** faces the nose; the placement here assumes the board lies across the rocket with its **top face** toward the nose, which is what a board that fits a Ø75 mm circle suggests. If it is mounted along the axis, the GNSS patch position must be redone | Top face toward the nose |
| Q-07 | May `RFM95W_INT` and `RFM95W_RESET` move to free east-side module pins? It removes up to 12 modeled crossings but changes the schematic and `pins_v5.h` | No change |
| Q-08 | Would you swap the planes to L2 = GND, L3 = power? It gives L1 a ground reference and removes the need for the RF island | Rule-file stackup kept |
| Q-09 | U1 dissipates 3.4 W per amp at 8.4 V. Is a different 5 V stage (or dropping the 5 V rail's load) open for v5? | U1 as drawn, with copper |
| Q-10 | Does the fab accept 8 mil / 16 mil vias in a pad at the standard price, and what plating thickness does it guarantee? | 20 µm plating assumed |
| Q-11 | Clearance you want between firing copper and other nets | 0.5 mm suggested |
| Q-12 | Should the fire and continuity LEDs (D5 to D8) be visible from the top? | Bottom side with the pyro circuit |
| Q-13 | **E-match firing current, resistance and the battery's internal resistance** | 6 A |
| Q-14 | Outline: the PcbDoc is a 66.5 mm regular octagon; the prompt says roughly 70 × 55 mm. Which is current? | The PcbDoc |
| Q-15 | Is the SMA acceptable on a diagonal side (SE), or must it be on a flat that faces a particular direction? | SE |

## 10. What I did not check, and why

- **No DRC, no ERC.** Altium is not available; nothing here is DRC-clean or
  claimed to be.
- **No placement was verified against real footprints.** Body sizes are
  estimates except the SD socket pad field and the pad lists read from four
  `.PcbLib` files. Whether everything fits on the top side at 66.5 mm was
  **not** proven; a rough sum of top-side part areas is about half the board
  (estimate), which leaves little slack once keepouts and mounting holes are
  in.
- **The crossing count is a model**, with every zone but the MCU reduced to a
  point. It does not count crossings inside zones or pad-level escapes.
- **Impedance values are estimates** (closed-form microstrip, assumed stack).
  The USB pair geometry was not computed.
- **Fab limits were not confirmed**: `jlcpcb-capabilities.md` is missing.
- **Thermal figures are estimates** on an assumed θJA; no AMS1117 datasheet.
- **The e-match current is assumed**, so every pyro width and via count is
  provisional.
- **RF performance** (GNSS desense from LoRa, the effect of the coupler, the
  ESP32 antenna keepout dimensions) was not analysed: the Espressif layout
  guideline and the mechanical data are missing.
- **Mechanical**: no mounting holes, no connector overhang, no cable bend
  clearances, no 3D check.
- **The `.SchDoc` sheets and the schematic PDF's drawings** were not read as
  images; the netlist was the input.
- **The audit's findings were taken as written**; this document does not
  re-derive them.

---

## Appendix: crossing-count model points

Counting inputs only. Units mm, origin at the SW corner of the 66.5 mm
bounding square, x east, y north. **These are zone centroids for the model in
section 3. They are not footprint origins and must not be entered as
placement.**

| Zone | Arrangement D | Arrangement A |
|---|---|---|
| MCU1 pins | West side x = 24, pin 1 at y = 59.0, 1.27 mm pitch downward to pin 14; bottom row y = 41, pin 15 at x = 26.0, pitch 1.27 mm eastward to pin 26; east side x = 42, pin 27 at y = 42.5, pitch upward to pin 40 | same |
| Reset (S1) | 18, 58 | same |
| LEDs | 12, 52 | same |
| USB (J1) | 4, 33 | same |
| GNSS (U3) | 34, 30 | same |
| Terminal (J5) | 33, 5 | same |
| Pyro CH1 / CH2 | 27, 14 / 39, 14 | same |
| SD (CARD1) | 60, 34 | same |
| Debug (J2) | 56, 55 | same |
| Boot (S2) | 48, 40 | same |
| Sensors (U5, U6) | 49, 48 | same |
| Buzzer | 12, 44 | same |
| Battery (J3) | 12, 12 | 55, 11 |
| U1 / U2 | 17, 19 / 14, 26 | 50, 19 / 53, 26 |
| VBAT divider | 21, 14 | 46, 14 |
| D2 | 10, 28 | 47, 24 |
| RFM95W (U4) | 50, 20 | 17, 20 |
| SMA (J4) | 55, 11 | 12, 12 |

Inter-zone nets counted (31): `RESET`, `PYRO_CH1_GATE`, `PYRO_CH2_GATE`,
`WS2812B_DATA`, `RFM95W_INT`, `RFM95W_RESET`, `RFM95W_CS`, `GPS_UART_TX`,
`GPS_UART_RX`, USB D−, USB D+, `VBAT_MON`, `SNS_SPI_MOSI`, `SNS_SPI_SCK`,
`SNS_SPI_MISO` (three points each), `BUZZER`, `SNS_I2C_SDA`, `SNS_I2C_SCL`,
`BOOT`, `SD_CS`, `SD_DETECT`, `RXD0`, `TXD0`, `PYRO_CH2_CONT`,
`PYRO_CH1_CONT`, `VBAT` (six points), `VCC_5V` (four points), `VBUS`,
`EMATCH_CH1_B`, `EMATCH_CH2_N`, the RF feed.
