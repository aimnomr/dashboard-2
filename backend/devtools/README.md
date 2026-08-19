# devtools — NOT FOR LAUNCH

Development tooling. Nothing in this package is used at the launch site, and nothing
in `dashboard/` imports from it.

## Why it is separate

The launch entry point (`python -m dashboard`) reads from the serial port and has **no
flag that can select simulated data**. Choosing mock telemetry requires deliberately
running a different module from a different package — it cannot be reached by fumbling
an option at the pad.

Dependency direction is one-way: `devtools` imports `dashboard`, never the reverse.

## Running the mock

```bash
python -m devtools.run_mock
```

Then open http://127.0.0.1:8000 (or run the Vite dev server against it).

| Flag | Effect |
|---|---|
| `--interval 0.2` | Speed the flight up for faster UI iteration |
| `--once` | Stop after one 78 s flight, as the real firmware does |
| `--clean` | No malformed lines, no `[GCS]` status lines |
| `--seed 42` | Reproducible flight, for tests and comparisons |

## Replaying a captured flight

```bash
python -m devtools.run_replay FLIGHT21.CSV --speed 8 --hold
```

Feeds a real SD capture through the real pipeline at the rate the vehicle produced it.
A bare filename is searched for in `logs/raw/` and then `backend/tests/fixtures/`.

| Flag | Effect |
|---|---|
| `--speed 8` | Playback multiplier. Above ~x60 Windows' ~15 ms timer granularity dominates |
| `--hold` | Keep the dashboard up after the capture ends, instead of exiting with it |
| `--loop` | Repeat. Each pass restarts the vehicle clock and `seq`, as a reboot would |
| `--interval 0.2` | Fixed gap, ignoring the vehicle clock entirely |

Pacing comes from the capture's own `ms` field rather than a chosen interval, so the
replay reproduces the cadence the vehicle actually ran at, irregularities included.

**Replay adds no RSSI or SNR.** Those are measured by the ground station's radio as a
packet arrives; a packet read from a file crossed no radio, so there is no measurement
and both stay `null`. The status bar renders them as `—`. Inventing a plausible dBm
figure would be a fabricated measurement on the operator's screen, which is the failure
this project is organised against.

Two consequences worth expecting before the dashboard looks broken:

- **The captures in `tests/fixtures/` are bench runs, not flights.** Altitude stays
  within ±1.2 m, the GPS never gets a fix (ISS-14), and the chute is never commanded.
  They test parsing and transport, not the flight display.
- **There is no loss figure yet.** The pipeline does not carry `seq`, `vehicle_ms` or
  `crc_ok` until steps 2–3 of `wiki/decisions/dashboard-gen3-plan.md` are built, so
  charts still run on the PC arrival clock.

## What the mock simulates

`MockSource` stands in for the CanSat, the LoRa link, the ground station and the USB
hop — everything above the PC. It emits GEN2 lines with RSSI/SNR already appended, so
the raw log, parser and WebSocket layer downstream are all the real launch code.

The flight profile is a port of `MRC_FlightUnit_V7.ino`: eight phases, 78 seconds,
apogee 150 m. Full detail in `wiki/source/firmware/lora-link-and-protocol.md`.

`EJECT` sent from the UI sets the chute flag to 1, so the uplink path is exercisable
without hardware — useful given ISS-02, where the ground unit firmware that would
receive the real command does not exist yet.

## The feed is deliberately imperfect

By default roughly 2% of lines are malformed (truncated, a dropped field, or garbled
bytes) and about 4% of intervals carry a `[GCS] Timeout - no packet` status line
instead of a packet.

This is intentional. A UI built against a clean feed is a UI that has never been tested
against the feed it will actually get. Use `--clean` only when isolating something else.

## Every simulated run is labelled

- The raw log filename ends in `-mock.log` or `-replay.log`, so it can never be mistaken
  for a real flight later.
- The `session` and every `frame` envelope carry `"simulated": true`.
- The UI shows an unmissable banner.

The worst outcome available in this project is somebody watching a simulated flight and
believing it.

Replay is the sharper version of that risk, because the data genuinely is real — it came
off the vehicle. Only the liveness is false. That is exactly why `FileSource` sets
`simulated = True` despite carrying authentic telemetry, and why the `EJECT` control
reports failure during a replay rather than appearing to work.
