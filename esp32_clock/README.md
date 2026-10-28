# ESP32 clock (Waveshare ESP32-C6-LCD-1.47)

It started as a Wi-Fi clock and is now a small status display. Four pages rotate every 10 s:

1. **Clock**: time (NTP, US Eastern, 12-hour), date, current weather and an 8-hour forecast.
2. **ROS**: topics and the latest `/rosout` line from a ROS 2 system, through rosbridge.
3. **Govee**: temperature and humidity from two Govee H5179 Wi-Fi thermometers.
4. **Dashboard**: the clock, weather and thermometers on one screen.

The BOOT button (GPIO 9) advances to the next page right away.

Power options for this board are in [`../POWER.md`](../POWER.md). The network scan that found the thermometers is in [`NETWORK_SCAN.md`](NETWORK_SCAN.md).

## The board
**Waveshare ESP32-C6-LCD-1.47.** 172x320 ST7789 IPS screen, a single WS2812 RGB LED, about 1 7/8 x 3/4 in.
- USB: Espressif USB-Serial/JTAG `303a:1001`, port `/dev/cu.usbmodem1101`.
- esptool reports **ESP32-C6FH8**, 8 MB flash. Waveshare's spec sheet says the 4 MB C6FH4, so this unit is an 8 MB revision.
- Earlier I wrongly guessed the ESP32-C6-LCD-0.85 from the chip's flash size. The 0.85 board's pins and Waveshare's factory demo for it didn't work: the backlight came on but nothing was drawn, and its audio chip at I2C 0x18 never answered.
- The factory demo that originally came on the board was overwritten, first by the clock and then by the 0.85's factory image.

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
| BOOT button | 9 |

Panel: ST7789, IPS (inverted colours), 172x320, column offset 34, row offset 0. The clock uses rotation 1 (landscape, 320x172).

