# 003 · Extract source material into the wiki

**Date** 2026-08-18
**Type** investigation
**Refs** ISS-01 … ISS-05

## What

Read the nine unsorted files dropped into `wiki/source/` — two flight unit firmware builds, a
ground unit build, a pin sheet, a serial→MQTT bridge, two Node-RED flow exports, a flowchart,
and a 1 MB file with no extension that turned out to be a SQLite database.

Wrote nine documents:

```
wiki/source/inventory.md
wiki/source/hardware/flight-unit.md
wiki/source/hardware/ground-unit.md
wiki/source/firmware/packet-format.md
wiki/source/firmware/lora-link-and-protocol.md
wiki/source/firmware/firmware-versions.md
wiki/source/previous-system/node-red-dashboard.md
wiki/source/previous-system/telemetry-database.md
wiki/source/previous-system/serial-to-mqtt-bridge.md
```

Added `wiki/source/previous-system/` — the v1 Node-RED/MQTT/SQLite stack is neither hardware
nor firmware.

## Why

The material was unusable as dropped: mixed generations, no index, and a database with no file
extension. Decisions needed a stable reference.

## Result

Established the radio parameters (919.0 MHz, BW 125 kHz, SF7, CR 4/5, sync `0xAB`, 17 dBm,
SX1262/RadioLib, 1 Hz), the full field layout of both packet generations, the two-way `EJECT`
protocol, and the v1 architecture.

Surfaced five conflicts, raised as ISS-01 through ISS-05.

`CANSAT_DATA` proved to be SQLite with 5,481 readings spanning 2026-05-03 → 2026-06-16.
Verified rather than assumed: 256 readings have no IMU/GPS row, 989 have no signal row, 6
`reading_id`s have duplicate children, and 250 have NULL measurements. Observed RSSI −124 to
−14 dBm, at the edge of SX1262 sensitivity for SF7/BW125.

Two initial counts (250 and 983) were stated before verification and corrected to 256 and 989
once checked by left join — the raw count difference had hidden the duplicate child rows.

Raw files were left in place unmodified; relocating them is Aiman's action.
