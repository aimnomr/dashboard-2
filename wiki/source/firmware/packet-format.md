# Telemetry Packet Format

Source: `13.CANSAT_919MHZ.ino` (GEN1), `MRC_FlightUnit_V7.ino` (GEN2),
`14.GROUND_919MHz.ino`, `serial_to_mqtt_V3.py`, `MRC_TwoWay_Flowchart.html`

> **Decided:** the **GEN2 packet is the format the dashboard targets.** GEN1 is the one-way
> build; GEN2 is the two-way build and its packet is derived from GEN1 with the chute flag
> added. See `ISS-01` in `wiki/issues.md`.

Plain comma-separated values. **No start marker, no packet counter, no timestamp, no checksum**
in either generation. Framing is the newline from `Serial.println()` alone — see `ISS-08`.

---

## Canonical format — GEN2, 15 fields from the flight unit

`MRC_FlightUnit_V7.ino`:

```
%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.6f,%.6f,%.2f,%d,CHUTE:%d
```

| # | Field | Unit | Format | Notes |
|---|---|---|---|---|
| 1 | `temp` | °C | `%.2f` | |
| 2 | `hum` | % RH | `%.1f` | |
| 3 | `pres` | hPa | `%.2f` | |
| 4 | `alt` | m | `%.2f` | relative to boot altitude, not AMSL |
| 5 | `ax` | g | `%.3f` | |
| 6 | `ay` | g | `%.3f` | |
| 7 | `az` | g | `%.3f` | |
| 8 | `gx` | °/s | `%.2f` | |
| 9 | `gy` | °/s | `%.2f` | |
| 10 | `gz` | °/s | `%.2f` | |
| 11 | `lat` | deg | `%.6f` | |
| 12 | `lng` | deg | `%.6f` | |
| 13 | `spd` | km/h | `%.2f` | |
| 14 | `sat` | count | `%d` | |
| 15 | `chute` | flag | `CHUTE:%d` | **literal prefix**, `CHUTE:0` / `CHUTE:1` — see `ISS-07` |

Payload buffer 180 bytes.

## Ground unit appends 2 fields → **17 fields at the PC**

`14.GROUND_919MHz.ino` re-emits the received string with link quality appended:

```
<received payload>,<rssi %.1f>,<snr %.2f>
```

| # | Field | Unit | Source |
|---|---|---|---|
| 16 | `rssi` | dBm | ground unit, `radio.getRSSI()` |
| 17 | `snr` | dB | ground unit, `radio.getSNR()` |

Output buffer 160 bytes — note this is **smaller than the flight unit's 180-byte payload
buffer**, so a maximum-length packet plus appended RSSI/SNR could be truncated by `snprintf`.

`MRC_TwoWay_Flowchart.html` confirms the target: *"Parse serial line — 17-field CSV validation"*.

---

## GEN1 format — 14 fields, for reference

`13.CANSAT_919MHZ.ino`, the one-way build. Same fields 1–14, **lower precision**, no chute flag:

```
%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.5f,%.5f,%.1f,%d
```

Becomes 16 fields at the PC after the ground unit appends RSSI/SNR. This is what the v1 SD card
logs and the `CANSAT_DATA` database contain, and what `serial_to_mqtt_V3.py` was written for.

**Precision differs between generations** — GEN2 carries an extra decimal place on most fields
and two extra on lat/lng (≈1.1 m → ≈0.11 m of GPS resolution). Parsers must not assume GEN1
precision when reading GEN2 data.

## Consequence: the v1 bridge cannot read GEN2

`serial_to_mqtt_V3.py` hard-requires exactly 16 fields and casts every one with `float()`.
Against GEN2 it fails twice over — 17 fields is rejected as malformed, and `float("CHUTE:1")`
raises `ValueError` regardless. The v2 ingest must handle both, and must strip the `CHUTE:`
prefix before parsing field 15.

## PC-side field names (v1, GEN1 only)

```python
["temp","hum","pres","alt","ax","ay","az","gx","gy","gz","lat","lng","spd","sat","rssi","snr"]
```

## Expected value ranges

Clamps applied in `MRC_FlightUnit_V7.ino` — useful as sanity-check bounds:

```
temp  15 … 50 °C          alt   0 … 500 m
hum   30 … 100 %          accel −16 … +16 g
pres  900 … 1050 hPa      gyro  −250 … +250 °/s
spd   0 … 400 km/h        sat   5 (pre-launch/boost) or 9
```

Base coordinates: `3.07830, 101.71220`.

Observed in real logged data (`CANSAT_DATA`): altitude −14.3 … 500.9 m, RSSI −124 … −14 dBm,
SNR −10.75 … 14 dB, `sat` 0–12 plus NULL.

## Known gaps

Tracked as `ISS-08`. In summary: no packet counter (loss is invisible), no onboard timestamp
(time is stamped on PC arrival), no checksum (nothing covers the USB serial hop).
