# Telemetry Database — `CANSAT_DATA`

Source: `wiki/source/CANSAT_DATA` (SQLite 3, ~1.0 MB, no file extension)

Written by the v1 Node-RED flow. Contains **real logged telemetry** — the only actual flight
data in the source set.

## Schema

```sql
CREATE TABLE sessions (
    session_id  INTEGER PRIMARY KEY AUTOINCREMENT,
    start_time  DATETIME NOT NULL DEFAULT (datetime('now','localtime')),
    label       TEXT,
    notes       TEXT
);

CREATE TABLE readings (                       -- 1 row per packet
    reading_id  INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL,
    timestamp   DATETIME NOT NULL DEFAULT (datetime('now','localtime')),
    temp REAL, hum REAL, pres REAL, alt REAL, spd REAL, sat INTEGER,
    FOREIGN KEY (session_id) REFERENCES sessions(session_id)
);

CREATE TABLE imu_data (                       -- FK → readings, ON DELETE CASCADE
    imu_id INTEGER PRIMARY KEY AUTOINCREMENT, reading_id INTEGER NOT NULL,
    ax REAL, ay REAL, az REAL, gx REAL, gy REAL, gz REAL
);

CREATE TABLE gps_data (                       -- FK → readings, ON DELETE CASCADE
    gps_id INTEGER PRIMARY KEY AUTOINCREMENT, reading_id INTEGER NOT NULL,
    lat REAL, lng REAL
);

CREATE TABLE signal_data (                    -- FK → readings, ON DELETE CASCADE
    signal_id INTEGER PRIMARY KEY AUTOINCREMENT, reading_id INTEGER NOT NULL,
    rssi REAL, snr REAL
);

CREATE INDEX idx_gps_reading    ON gps_data (reading_id);
CREATE INDEX idx_signal_reading ON signal_data (reading_id);
```

The 16 telemetry fields are split across four tables, one-to-one, joined on `reading_id`.
Note `imu_data` has no index on `reading_id` while `gps_data` and `signal_data` do.

## Contents

| Table | Rows |
|---|---|
| `sessions` | 1 |
| `readings` | 5,481 |
| `imu_data` | 5,231 |
| `gps_data` | 5,231 |
| `signal_data` | 4,498 |

**Time span:** `2026-05-03 15:17:22` → `2026-06-16 11:59:22` — six weeks.
**Altitude range:** −14.3 m to 500.9 m.

## Observations

**It is not a flight log.** 5,481 readings at 1 Hz is ~91 minutes of packets, but they are
spread over six weeks. This is accumulated bench and field testing, all appended to a single
session row labelled `Default Session` / `Auto-created on first run`. The `sessions` table
exists but was never used to separate runs — so every Data Analytics query in the v1 dashboard
aggregates across every test ever run, including bench idling.

**Child rows don't match parent rows.** Verified by left join:

- **256** readings have no `imu_data` row, and the same 256 have no `gps_data` row.
- **989** readings have no `signal_data` row.
- **6** `reading_id`s have *more than one* child row in each child table — duplicate inserts.

Cause not recorded — likely inserts that failed or double-fired after the parent row landed,
since each packet took a `last_insert_rowid()` round-trip plus three more inserts to store.
The one-to-one assumption the schema implies does not hold in practice, so any query joining
these tables must handle both missing and duplicated children.

**250 readings have NULL measurements**, including the first rows (`reading_id` 1–3, at
`2026-05-03 15:17:22`). Rows were being created before parsing worked.

**Link quality actually observed** (from `signal_data`):

| | min | max | mean |
|---|---|---|---|
| RSSI (dBm) | −124.0 | −14.0 | −55.6 |
| SNR (dB) | −10.75 | 14.0 | 9.33 |

An RSSI floor of −124 dBm is at the edge of SX1262 sensitivity for SF7/BW125 — packets were
being received right at the limit, which is consistent with the missing `signal_data` rows.

**`sat` includes NULL and 0** alongside real fixes of 3–12 satellites.

**Timestamps are PC arrival times.** The `DEFAULT (datetime('now','localtime'))` means time is
stamped when the row is inserted, not when the sample was taken. Second-resolution only, local
timezone, no UTC offset stored. Combined with the absence of any onboard clock or packet
counter (see `../firmware/packet-format.md`), there is no way to recover true sampling time or
detect a dropped packet from this data.

**Altitude goes negative** (−14.3 m), consistent with altitude being relative to boot altitude
and drifting with barometric pressure over a long session.

## Value for v2

Despite the caveats this is a genuine multi-thousand-row sample of the real signal
characteristics — RSSI/SNR distributions, sensor noise, GPS behaviour. Useful as test input
for a parser and as a realistic feed for developing the UI without hardware on the desk.
