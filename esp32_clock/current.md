# ESP32 clock: current state (2026-10-25)

## Goal
Show the current time (Wi-Fi + NTP, US Eastern, 12-hour) on the board's built-in screen.

## The board
**Waveshare ESP32-C6-LCD-1.47.** 172x320 ST7789 IPS screen, a single WS2812 RGB LED, about 1 7/8 x 3/4 in.
- USB: Espressif USB-Serial/JTAG `303a:1001`, port `/dev/cu.usbmodem1101`.
- esptool reports **ESP32-C6FH8**, 8 MB flash. Waveshare's spec sheet says the 4 MB C6FH4, so this unit is an 8 MB revision.
- Earlier I wrongly guessed the ESP32-C6-LCD-0.85 from the chip's flash size. The 0.85 board's pins and Waveshare's factory demo for it didn't work: the backlight came on but nothing was drawn, and its audio chip at I2C 0x18 never answered.
- The board now runs the clock. The factory demo that originally came on it was overwritten, first by the clock and then by the 0.85's factory image.

## Pins
| Signal | GPIO |
|---|---|
| LCD MOSI | 6 |
| LCD SCLK | 7 |
| LCD CS | 14 |
| LCD DC | 15 |
| LCD RST | 21 |
| LCD backlight | 22 (high = on) |
| RGB LED (WS2812) | 8 |

Panel: ST7789, IPS (inverted colours), 172x320, column offset 34, row offset 0. The clock uses rotation 1 (landscape, 320x172).

