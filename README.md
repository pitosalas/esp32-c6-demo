# ESP32 projects

Notes and code for two small ESP32 display boards, and for powering them on batteries.

| Folder / file | What it is |
|---|---|
| [`esp32_clock/`](esp32_clock/README.md) | A Waveshare ESP32-C6-LCD-1.47 status display: clock and weather, ROS 2 topics, Govee thermometers, and a dashboard combining them. Arduino (`arduino-cli`). |
| Yellow Pad (not here) | A "Cheap Yellow Display" (ESP32-2432S028R) touchscreen used as a BLE gamepad to drive the DOME robot. It moved to its own repo, [`Boston-Robot-Hackers/dome_pad`](https://github.com/Boston-Robot-Hackers/dome_pad) (`~/mydev/dome_pad`); its detailed notes are in that repo's `process/02-doc/notes.md`. |
| [`POWER.md`](POWER.md) | Battery and power options for both boards: parts on hand, parts on order, and how they combine. |
| [`power_monitor.md`](power_monitor.md) | Plan for a battery gauge and sleep on both boards, using only the existing hardware. |

The Yellow Pad used to be the `yellow_pad/` submodule here. It became dome_pad on 2026-11-22, and `Boston-Robot-Hackers/yellow_pad` is archived.

## Environment
- Mac (Apple Silicon, no Rosetta). Several Arduino-bundled tools are x86-only and fail with "bad CPU type". Each project's notes describe the workaround.
- ROS 2 runs in a VMware Fusion VM (Jazzy, 192.168.4.83) for the clock's ROS page. The Yellow Pad's robot runs ROS 2 Kilted on a Raspberry Pi.
