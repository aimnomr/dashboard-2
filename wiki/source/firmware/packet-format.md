# Telemetry Packet Format

Source: `13.CANSAT_919MHZ.ino`, `MRC_FlightUnit_V7.ino`, `14.GROUND_919MHz.ino`,
`serial_to_mqtt_V3.py`, `MRC_TwoWay_Flowchart.html`

Plain comma-separated values. **No start marker, no packet counter, no timestamp, no checksum**
in any version. Framing is the newline from `Serial.println()` alone.

## Field layout — flight unit transmits 14 fields

From `13.CANSAT_919MHZ.ino` (the real-sensor build):

| # | Field | Unit | Format | Notes |
|---|---|---|---|---|
| 1 | `temp` | °C | `%.1f` | BME280 |
| 2 | `hum` | % RH | `%.1f` | BME280 |
| 3 | `pres` | hPa | `%.1f` | BME280 |
| 4 | `alt` | m | `%.1f` | **relative to boot altitude**, not AMSL |
| 5 | `ax` | g | `%.2f` | offset-corrected |
| 6 | `ay` | g | `%.2f` | offset-corrected |
| 7 | `az` | g | `%.2f` | **not** offset-corrected |
| 8 | `gx` | °/s | `%.1f` | offset-corrected |
| 9 | `gy` | °/s | `%.1f` | offset-corrected |
| 10 | `gz` | °/s | `%.1f` | offset-corrected |
| 11 | `lat` | deg | `%.5f` | `0.0` when GPS invalid |
| 12 | `lng` | deg | `%.5f` | `0.0` when GPS invalid |
| 13 | `spd` | km/h | `%.1f` | offset-corrected, floored at 0 |
| 14 | `sat` | count | `%d` | `0` when GPS invalid |

Payload buffer is 128 bytes.

## Ground unit appends 2 fields

`14.GROUND_919MHz.ino` re-emits the received string with link quality appended:

```
<received payload>,<rssi %.1f>,<snr %.2f>
```

Output buffer is 160 bytes. So the PC sees **16 fields** from the real firmware.

## ⚠️ The simulator emits a 15th field — and it breaks the chain

`MRC_FlightUnit_V7.ino` uses different precision *and* appends a chute flag:

```
%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.6f,%.6f,%.2f,%d,CHUTE:%d
```

Field 15 is the literal text `CHUTE:0` or `CHUTE:1` — **not a bare number**.

This produces a live inconsistency across the source set:

| Producer | Fields at PC | `serial_to_mqtt_V3.py` (expects 16) |
|---|---|---|
| `13.CANSAT_919MHZ.ino` + ground unit | 16 | ✅ parses |
| `MRC_FlightUnit_V7.ino` + ground unit | 17 | ❌ rejected as malformed |

`MRC_TwoWay_Flowchart.html` documents the intended pipeline as **"17-field CSV validation"**,
matching the simulator. `serial_to_mqtt_V3.py` was written for the 16-field real firmware and
would drop every simulator packet.

Even if the count were fixed, `float("CHUTE:1")` raises `ValueError` — the bridge's
`parse_line()` would still reject it. The `CHUTE:` prefix needs stripping before parsing.

## PC-side field names

`serial_to_mqtt_V3.py` names the 16 fields:

```python
["temp","hum","pres","alt","ax","ay","az","gx","gy","gz","lat","lng","spd","sat","rssi","snr"]
```

All parsed as `float`; `sat` cast to `int` afterwards.

## Simulator value ranges

`MRC_FlightUnit_V7.ino` clamps its synthetic output — useful as expected-range reference:

```
temp  15 … 50 °C          alt   0 … 500 m
hum   30 … 100 %          accel −16 … +16 g
pres  900 … 1050 hPa      gyro  −250 … +250 °/s
spd   0 … 400 km/h        sat   5 (pre-launch/boost) or 9
```

Base coordinates: `3.07830, 101.71220`.

## Gaps

- **No packet counter** — packet loss is invisible; a dropped packet is indistinguishable from
  a dead transmitter.
- **No onboard timestamp** — time is assigned by the PC on receipt
  (see `previous-system/telemetry-database.md`), so it reflects arrival, not sampling.
- **No checksum** — LoRa's own CRC covers the RF hop, but nothing covers the USB serial hop
  or a truncated `Serial.println`.
- **No field for chute state in the real firmware** — the flight unit that has actual sensors
  has no deployment mechanism and no two-way capability.