## Code
- `clock.cpp`: all the code (Arduino_GFX + `configTzTime`; the LED is turned off with the core's `rgbLedWrite`).
- `esp32_clock.ino`: only a comment, because Arduino needs a .ino named after the folder.
- `secrets.h`: `WIFI_SSID` / `WIFI_PASSWORD`.
- Settings at the top of `clock.cpp`: `TIMEZONE` (POSIX string) and `USE_24H`.

## Toolchain
- `arduino-cli` 1.5.1 (Homebrew), core `esp32:esp32@3.2.0`, library `GFX Library for Arduino@1.6.0`. FastLED 3.10.1 is installed but no longer used.
- The bundled `ctags` is an x86 binary and this Mac has no Rosetta. The workaround: keep the code in `.cpp` and pass `--build-property tools.ctags.cmd.path=/usr/bin/true`.

```
cd /Users/pitosalas/mydev/demo/esp32_clock
F=esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=8M
arduino-cli compile --fqbn $F --build-property tools.ctags.cmd.path=/usr/bin/true .
arduino-cli upload  --fqbn $F -p /dev/cu.usbmodem1101 .
arduino-cli monitor -p /dev/cu.usbmodem1101 -c baudrate=115200
```

## Redesign (2026-10-26)
- `clock.cpp` was rewritten from scratch. It draws into an `Arduino_Canvas` (320x172 off-screen buffer) and pushes the whole screen to the panel at once. The screen redraws every minute and after each weather fetch.
- Layout: the time, AM/PM and date are on the top left. The current weather (drawn icon, temperature, label, high/low) is on the top right. An 8-hour forecast strip runs along the bottom (hour, icon, temperature, rain % when it's 10% or more).
- Weather comes from Open-Meteo over plain HTTP (no key), with ArduinoJson 7.4.3. The location is Arlington MA (42.4188, -71.1557), set by `WEATHER_LAT`/`WEATHER_LON`. It refreshes every 15 minutes and retries after 1 minute on failure.
- Fonts: Adafruit FreeSans headers copied into `fonts/` with the `#include <Adafruit_GFX.h>` line removed. They're 7-bit, so the degree sign is drawn as a ring.
- Flash use is 90% of the default 1.3 MB app partition. Bigger features will need a larger partition scheme.
- Arduino's `arduino-cli monitor` binary is x86 too (bad CPU type), so it can't be used to read the serial log. Use `stty -f PORT 115200 raw; cat PORT`, or install pyserial.

## ROS 2 page (2026-10-26)
- ROS 2 runs in a VMware Fusion VM on this Mac: "Ubuntu 64-bit Arm 24.04.4", so Jazzy. It's bridged to Wi-Fi at **192.168.4.83**, domain 0, Fast DDS. Right now it runs RViz and rosbridge only, with no robot. The topics exist because RViz subscribes to them.
- SSH to the VM is refused (publickey/password). The VM username isn't known. All checks go through rosbridge (port 9090) from the Mac, using `scratchpad/rb.py`-style raw WebSocket scripts.
- `ros_page.cpp` connects to rosbridge with the WebSockets@2.7.2 library (Markus Sattler). It calls `/rosapi/topics` every 30 s and subscribes to the non-heavy topics with throttle 50 ms, so rates are capped and show as "20+ Hz". It shows a green dot for a message in the last 3 s and amber for seen earlier, plus the latest `/rosout` line, ignoring rosbridge's own lines.
- Heavy types aren't subscribed and show "skip": Image, CompressedImage, PointCloud2, OccupancyGrid, OccupancyGridUpdate, MarkerArray, and `/robot_description`.
- The BOOT button (GPIO 9) flips between the clock and ROS pages. The shared drawing helpers are in `display.h`.
- **The FQBN now needs `PartitionScheme=default_8MB`** (3 MB app partition):
  `F=esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=default_8MB`
- After the partition change the ESP32 appeared at **192.168.4.105**. rosbridge's `/connected_clients` listed it.

## Status
- The 1.47 build is flashed. The serial log shows Wi-Fi connected (192.168.4.98) and the time synced.
- **Working:** the user confirmed the clock shows correctly on the screen (2026-10-25).

---

# Home network scan for thermometers (2026-10-25)

## Network
- Subnet 192.168.4.0/22 (192.168.4.0 to 192.168.7.255). Gateway: eero at 192.168.4.1. This Mac (macmini2026i) is 192.168.4.62 / .125.
- The eero doesn't answer reverse-DNS (PTR) queries, so names came only from what devices announce themselves (mDNS and SSDP). **The eero app shows a name for every device** and is the best way to identify the leftovers.

## Methods used
- mDNS/Bonjour browse of all service types, plus mDNS reverse lookups.
- Ping sweep of the /22, then the ARP table, then MAC vendor lookup (first 3 bytes only, via api.macvendors.com).
- NetBIOS (`smbutil status`). Nothing answered.
- HTTP/HTTPS `GET /`, plus Shelly `/shelly` and Tasmota `/cm?cmnd=Status` on the Espressif devices.
- SSDP M-SEARCH, then the UPnP friendlyName.
- Resolved the Matter and HomeKit service records to IP addresses.

## Result
No device identifies itself as a thermometer.

**Most likely thermostats or thermometers:**
| IP | Vendor | Evidence |
|---|---|---|
| 192.168.4.39 | Google | Matter node (fabric 1A205AB89866BEAC), no Chromecast service. Fits a Nest Thermostat. |
| not in ARP | Google, MAC 3C:31:74:E9:98:7A | A second Google Matter node on the same fabric. Didn't answer ping. |
| 192.168.4.25 | Amazon | Matter node (fabric 10A36A96EA8D9B75), probably an Echo. Some Echo models have a temperature sensor. |

**Unnamed devices that could be sensors** (no mDNS name, no web server):
| IP | Vendor | Notes |
|---|---|---|
| .81, .136 | Espressif | Not Shelly or Tasmota |
| .130 | Sichuan AI-Link | Often Tuya / Smart Life |
| .27 | AMPAK | Generic Wi-Fi module |
| .28 | LG Innotek | Appliance or IoT module |
| .31 | TP-Link | Kasa/Tapo plug or hub |
| .218 | HPE | Probably an Aruba access point |

**Identified, not thermometers:**
- **Audio:** Denon AVR-S650H (.20), Sonos One Kitchen (.168), Sonos One Living Room (5.232), WiiM Mini-CC3E (.111)
- **Network and storage:** eero units (.1, 5.89, 5.91), interlakennas Synology DS218+ (.248)
- **Computers and tablets:** Entertainment-Room Apple TV (.22), macmini2026i (.62), CKS-Macbook-Air-2022 (.50), iPad-2 (.44), RPSiPhone (.45), omarchy (.155, a Linux machine), mini (.165)
- **Android:** Android (.221) and Android-2 (.24), Amazon hardware
- **Other:** dp-518301HE (.29, Amazon hardware with SSH/SFTP), Brother HL-3170CDW printer (.121)
- **Home devices:** Rachio-F595DC sprinkler (.36, HomeKit, Murata Wi-Fi), iRobot (5.28), Ring (.26), Blink (.61), Google (.47, not Matter), Apple (.101 and .155 share one Apple-made network adapter), the ESP32 clock (.98)

## Update (2026-10-26)
- The user says the thermometer is a **Govee Wi-Fi** device. The model isn't known yet.
- Plan: use the Govee cloud API (openapi.api.govee.com). The key will be kept in `~/.govee_key`. Then show the temperature and humidity on the clock.
- A Govee LAN API scan (UDP multicast to 239.255.255.250:4001, listening on 4002) got no replies. That's expected, because Govee's LAN API only covers lights.
- If it's a Wi-Fi model, it's probably one of the Espressif devices: .136 (CC:BA:97:09:FB:08) or .81.

## Govee API works (2026-10-26)
- The key is in `secrets.h` as `GOVEE_API_KEY`. `secrets.h.sample` and `.gitignore` were created; the folder isn't a git repo yet.
- `GET https://openapi.api.govee.com/router/api/v1/user/devices` with the `Govee-API-Key` header lists two **H5179** Wi-Fi thermometers:
  - "Garden": 16:A0:18:1F:6C:71:DD:2C
  - "Cottage Thermometer": 44:19:E0:E1:02:C6:6B:4D
- To read one: `POST .../router/api/v1/device/state` with body `{"requestId":..,"payload":{"sku":"H5179","device":..}}`. It returns `online`, `sensorTemperature` (°F) and `sensorHumidity` (%). A reading at about 8 am: Garden 41.5°F / 87%, Cottage 67.1°F / 44.5%.

## Govee page (2026-10-26)
- `govee_page.cpp` is the third page. Pages cycle clock → ROS → Govee every 10 s (`PAGE_ROTATE_MS`); a BOOT press advances right away and restarts the timer. It fetches the device list hourly and readings every 5 minutes, retrying after 1 minute on failure. Each thermometer gets a column; " Thermometer" is removed from the end of names.
- HTTPS is verified against Amazon Root CA 1 (`govee_ca.h`, valid to 2038).
- Confirmed on the serial log: "govee: 2 thermometers", Garden 42.4F 85%, Cottage 67.1F 44%.
- The serial log works with `stty -f /dev/cu.usbmodem1101 115200 raw; cat /dev/cu.usbmodem1101` started right after the upload.

## Next steps
- Match the unnamed IPs to names in the eero app.
- Any Matter device can be identified exactly once it's added to a controller (Google Home / Apple Home / chip-tool).

## Dashboard page (2026-10-26)
- The fourth page (`drawDashboardPage` in `clock.cpp`) combines the clock, weather and Govee pages. Rotation is now clock → ROS → Govee → dashboard.
- Bands: y 0–52 time/date on the left, current weather on the right. y 54–80 one line per thermometer ("Garden: 42.4°/85%"). y 82–172 the 8-hour strip with 28 px icons. The user wants the time to stay at its current size (18pt).
- The Govee data is shared through `goveeThermometers()`; `struct Thermometer` moved to `govee_page.h`.
- The thermometers are shown as **MA** (Garden) and **NH** (Cottage) via `ALIASES` in `govee_page.cpp`, matched by device ID. The Govee app names are unchanged.
