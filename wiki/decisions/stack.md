# Stack

Decided 2026-08-18.

| Layer | Choice |
|---|---|
| Backend | **Python** — FastAPI + uvicorn, `pyserial` |
| Transport to UI | **WebSocket**, plus REST for history and commands |
| Frontend | **React + Vite** |
| Charts | **uPlot** (proposed) |
| Map | **Leaflet** with offline tiles (proposed) — see `ISS-11` |
| Raw log | append-only text file, written before parsing |
| Parsed store | **SQLite, single flat table** |
| Environment | `venv` + `requirements.txt` |

## Why Python + browser

The team is strongest in Python. At 1 Hz and 17 fields performance is irrelevant — a 30-minute
flight is ~1,800 packets — so the decision was made on **field reliability** and
**maintainability**, not throughput.

- **One process to start.** The backend owns the serial port, writes both logs, and serves the
  built frontend. The browser is only a viewer. v1 needed three processes up before a single
  byte flowed (MQTT broker + Node-RED + Python bridge); that is three things to get wrong at the
  pad.
- **`pyserial` is battle-tested on Windows COM ports**, with no native build step.
- **The backend owns the file handle**, which is what makes the raw-log-first rule enforceable
  across a parser exception or a UI crash.
- **`pandas` for post-flight analysis** comes free.
- **Replay stays cheap** — swap the source, everything downstream is unchanged.

Accepted cost: Python must be present on the field laptop (`ISS-12`), and the repo holds two
languages.

Rejected: **Node** (`serialport` is a native module — Windows build tools, breaks on version
bumps), **browser-only Web Serial** (needs a manual port + folder click-through every run, and a
closed tab kills logging mid-flight), **Tauri** (a Rust toolchain to maintain, for 1 Hz of CSV).

## Proposed repository layout

```
backend/
├── dashboard/
│   ├── __main__.py        entry point:  python -m dashboard
│   ├── config.py          port, baud, paths — nothing hardcoded
│   ├── sources/
│   │   ├── base.py        the seam: yields raw lines
│   │   ├── serial_source.py
│   │   └── file_source.py    replay — not built yet
│   ├── rawlog.py          write-first, flush, never gated on validity
│   ├── parser.py          GEN2 line → Frame
│   ├── store.py           SQLite, single flat table
│   ├── uplink.py          EJECT command out
│   └── api.py             FastAPI: WebSocket + REST + static
├── tests/
└── requirements.txt

frontend/
├── src/
│   ├── panels/            LinkHealth, AltitudeChart, GpsMap, Attitude, EjectControl
│   ├── hooks/useTelemetry.ts
│   └── App.tsx
└── package.json

logs/                      raw + SQLite, gitignored
```

## Running it

**Development** — two processes: `uvicorn` with reload, and the Vite dev server proxying to it.

**Field** — one process. The frontend is built ahead of time and FastAPI serves the static
bundle, so the operator runs `python -m dashboard` and opens a browser. **The build must be done
before leaving for the launch site** — `npm run build` needs the network. See `ISS-12`.

## Storage

**Raw log** — every byte off the serial port, appended and flushed before any parsing is
attempted. No filtering, no validity gate. Includes `[GCS]` status lines and malformed lines.

**Parsed store** — SQLite, **one flat table, one INSERT per packet**.

This is a deliberate correction of v1. Its schema split each packet across four tables joined on
`reading_id`, requiring a `last_insert_rowid()` round-trip plus three more inserts per packet.
The observed cost in `CANSAT_DATA`: 256 readings with no IMU/GPS row, 989 with no signal row,
and 6 `reading_id`s with duplicated children. A single flat insert makes all three failures
structurally impossible. At ~2,000 rows per flight there is no reason to normalise.

Columns follow the GEN2 packet plus a session id, PC arrival time, and a parse-status flag so
malformed lines are recorded rather than silently dropped.

## Frontend notes

**Charts — uPlot.** Canvas-based and very fast. The dashboard will run 9+ live series updating
every second across a 30+ minute flight, which is ~1,800 points per series; SVG-based libraries
such as Recharts get sluggish at that scale. uPlot needs a thin React wrapper, which is a small
one-off cost.

**Attitude.** A 2D artificial-horizon style indicator on canvas first. Legible, cheap, and
sufficient for MPU6050 data. 3D is a later question, not a first-version need.

**EJECT is not a display control — it fires a parachute.** It needs an explicit armed state or
confirmation step so it cannot be triggered by a stray click, and it must show that the command
was *sent* separately from whether it was *acted on* — the link has no acknowledgement, so
confirmation only ever arrives indirectly via `CHUTE:1` in later telemetry.

## Still open

- Whether flight logs are committed to the repo or stay local (currently proposed: gitignored).
- Offline map tiles — `ISS-11`.
- Field laptop provisioning — `ISS-12`.
