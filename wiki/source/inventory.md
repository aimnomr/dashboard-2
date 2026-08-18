# Source File Inventory

What each raw file in `wiki/source/` actually is. Originals left in place, unmodified.

| File | What it is | Extracted into |
|---|---|---|
| `13.CANSAT_919MHZ.ino` | Flight unit firmware, **real sensors**. Heltec V3. 14-field CSV @ 1 Hz + SD logging. One-way TX only. | `hardware/flight-unit.md`, `firmware/firmware-versions.md`, `firmware/packet-format.md` |
| `MRC_FlightUnit_V7.ino` | Flight unit firmware, **simulator** — no sensors, generates a synthetic 78 s flight. Heltec V4. 15-field CSV. Two-way: listens for `EJECT`. | `firmware/firmware-versions.md`, `firmware/lora-link-and-protocol.md` |
| `14.GROUND_919MHz.ino` | Ground unit firmware. Heltec V3. Receives LoRa, appends RSSI/SNR, prints to USB serial. One-way RX only. | `hardware/ground-unit.md`, `firmware/firmware-versions.md` |
| `Pins_Assignment.md` | Hand-written pin table. Covers a Heltec V3 sensor stack and an Arduino Nano + RA-01 LoRa unit. | `hardware/flight-unit.md`, `hardware/ground-unit.md` |
| `serial_to_mqtt_V3.py` | PC-side bridge: reads ground unit over USB serial, parses 16-field CSV, publishes JSON to MQTT. | `previous-system/serial-to-mqtt-bridge.md` |
| `CANSAT.json` | Node-RED flow export, 234 nodes. Contains the v1 CanSat dashboard **plus unrelated university lab flows**. | `previous-system/node-red-dashboard.md` |
| `MRC_DualDebug_Flow.json` | Separate Node-RED flow: dual serial debug + two-way comms / EJECT button. | `previous-system/node-red-dashboard.md` |
| `MRC_TwoWay_Flowchart.html` | Rendered flowchart documenting the intended two-way comms sequence end to end. | `firmware/lora-link-and-protocol.md` |
| `CANSAT_DATA` | **SQLite database** (no file extension). 5,481 telemetry readings logged 2026-05-03 → 2026-06-16. | `previous-system/telemetry-database.md` |

## Status of the raw files

The raw files were **removed from `wiki/source/` root on 2026-08-18** and are not present
elsewhere in the repo or under `D:\MRCC`. They were never committed to git, so they are not
recoverable from history.

The extracted documents listed above are now the only record of their contents here. See
`ISS-09` — the loss that matters is `CANSAT_DATA`, whose 5,481 rows of real telemetry are not
reproducible from documentation.
