# ESP32 projects

Notes and code for two small ESP32 display boards, and for powering them on batteries.

| Folder / file | What it is |
|---|---|
| [`esp32_clock/`](esp32_clock/README.md) | A Waveshare ESP32-C6-LCD-1.47 status display: clock and weather, ROS 2 topics, Govee thermometers, and a dashboard combining them. Arduino (`arduino-cli`). |
| [`yellow_pad/`](yellow_pad/README.md) | A "Cheap Yellow Display" (ESP32-2432S028R) touchscreen used as a BLE gamepad to drive a ROS 2 robot. PlatformIO firmware plus a ROS 2 package. Its detailed notes are in [`yellow_pad/NOTES.md`](yellow_pad/NOTES.md). |
| [`POWER.md`](POWER.md) | Battery and power options for both boards: parts on hand, parts on order, and how they combine. |

`yellow_pad/` is a git submodule (github.com/Boston-Robot-Hackers/yellow_pad). Clone with `git clone --recurse-submodules`, or run `git submodule update --init` after a plain clone. Commit changes to it inside `yellow_pad/` first, then commit the updated submodule pointer here.

## Environment
- Mac (Apple Silicon, no Rosetta). Several Arduino-bundled tools are x86-only and fail with "bad CPU type". Each project's notes describe the workaround.
- ROS 2 runs in a VMware Fusion VM (Jazzy, 192.168.4.83) for the clock's ROS page. The Yellow Pad's robot runs ROS 2 Kilted on a Raspberry Pi.
