# modbus-matter-meter

Reads a 3-phase Modbus RTU energy meter (F&F LE-03MW) over RS-485 with an ESP32-C6 and exposes it as a
**Matter-over-Thread Electrical Sensor**. Tested with Home Assistant (Matter Server + Thread border router on a
Home Assistant Connect ZBT-2): totals and per-phase values show up as normal HA sensors, and imported energy works
in the Energy dashboard. No cloud, no Wi-Fi.

- `firmware/`: ESP-IDF + esp-matter application
- KiCad carrier board for a MuseLab nanoESP32-C6 (`rs485-meter.kicad_*`, `fab/`, `scripts/`). **The board has not been
  manufactured or tested yet**; the firmware was verified on a breadboard with an RS485-TTL module.

## Matter data model

| Endpoint | Device type | Clusters | Values |
|---|---|---|---|
| 1 | Electrical Sensor | Power Topology, Electrical Power Measurement, Electrical Energy Measurement | total active power, cumulative imported + exported energy |
| 2 / 3 / 4 | Electrical Sensor | same | L1 / L2 / L3: voltage, active current, active power, cumulative imported energy |

The meter is polled every 2 s. Values are reported to Matter only when they move by at least 0.5 V / 0.05 A / 10 W, and
null only after 3 failed reads in a row. Cumulative energy is published only when it is plausible (never decreasing,
never faster than ~60 kW since the last report, first value confirmed by two reads), because Home Assistant treats a
drop of a `total_increasing` sensor as a counter reset.

## Firmware

Tested with ESP-IDF v6.0.2 and esp-matter `main` (Nov 2026).

```bash
. ~/esp/esp-idf/export.sh && . ~/esp/esp-matter/export.sh
cd firmware
idf.py set-target esp32c6
idf.py build
idf.py -p <port> flash monitor      # nanoESP32-C6: use the CH343 USB-C port
```

Reflash with `flash`, not `erase-flash`, to keep the Matter pairing (fabrics live in NVS).

Pins (UART1, matches the carrier board): TX = GPIO19, RX = GPIO23, DE/RE = GPIO4 (unused with auto-direction modules).
Meter settings are auto-detected at boot (9600/4800/2400/1200 baud x 8E1/8E2/8N1 x FC03/FC04 x word order).

### Commissioning

- **Test credentials:** the firmware uses esp-matter's public development credentials (test DAC, vendor 0xFFF1,
  passcode 20202021, discriminator 3840, manual pairing code `34970112332`). They are not secret and are the same on
  every development device; they only matter while the device is not commissioned. For a per-device passcode, generate
  a factory partition with `esp-matter-mfg-tool` (keep it out of git, see `.gitignore`).
- **Home Assistant:** enable *"Enable test-net DCL usage"* in the Matter Server add-on, otherwise HA rejects the test
  DAC at the end of commissioning.
- **Thread + VLANs:** the phone that commissions the device must reach the Thread network over IPv6, i.e. be on the same
  L2 segment as the Thread border router (or have IPv6 routing to the Thread OMR prefix). A phone on a separate VLAN
  gets through the BLE part and then fails with "unable to add".

### Notes

- `main/matter_project_config.h` raises `CHIP_IM_SERVER_MAX_NUM_DIRTY_SET` from 8 to 24. With 4 endpoints a single poll
  can change ~10 attributes; when the dirty-set pool overflows, the reporting engine merges paths and re-sends the
  whole data model (~14 kB every poll over Thread).
- If the meter is replaced by one with a lower counter, the new readings are rejected as implausible until the ESP reboots.

# Hardware: RS-485 Modbus meter reader – nanoESP32-C6 carrier

2-layer, 100 × 70 mm, 4× M3 (3.2 mm) in the corners. KiCad 10.

## GPIO map (nanoESP32-C6, MuseLab, 2×15 headers)

