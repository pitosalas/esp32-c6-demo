# Power monitoring and sleep (planned 2026-11-10)

Goal: give both boards a battery gauge and a sleep mode, **using only the existing hardware: no new parts, not even a wire.**
- The **C6 board**: the ESP32 clock ([`esp32_clock/`](esp32_clock/README.md)).
- The **CYD**: the Yellow Pad ([`yellow_pad/`](yellow_pad/README.md)).

Power supplies and runtimes are in [`POWER.md`](POWER.md). The Yellow Pad's runtime tracking and idle dimming are built and flashed; the rest is planned.

## Why the boards can't measure their battery
- The PM11 (and the MiniBoost setups) give the boards a **regulated** 5 V or 3.3 V. The boards never see the cell voltage.
- Regulated rails stay flat until the cell is nearly empty, then collapse within minutes. The PM11 holds 5 V from about 4.2 V down to about 3.3 V. The CYD's AMS1117 holds 3.3 V as long as its input is above about 4.4 V. A gauge based on the rail would read 100% for hours, then 0%.
- The ESP32 measures analog inputs against an internal reference, not its supply, and reads at most about 3.1 V. No analog pin is wired to a supply or the cell on either board. The original ESP32 (CYD) has no documented way to read its own supply. None is known for the C6 either (not checked against its reference manual).
- Real measurement would take a two-resistor divider from the PM11's BAT header to an analog pin (GPIO 35 on the CYD, one of GPIO 0–5 on the C6). Ruled out for now: no wiring.
- Unchecked: whether the Waveshare C6 board already has a battery input with a divider on an analog pin. Worth checking its schematic.

## The approach: count runtime, saved to flash

**Yellow Pad: check-ins, brownout record and "charged" reset done (2026-11-10, flashed, not yet tested on battery).** Saves once a minute rather than every 5. "Charged" is a 3 s press on the status bar (the thin strip at the top of the screen), not a Settings button (the tab is full). The runtime shows as `Run h:mm` at the left of the status bar. The brownout is a sticky flag that turns that text amber; the pad does not stop or sleep after a brownout. Saved in NVS namespace `yellowpad_pwr`. Details in `yellow_pad/NOTES.md`. Measured awake draw before dimming and before the ROS tab: about 0.1 A at 5 V, so about 7.5 h on the PM11 (about 5.5 h stopping at one LED). The ROS tab turns Wi-Fi on while it's showing, which will draw more.

### Storage
| Storage | Survives a reset or deep sleep | Survives power loss | Use |
|---|---|---|---|
| **NVS flash** (Arduino `Preferences`) | yes | **yes** | Runtime totals and the brownout record. Wear-leveled, so a save every few minutes is fine for years. The Yellow Pad already uses it (namespaces `yellowpad`, `yellowpad2`, and `yellowpad_pwr` for runtime). |
| **RTC memory** (`RTC_NOINIT_ATTR`) | yes | no | Optional: counters between NVS saves, and state across deep sleep. |

### 1. Check-ins
- Every few minutes (about 5), save **runtime since last charge** to NVS.
- Also save time spent in each state (backlight on, asleep), so sleep shows up in the numbers.
- The clock can also save the real time (it has NTP). The CYD has no clock, so it saves only minutes.
- At most one interval of runtime is lost when power is cut.

### 2. Brownout record
- When the battery fails, the chip usually resets several times as the voltage sags. On each of those boots, `esp_reset_reason()` returns `ESP_RST_BROWNOUT`.
- On a brownout boot, **first** save "died on low battery after X h" to NVS, **before** turning on Wi-Fi, BLE or the backlight. Then stop, or go into deep sleep.
- Don't rely on seeing the reset reason after recharging: by then the board does a normal power-on (`ESP_RST_POWERON`), and the brownout is no longer reported.
- Writing flash at low voltage can fail. NVS survives an interrupted write, so the worst case is losing that one record, not the settings.
- This is the only way to tell "battery died" from "switched off". To the board both cut power instantly.

### 3. "Charged" reset
- Sets runtime back to zero after a charge.
- Yellow Pad: a button on the Settings tab.
- Clock: a long press of BOOT (GPIO 9). A short press still changes the page.

