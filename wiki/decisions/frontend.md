# Frontend

Decided 2026-08-18.

| | |
|---|---|
| Layout | **Single fixed screen, no scrolling** |
| Theme | **High-contrast light, sun-first** |
| Panels | All nine, in the first version |
| Mock feed | **Python mock server over WebSocket** |
| Wire format | Envelope carrying raw line **and** parsed frame — proposed, see below |

## Layout — one screen, nothing hidden

```
┌─ STATUS BAR ───────────────────────────────────────────────┐
│ ● LIVE  1.2s ago    RSSI −55 dBm   SNR 9.3 dB   CHUTE: ARMED│
├──────────────────────┬──────────────────┬──────────────────┤
│  ALTITUDE            │  GROUND TRACK    │  ATTITUDE        │
│    150.2 m           │                  │                  │
│                      ├──────────────────┤  SPEED           │
│                      │  GPS READOUT     │    8.2 km/h      │
├──────────────────────┴────────┬─────────┴──────────────────┤
│  ENVIRONMENT  T / H / P       │ [ EJECT ] │  RAW FEED       │
└───────────────────────────────┴───────────┴─────────────────┘
```

Everything visible at once. Nothing behind a tab or below the fold at the moment it matters —
during descent the operator has about one second to read the vehicle's state.

Accepted cost: it is dense, and it has little room to grow without a redesign. A separate
analysis view is the natural home for tables and post-flight aggregates if those are wanted
later; it is not part of the live screen.

## Theme — designed for sunlight, not for screenshots

The dashboard runs on a laptop, outdoors, in Malaysia. Glare washes out low-contrast greys, so
the usual dark "mission control" aesthetic is actively wrong for the operating environment.

Rules:

- Near-black on near-white. Maximum contrast.
- Heavy weights, large numerals for flight-critical values. Tabular figures so digits do not
  shift as values change.
- **No information carried by colour alone** — every state also has an icon, a shape, or a word.
  This covers both glare and colour vision deficiency.
- All colour via CSS custom properties, so a dark variant remains possible later without
  reworking components.

Accepted cost: harsh indoors during development, and less impressive in a demo.

## Panels

All nine are in scope for the first version.

| Panel | Fields | Notes |
|---|---|---|
| Link health | `rssi`, `snr`, arrival time | See the hard constraint below |
| Altitude | `alt` | Primary chart. Current value large, plus max reached |
| Chute state | `chute` | ARMED / DEPLOYED, high prominence |
| EJECT control | uplink | Armed state required — see below |
| Ground track | `lat`, `lng` | Plain XY trace, launch point marked, scale bar. No basemap (`ISS-11`) |
| GPS readout | `lat`, `lng`, `sat` | Numeric; satellite count doubles as a fix-quality indicator |
| Attitude | `ax…gz` | 2D artificial horizon plus gyro rates. Distinguishes tumble from stable descent |
| Speed | `spd` | Descent rate sanity check |
| Environment | `temp`, `hum`, `pres` | Secondary. `pres` cross-checks `alt` |
| Raw feed | raw lines | Scrolling last N lines, malformed ones marked. Unglamorous, invaluable in the field |

### Hard constraint: link health cannot show packet loss

There is no packet counter (`ISS-08`). The panel may show **time since last packet**, **RSSI**
and **SNR** — and nothing else. It must not display a loss percentage, a gap count, or a
"packets missed" figure, because those cannot be computed from this data and a fabricated one
would be acted on.

A stale link must be **loud**. A dashboard that silently stops updating looks identical to a calm
one, and that is the most dangerous failure mode on the screen.

### Attitude is a 2D horizon, drawn on canvas

Pitch and roll from the accelerometer, drawn as an artificial horizon, with gyro rates
alongside. No library.

**The reading is labelled unreliable when it is.** An accelerometer measures gravity
plus vehicle acceleration, so it is only an attitude reference when the vehicle is not
accelerating. Under boost it is measuring thrust; at apogee there is no gravity vector
to measure at all. In those states, and while tumbling above 90 °/s, the horizon
desaturates and the panel states the reason rather than drawing a confident attitude
from meaningless numbers.

