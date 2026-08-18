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

## Note on organisation

The raw files were left at `wiki/source/` root rather than moved into the subdirectories, because
moving them removes them from their original path and all deletion/relocation is the user's action.
Proposed moves are listed at the end of the session notes if you want them tidied.