### 4. Gauge and self-calibration
- Before there's any data: estimate from the typical draws in `POWER.md` (about 100–150 mA per board) and the battery capacity. Show it as an estimate.
- After each brownout record: save the real runtime per charge. Average the last few runs and show **time used / time left** against that.
- Calibrate separately for each power setup (PM11, AAA pack, and so on). A setting picks which one is in use.
- Display: a small battery readout in the Yellow Pad's top status bar, and an icon on the clock's dashboard and clock pages. Show "last shutdown: battery low after X h" once after a brownout.
- Measuring each board once with a USB power meter (awake and asleep) would make the estimate better before calibration.

## Sleep when inactive

### Yellow Pad (CYD)
- Inactive = no touch.
- **Done (2026-11-10, flashed):** no touch for 2 minutes → backlight dims to `DIM_LEVEL` 12 (`DIM_AFTER_MS` in `main.cpp`). The waking touch is ignored until lifted, so a blind tap can't drive. A Bluetooth connect or disconnect also brightens it.
- Next: after more minutes → backlight off (GPIO 21), then deep sleep, using the same `lastActivityMs` timer.
- Wake: the touch controller's interrupt, GPIO 36 (`TOUCH_IRQ` in `main.cpp`), which can wake the chip from deep sleep (`ext0`).
- BLE drops while asleep and reconnects on wake, probably in a few seconds. That's safe: the robot stops when Bluetooth drops.
- Limits: the AMS1117, CH340 and power LED keep drawing several mA, so sleep draw stays far above the chip's microamp figures.
- To check: whether the PM11 switches itself off at very light loads. If it does, deep sleep could turn the pad off completely.

### Clock (C6)
- "Inactive" isn't decided: a night schedule (NTP time), a timeout since the last BOOT press, or both.
- Use **light sleep**, not deep sleep: on the C6 only GPIO 0–7 can wake the chip from deep sleep, and BOOT is GPIO 9. Deep sleep would also drop Wi-Fi and rosbridge.
- Backlight off (GPIO 22), Wi-Fi in power-save mode, light sleep between updates. Wake for the weather (every 15 min) and Govee (every 5 min). ROS updates stop while asleep.
- The backlight and Wi-Fi use most of the power, so this saves most of it.

## Power saving while awake

### Done (2026-11-10, built, not yet flashed or measured)
- **Clock: ROS topics stream only while the ROS page is on screen** (`rosSetVisible` in `ros_page.cpp`, called on each page change in `clock.cpp`). Before this, rosbridge sent up to 20 messages/s per topic all the time, which kept Wi-Fi from ever resting. `/rosout` stays subscribed so the log line is current. Tradeoff: for about 2 s after the page appears, active topics show `...` until the first rate window finishes. The topic list is only refreshed while the page is showing.
- **Clock: backlight dimmed** to `BACKLIGHT_LEVEL` 110 of 255 (PWM on GPIO 22, `clock.cpp`).
- **Yellow Pad: CPU at 80 MHz** instead of 240 (`CPU_MHZ` in `main.cpp`), the lowest speed BLE supports.
- **Yellow Pad: backlight dimmed** to `BACKLIGHT_LEVEL` 128 of 255 (PWM on GPIO 21, `main.cpp`). It's a constant for now: the Settings tab fits only four rows, so a brightness row needs paging first.

### Still to do
| Board | Change | Notes |
|---|---|---|
| Clock | Set Wi-Fi modem sleep explicitly: `WiFi.setSleep(WIFI_PS_MIN_MODEM)` | Pays off now that the ROS stream is off most of the time. |
| Clock | CPU at 80 MHz (`setCpuFrequencyMhz(80)`) | Wi-Fi still works at 80 MHz. Redraws get slower. |
| Yellow Pad | Back off advertising after a few minutes unconnected, or count it as inactive and sleep | Pairing takes longer after a long idle. |
| Yellow Pad | Lower BLE transmit power | Test at the longest driving distance. |
| Yellow Pad | Brightness row on the Settings tab | Needs paging. |
| Both | Measure awake and asleep draw with a USB power meter | Shows what each change saved, and tightens the gauge estimate. |

Fixed board loads that firmware can't reduce: the CYD's AMS1117, CH340 and power LED.

## Order
1. Yellow Pad: runtime counting and the brownout record. It already uses NVS.
2. Yellow Pad: sleep.
3. Clock: the same runtime counting and brownout record, plus the gauge on screen.
4. Clock: sleep, once the trigger is decided.

## Open questions
- Clock sleep trigger: night schedule, button timeout, or both?
- Does the Waveshare C6 board have a battery input already?
- Does the PM11 cut out at low load?
- Check-in interval and sleep timeouts.