Note also that the accelerometer gives **two degrees of freedom, not three** — it fixes
tilt but cannot determine rotation about the gravity vector, and there is no
magnetometer, so there is no heading reference on this vehicle at all.

#### A 3D pose was tried and reverted

Built with Three.js on 2026-08-18 and reverted the same day as not feasible — see devlog
013 and 014. Recorded here so it is not proposed again without new information.

The argument for it was real: a 2D horizon was designed for aircraft, which live in a
narrow band of pitch and roll, and it degenerates for a free-falling body that can be at
any orientation. The argument against was practicality — a WebGL dependency, a model
pipeline, and a placeholder to maintain, in exchange for a display whose third axis was
never measured in the first place.

If it is revisited, the unmeasured-rotation problem is the thing to solve first, not the
rendering.

### EJECT is not a display control

It fires a parachute. Requirements:

- An explicit **armed state** or confirmation step. A stray click must not deploy.
- **Sent ≠ deployed.** The link carries no acknowledgement. The UI shows that the command was
  transmitted; actual deployment is confirmed only indirectly, by `CHUTE:1` appearing in later
  telemetry. Showing "deployed" on button press would be a lie the operator may act on.
- `ISS-02` — the ground unit firmware that receives this command does not exist yet, so the path
  cannot be tested end to end against hardware. It can be exercised against the mock server.

## Mock feed

The mock is a **source**, plugged into the seam at the very top of the pipeline
(`backend/devtools/mock_source.py`). It stands in for the CanSat, the LoRa link, the ground
station and the USB hop — everything above the PC. It implements the GEN2 simulator's 8-phase
flight profile documented in `../source/firmware/lora-link-and-protocol.md`.

Everything downstream — raw log, parser, WebSocket — is the **real launch code** in both paths,
so developing against the mock exercises the code that will fly. A standalone mock server would
have needed its own envelope builder, meaning a second parser free to drift from the real one.

**Dev and launch are separated by entry point, not by a flag.** `python -m dashboard` reads the
serial port and has no option that selects simulated data; the mock requires deliberately running
`python -m devtools.run_mock`. Dependency direction is one-way: `devtools` imports `dashboard`,
never the reverse.

The mock accepts EJECT and flips the chute flag, so that path is exercisable without hardware —
useful given `ISS-02`.

**The feed is deliberately imperfect**: ~2% malformed lines and ~4% `[GCS]` status lines, with
`--clean` to disable. A UI built against a perfect feed is a UI that has never been tested
against the feed it will actually get.

This partly answers `ISS-09`: the real telemetry was lost, but a faithful substitute can be
regenerated from the documentation.

## Wire contract (proposed)

The socket carries an envelope with **both** the raw line and the parsed frame. Parsing happens
server-side only — a frontend parser would be a second implementation, free to drift.

```jsonc
{ "type": "frame", "rx_index": 42, "pc_time": "2026-08-18T10:22:01.123Z",
  "raw": "32.50,78.0,...,CHUTE:0,-55.6,9.33", "ok": true,
  "frame": { "temp": 32.5, "…": "…", "chute": 0, "rssi": -55.6, "snr": 9.33 } }

{ "type": "frame", "rx_index": 43, "raw": "32.5,78.0,BAD", "ok": false,
  "frame": null, "error": "expected 17 fields, got 3" }

{ "type": "status", "raw": "[GCS] Timeout - no packet" }

{ "type": "command", "command": "eject" }                    // client → server
{ "type": "command_ack", "command": "eject", "sent": true }  // server → client
```

Deliberate choices:

- **`rx_index` is named for what it is** — lines arrived, not packets sent. It cannot detect
  loss. Naming it `seq` or `packet_id` would invite a false loss indicator built on top of it.
- **`sent` means sent.** Not received, not deployed.
- **Malformed lines are transmitted, not dropped**, so the raw feed can show corruption. Matches
  the raw-log-first principle in `ingest-pipeline.md`.
