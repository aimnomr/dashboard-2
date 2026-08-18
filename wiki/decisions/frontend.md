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

### EJECT is not a display control

It fires a parachute. Requirements:

- An explicit **armed state** or confirmation step. A stray click must not deploy.
- **Sent ≠ deployed.** The link carries no acknowledgement. The UI shows that the command was
  transmitted; actual deployment is confirmed only indirectly, by `CHUTE:1` appearing in later
  telemetry. Showing "deployed" on button press would be a lie the operator may act on.
- `ISS-02` — the ground unit firmware that receives this command does not exist yet, so the path
  cannot be tested end to end against hardware. It can be exercised against the mock server.

## Mock feed

A small Python server implements the GEN2 simulator's 8-phase flight profile (documented in
`../source/firmware/lora-link-and-protocol.md`) and serves it over WebSocket at 1 Hz.

Chosen over a TypeScript port so the profile has **one implementation**, the frontend always
talks to a real socket with no mock/live branching, and the same tool later serves as a backend
test fixture. It also accepts the EJECT command, so that path is exercisable without hardware.

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
