# modbus-matter-meter

Reads a 3-phase Modbus RTU energy meter (F&F LE-03MW) over RS-485 with an ESP32-C6 and exposes it as a
**Matter-over-Thread Electrical Sensor**. Tested with Home Assistant (Matter Server + Thread border router on a
Home Assistant Connect ZBT-2): totals and per-phase values show up as normal HA sensors, and imported energy works
in the Energy dashboard. No cloud, no Wi-Fi.

- `firmware/`: ESP-IDF + esp-matter application
- [`docs/wiring.html`](https://serusko.github.io/modbus-matter-meter/docs/wiring.html): animated wiring diagram and step-by-step how-to for the breadboard build

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

Pins (UART1): TX = GPIO19, RX = GPIO23, DE/RE = GPIO4 (unused with auto-direction modules).
Meter settings are auto-detected at boot (9600/4800/2400/1200 baud x 8E1/8E2/8N1 x FC03/FC04 x word order).

### Commissioning

- **Test credentials:** the firmware uses esp-matter's public development credentials (test DAC, vendor 0xFFF1,
  passcode 20202021, discriminator 3840, manual pairing code `34970112332`, QR payload `MT:Y.K9042C00KA0648G00`; the
  QR code is on the [wiring page](https://serusko.github.io/modbus-matter-meter/docs/wiring.html)). They are not secret and are the same on
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

## Hardware

Verified on a breadboard (20 Nov 2026):

- **ESP32-C6:** MuseLab nanoESP32-C6 (16 MB). Flash and power through the USB-C port marked CH343.
- **RS485-TTL module** with automatic direction (VCC/TX/RX/GND), powered from **3V3**, not 5 V. Its pins are labelled
  from the MCU side: **GPIO19 → module TX**, **GPIO23 → module RX**.
- **F&F LE-03MW:** Modbus address 1, 9600 baud **8E1**, FC03, FLOAT word order ABCD; power registers in kW, energy in
  kWh. 8E2 garbles the replies even though the manual lists "parity bits 2". Terminals **21 = A, 20 = B** (23/24 are the
  pulse output). 120 Ω terminator at the meter end only.

With a bare MAX3485 instead of an auto-direction module, drive DE/RE from GPIO4 (the firmware already sets it as the
UART RTS pin).