## Code
- `clock.cpp`: setup, the main loop, page rotation, and the clock and dashboard pages (Arduino_GFX + `configTzTime`; the LED is turned off with the core's `rgbLedWrite`). Settings at the top: `TIMEZONE` (POSIX string), `USE_24H`, `WEATHER_LAT`/`WEATHER_LON`, `PAGE_ROTATE_MS`.
- `ros_page.cpp` / `.h`: the ROS page.
- `govee_page.cpp` / `.h`: the Govee page, and `goveeThermometers()` for the dashboard.
- `display.h`: drawing helpers shared by the pages.
- `govee_ca.h`: Amazon Root CA 1, for verifying the Govee API's HTTPS.
- `fonts/`: Adafruit FreeSans headers with the `#include <Adafruit_GFX.h>` line removed. They're 7-bit, so the degree sign is drawn as a ring.
- `esp32_clock.ino`: only a comment, because Arduino needs a .ino named after the folder.
- `secrets.h` (git-ignored; copy `secrets.h.sample`): `WIFI_SSID`, `WIFI_PASSWORD`, `GOVEE_API_KEY`.

## Toolchain
- `arduino-cli` 1.5.1 (Homebrew), core `esp32:esp32@3.2.0`. Libraries: `GFX Library for Arduino@1.6.0`, ArduinoJson 7.4.3, WebSockets 2.7.2 (Markus Sattler). FastLED 3.10.1 is installed but no longer used.
- The bundled `ctags` is an x86 binary and this Mac has no Rosetta. The workaround: keep the code in `.cpp` and pass `--build-property tools.ctags.cmd.path=/usr/bin/true`.
- The FQBN needs `PartitionScheme=default_8MB` (3 MB app partition). The default 1.3 MB partition is too small since the ROS page was added.
- Arduino's `arduino-cli monitor` binary is x86 too (bad CPU type), so it can't read the serial log. Use `stty` and `cat`, started right after the upload, or install pyserial.

```
cd /Users/pitosalas/mydev/esp32-c6-demo/esp32_clock
F=esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=default_8MB
arduino-cli compile --fqbn $F --build-property tools.ctags.cmd.path=/usr/bin/true .
arduino-cli upload  --fqbn $F -p /dev/cu.usbmodem1101 .
stty -f /dev/cu.usbmodem1101 115200 raw; cat /dev/cu.usbmodem1101
```

## Pages

### Clock page (redesign 2026-10-26)
- It draws into an `Arduino_Canvas` (320x172 off-screen buffer) and pushes the whole screen to the panel at once. The screen redraws every minute and after each weather fetch.
- Layout: the time, AM/PM and date are on the top left. The current weather (drawn icon, temperature, label, high/low) is on the top right. An 8-hour forecast strip runs along the bottom (hour, icon, temperature, rain % when it's 10% or more).
- Weather comes from Open-Meteo over plain HTTP (no key). The location is Arlington MA (42.4188, -71.1557). It refreshes every 15 minutes and retries after 1 minute on failure.

### ROS page (2026-10-26)
- ROS 2 runs in a VMware Fusion VM on this Mac: "Ubuntu 64-bit Arm 24.04.4", so Jazzy. It's bridged to Wi-Fi at **192.168.4.83**, domain 0, Fast DDS. Right now it runs RViz and rosbridge only, with no robot. The topics exist because RViz subscribes to them.
- SSH to the VM is refused (publickey/password). The VM username isn't known. All checks go through rosbridge (port 9090) from the Mac, using raw WebSocket scripts.
- `ros_page.cpp` connects to rosbridge. It calls `/rosapi/topics` every 30 s and subscribes to the non-heavy topics with throttle 50 ms, so rates are capped and show as "20+ Hz". It shows a green dot for a message in the last 3 s and amber for seen earlier, plus the latest `/rosout` line, ignoring rosbridge's own lines.
- Heavy types aren't subscribed and show "skip": Image, CompressedImage, PointCloud2, OccupancyGrid, OccupancyGridUpdate, MarkerArray, and `/robot_description`.
- After the partition change the ESP32 appeared at **192.168.4.105**. rosbridge's `/connected_clients` listed it.

### Govee page (2026-10-26)
- Uses the Govee cloud API (openapi.api.govee.com) with the key in `secrets.h`.
- `GET https://openapi.api.govee.com/router/api/v1/user/devices` with the `Govee-API-Key` header lists two **H5179** Wi-Fi thermometers:
  - "Garden": 16:A0:18:1F:6C:71:DD:2C
  - "Cottage Thermometer": 44:19:E0:E1:02:C6:6B:4D
- To read one: `POST .../router/api/v1/device/state` with body `{"requestId":..,"payload":{"sku":"H5179","device":..}}`. It returns `online`, `sensorTemperature` (°F) and `sensorHumidity` (%).
- The page fetches the device list hourly and readings every 5 minutes, retrying after 1 minute on failure. Each thermometer gets a column; " Thermometer" is removed from the end of names.
- The thermometers are shown as **MA** (Garden) and **NH** (Cottage) via `ALIASES` in `govee_page.cpp`, matched by device ID. The Govee app names are unchanged.
- Confirmed on the serial log: "govee: 2 thermometers", Garden 42.4F 85%, Cottage 67.1F 44%.

### Dashboard page (2026-10-26)
- `drawDashboardPage` in `clock.cpp` combines the clock, weather and Govee pages.
- Bands: y 0–52 time/date on the left, current weather on the right. y 54–80 one line per thermometer ("Garden: 42.4°/85%"). y 82–172 the 8-hour strip with 28 px icons. The user wants the time to stay at its current size (18pt).
- The Govee data is shared through `goveeThermometers()`; `struct Thermometer` is in `govee_page.h`.

## Status
- Flashed and running. The user confirmed the clock shows correctly on the screen (2026-10-25). The serial log shows Wi-Fi connected and the time synced.