| Function | GPIO | Devkit pin |
|---|---|---|
| RS-485 RO (UART RX) | 23 | J6-7 |
| RS-485 DI (UART TX) | 19 | J6-11 |
| RS-485 DE + /RE (10k pull-down) | 4 | J1-4 |
| SPI SCK / MOSI / MISO | 6 / 7 / 2 | J1-6 / J1-7 / J6-2 |
| microSD CS | 5 | J1-5 |
| LoRa NSS / DIO1 / RST / BUSY | 18 / 3 / 1 / 0 | J6-12 / J6-3 / J1-9 / J1-8 |
| I2C SDA / SCL (4.7k pull-ups) | 21 / 22 | J6-9 / J6-8 |
| Encoder A / B / switch | 20 / 10 / 11 | J6-10 / J1-11 / J1-12 |
| Mode slide switch | 12 | J1-13 |

Deviations from the original brief, all agreed during design:
- RS-485 moved off GPIO16/17: those are UART0, wired to the devkit's CH343 USB-UART bridge.
- LoRa NSS moved from GPIO8 to GPIO18: GPIO8 is a strapping pin and drives the on-board RGB LED.
- Encoder A moved from GPIO9 to GPIO20: GPIO9 is the BOOT strapping pin.
- GPIO12 (mode switch) is the native-USB D−. The devkit's second (native) USB-C port is not usable; program over the CH343 port.

## Verified on the meter (breadboard, 2026-11-20)

- F&F LE-03MW: Modbus address 1, 9600 baud **8E1**, FC03, FLOAT word order ABCD. Power registers are in kW, energy in kWh. 8E2 garbles the replies, even though the manual lists "parity bits 2".
- Meter terminals: **21 = A, 20 = B** (23/24 are the pulse output). The 120 Ω terminator sits only at the meter end.
- Auto-direction RS485-TTL module (VCC/TX/RX/GND): its pins are labelled from the MCU side. Connect **GPIO19 → module TX** and **GPIO23 → module RX**.
- The carrier PCB is not affected: GPIO19 → MAX3485 DI and GPIO23 ← RO are correct as routed.

## Things to know before ordering

- **5 V back-feed:** the nanoESP32-C6 has only a fuse between USB VBUS and its 5V pin. With USB and the external 5 V terminal connected together, the terminal back-feeds the PC's USB port. Don't connect both at once, or add a diode/ideal-diode on the devkit side.
- **LoRa socket J5 uses a generic pinout:** 3V3, MISO, DIO1, NSS, SCK, MOSI, BUSY, RST, GND, chosen for routing. Check it against your breakout, or wire an adapter. The module envelope is to the right of J5 (about 16.5 × 26 mm). Its antenna must end in the copper-free "ANT" area at the bottom edge.
- **OLED socket J7 is GND-VCC-SCL-SDA.** Some 0.96" modules swap GND and VCC.
- **Encoder A/B have no external pull-ups.** Enable the ESP32 internal pull-ups.
- **CR2032 (BT1) sits under the devkit's USB end.** It is low enough to fit under the USB plug. Replacing it means unplugging the devkit.
- **Copper layers:** signals were autorouted with Freerouting 2.5.0 after the power, RS-485 and GND vias were routed by hand. About 36 % of track length is on B.Cu, so the bottom GND plane has some slots. The top layer also carries a GND pour. Both antenna areas are keepouts on both layers.
- **RS-485 A/B pair** runs together from U1 through the SM712 (D1). It splits only at the 5.08 mm terminal.
- **DRC:** 0 errors, 0 unconnected, schematic parity OK. The 3 remaining warnings are terminal-block silkscreen that reaches past the board edge. The min track width is set to 0.15 mm because Freerouting necks a few traces down to 0.187 mm at pads.

## 3D
`fab/3d/`: STEP/GLB of the carrier and renders with the modules plugged in (`assembled-*.png`). The nanoESP32-C6, OLED, LoRa breakout and EC11 models are simplified VRML stand-ins from `scripts/models3d.py` (`3d/`). They appear in KiCad's 3D viewer and renders but not in the STEP, which only takes STEP models.

## Outputs (`fab/`)
Gerbers + drill (`rs485-meter-gerbers.zip`), BOM, pick-and-place, schematic PDF, top/bottom renders, DRC/ERC reports.

## Regenerating the layout (`scripts/`, KiCad's bundled Python)
`place.py` → `attach3d.py` → `clear.py` → `preroute.py` → autoroute (Freerouting, GND excluded) → `pour.py` → `finish.py` → `silk_clear.py` → `silk.py`
