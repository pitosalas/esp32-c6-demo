# Home network scan for thermometers (2026-10-25)

Background for the clock's Govee page (see [`README.md`](README.md)): before we knew the thermometers were Govee, the home network was scanned to find them.

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

## Follow-up (2026-10-26)
- The user said the thermometers are **Govee Wi-Fi** devices. The Govee cloud API found them (two H5179s); see the Govee page in [`README.md`](README.md).
- A Govee LAN API scan (UDP multicast to 239.255.255.250:4001, listening on 4002) got no replies. That's expected, because Govee's LAN API only covers lights.
- They're probably the Espressif devices at .136 (CC:BA:97:09:FB:08) and .81.

## Still open
- Match the unnamed IPs to names in the eero app.
- Any Matter device can be identified exactly once it's added to a controller (Google Home / Apple Home / chip-tool).
