# Power options (started 2026-10-28)

Goal: run both boards away from the Mac's USB ports.
- The **C6 board**: the ESP32 clock ([`esp32_clock/`](esp32_clock/README.md)).
- The **CYD**: the Yellow Pad ([`yellow_pad/`](yellow_pad/README.md)).

All runtimes below are estimates from typical numbers, not measurements.

## The boards

### C6 board (Waveshare ESP32-C6-LCD-1.47)
- Today it's powered over USB-C from the Mac.
- The firmware keeps Wi-Fi on all the time (NTP, weather every 15 min, Govee every 5 min, rosbridge WebSocket) and the backlight is always on. No sleep modes are used.
- Rough draw (estimate): about 100–150 mA average at 5 V, with Wi-Fi transmit peaks near 300 mA. A supply should handle at least 500 mA.
- To check: whether this board has a LiPo connector and charger, which header pins accept 5 V or 3.3 V in, and how low its 3.3 V regulator can go.

### CYD (ESP32-2432S028R, "Cheap Yellow Display")
- ESP32-D0WD-V3 with a 2.8 in 320x240 ILI9341 touch screen. Details are in [`yellow_pad/NOTES.md`](yellow_pad/NOTES.md), under "The hardware".
- USB is **micro-USB** (CH340). Don't use a USB-C to USB-C cable: the board lacks the resistors that make a USB-C source switch on.
- Power in: 5 V through USB, or through the 4-pin JST 1.25 mm "P1" connector (VIN, TX, RX, GND). Its 3.3 V comes from an **AMS1117** regulator, which needs about 4.4 V in or more because it drops about 1.1 V.
- Rough draw: about 100–150 mA (Yellow Pad notes), maybe up to 200 mA with the backlight at full.
- So the CYD effectively needs a real 5 V. A raw LiPo (3.0–4.2 V) or 3 AA/AAA cells won't run it reliably without a boost converter. It has no charging or low-voltage cutoff of its own.

## Parts on hand

### TP4056 charger board
HiLetgo 3-pack: "TP4056 Type-C USB 5V 1A 18650 Lithium Battery Charger Module with Dual Protection Functions."
- **It's only a charger.** It charges one Li-ion/LiPo cell (4.2 V full) from USB-C at up to 1 A. It does not regulate its output.
- "Dual protection" usually means a DW01A chip plus an 8205A MOSFET. That protection cuts the load off when the cell drops to about 2.5 V, and also handles overcharge and short circuits.
- Pads: `IN+`/`IN-` (5 V in, same as the USB-C port), `B+`/`B-` (the cell), and `OUT+`/`OUT-` (the load, through the protection).
- **OUT is the raw cell voltage (about 3.0–4.2 V), not 5 V.**
  - That's too high for a 3.3 V pin, whose ESP32 limit is 3.6 V.
  - Fed into the C6 board's 5 V pin, it has to pass through the board's own 3.3 V regulator, and that only works while the cell is above roughly 3.5 V.
  - The CYD needs about 4.4 V.
  - So it needs a **boost converter to 5 V** after it (the MiniBoost, on order), or a low-dropout 3.3 V regulator for the C6 board's 3V3 pin.
- The 1 A charge rate is set by a resistor on the board (usually 1.2 kΩ, marked R3 or "Rprog"). That's fine for an 18650 but too fast for small LiPo pouch cells. Swap the resistor for a lower current.

### EEMB LiPo cells: 3.7 V 1100 mAh, model 603449, 4-pack, JST 2.0 mm (PH) plug
- Size: the model number means 6.0 x 34 x 49 mm. That's about the C6 board's length (49 mm vs. about 48 mm) but nearly twice its width (34 mm vs. about 19 mm).
- Pairs with the TP4056: one cell, 4.2 V full charge. The TP4056 has solder pads, not a JST socket. Either solder a JST PH 2.0 socket onto `B+`/`B-` or cut the plug off. **Check the polarity:** JST battery plugs aren't wired the same way across brands. Red should go to `B+`.
- Charge rate: 1 A on 1100 mAh is about 0.9C. Pouch cells like this are usually rated about 0.5C for standard charging and about 1C maximum, so it's near the top, probably not over. Check EEMB's datasheet for the exact limit. See "TP4056 charge current" below.
- EEMB cells usually have their own protection board under the tape, so with the TP4056's protection there would be two layers. Not verified for this cell.
- Rough runtime at about 130 mA: **7–8 hours** with the C6 board's always-on firmware (less after boost losses). Going much longer would need firmware changes: dim or switch off the backlight, and turn Wi-Fi off between updates.

### All-in-one LiPo module: "LiPo Battery Module, USB-C Rechargeable 1200mAh, 3.3V 5V USB Output"
- **Probably the NULLLAB PM11** that the Yellow Pad notes already plan around (docs: https://github.com/nulllaborg/pm11-module). Not confirmed. If it is, what the Yellow Pad notes say about it:
  - 1200 mAh LiPo, boost to 5 V/1.2 A, USB-C charging (about 1 hour), 4-LED gauge, slide switch, 56×40 mm with two M4 holes.
  - **No low-battery cutoff.** Switch it off after use and recharge when only one LED is lit.
  - **Not a UPS:** plugging or unplugging USB-C while running will likely reset the board.
  - With the switch off and USB-C plugged in, USB powers the board and the battery stays out of the circuit.
  - Still to check: whether it auto-shuts off at light load. Run a board on it for 30+ minutes.
- It combines the cell, charger and regulated outputs in one unit, so **it doesn't need the extra boost converter the TP4056 does.**
- C6 board: either the **5 V USB output to its USB-C port** (no soldering), or the **3.3 V output to its 3V3 and GND pins**, which skips the board's regulator. Check the pins before connecting.
- CYD: a short USB-A to micro-USB cable, or its 5 V header to the CYD's P1 connector (5V → VIN, GND → GND; check polarity against both pinouts).
- Rough runtime (1200 mAh × 3.7 V ≈ 4.4 Wh, about 85% efficiency): **about 5–6 hours** from 5 V and **about 8–9 hours** from 3.3 V on the C6 board. The Yellow Pad notes estimate 5–8 hours on the CYD.

### 3 x AA battery pack, no electronics yet
The cell type (alkaline or NiMH) isn't decided.
- Voltage: alkaline is about 4.5 V fresh, falling to about 3.0 V when empty. NiMH is about 3.6 V (up to about 4.2 V straight off the charger).
- Into a **3V3 pin directly: no.** It's above the 3.6 V limit for most of the discharge.
- Into the **C6 board's 5 V pin:** works only while the voltage stays above what the board's regulator needs, so the pack stops well before the cells are empty.
- Into the **CYD:** not reliable. Its AMS1117 needs about 4.4 V, and three alkalines drop below that quickly.
- With a **boost converter to 5 V** (the MiniBoost), it's a good supply for either board and uses nearly all of the battery's charge.
- Rough runtime with a converter (alkaline AA about 2000 mAh): **about 10–12 hours** on the C6 board, roughly **6–10 hours** on the CYD.
- No recharging unless NiMH cells and a separate charger are used. It also needs an on/off switch.

### TP4056 charge current
**Decision (2026-10-28): the user will probably leave the stock 1 A resistor.**

If left at 1 A with the 1100 mAh EEMB cells:
- A full charge takes about 1.5 h. The cell runs warmer and loses capacity faster over hundreds of cycles. The risk of swelling is higher if it charges warm or enclosed.
- The TP4056 lowers its own current when the chip gets hot, so the real rate may be under 1 A.
- Precautions: charge where it can be seen, on a hard surface, not inside a closed case. Retire any cell that puffs up.

If you ever want to lower it: the current is set by **R3** ("Rprog"), a tiny 0603 surface-mount resistor next to the chip, printed `122` (1.2 kΩ). Charge current ≈ 1200 ÷ R amps.

| R3 | Code | Current |
|---|---|---|
| 1.2 kΩ (stock) | 122 | ~1 A |
| 2.0 kΩ | 202 | ~580 mA |
| 2.2 kΩ | 222 | ~545 mA |
| 2.4 kΩ | 242 | ~500 mA |
| 3.0 kΩ | 302 | ~400 mA |

- **Replace it:** add flux, melt a blob of solder across both ends to lift it off, clean with wick, then solder an 0603 2.2 or 2.4 kΩ in its place.
- **No new part:** lift one end of R3 and add another 1.2 kΩ in series (1.2 + 1.2 = 2.4 kΩ, 500 mA).
- **Through-hole:** remove R3 and solder an ordinary 2.4 kΩ resistor across the pads with short legs.
- Never add a resistor in *parallel*: that raises the current.
- Check: about 2.4 kΩ across the pads, with the board unpowered. A USB power meter should read about 0.5 A charging a partly drained cell.

## On order (2026-10-28)
- **1 x Adafruit 3 x AAA battery holder with on/off switch and 2-pin JST** (ID 727, $1.95). About 4.5 V with alkalines, falling to about 3.0 V. Its JST is probably PH 2.0 mm, Adafruit's standard (not verified). AAA cells hold about half what AAs do, roughly 1000 mAh alkaline.
- **2 x Adafruit MiniBoost 5V @ 1A, TPS61023** (ID 4654, $7.90 for two). A fixed 5 V step-up, so nothing to adjust. It accepts inputs below 5 V, which covers a LiPo (3.0–4.2 V) and 3 x AA/AAA (3.0–4.5 V). It provides enough current for either board's Wi-Fi peaks.

## Combinations

| Setup | Boards | Rechargeable | Rough runtime | Notes |
|---|---|---|---|---|
| All-in-one module (PM11?) → USB | both | yes | 5–6 h (C6), 5–8 h (CYD) | Simplest. No low-battery cutoff, resets when plugged in. |
| EEMB LiPo → TP4056 → MiniBoost → 5 V/VIN | both | yes | about 6–7 h | Soldering needed. Two MiniBoosts means both boards can be wired this way. Charges at 1 A unless R3 is swapped. |
| AAA holder → MiniBoost → 5 V/VIN | both | no (NiMH optional) | 5–6 h (C6), 3–5 h (CYD) | Switched, no soldering at the battery end. |
| AA pack → MiniBoost → 5 V/VIN | both | no (NiMH optional) | 10–12 h (C6), 6–10 h (CYD) | Longest runtime. Needs a switch. |

**Before connecting:** check that the JST plugs on the AAA holder, the EEMB cells and anything they plug into have the same polarity.

## How a single 3.7 V LiPo is usually used (background)
Every setup puts a charger and protection circuit next to the cell. After that it depends on the load:
1. **Charger → step-up to 5 V → board.** The usual setup for 5 V boards (Arduino, Pi Zero, the CYD).
2. **A board with the charger built in** (Adafruit Feather, Seeed XIAO, LOLIN): JST socket, charger and low-dropout 3.3 V regulator on board. Plug in the LiPo and it works down to about 3.5 V.
3. **In a robot:** small DC motors run straight off the cell through a driver that works at that voltage (e.g. DRV8833). The ESP32 gets its own regulated supply, with a 100–470 µF capacitor across the battery so motor spikes don't reset it. Servos want 4.8–6 V, so they need their own 5 V step-up.
4. **Two cells (7.4 V)** once there are real motors. Stepping down wastes less than stepping up.

A useful habit: a two-resistor divider from the battery to an analog input lets the ESP32 read its own battery level.

## Decision
(not yet)
