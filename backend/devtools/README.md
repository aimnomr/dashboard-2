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

## What it simulates

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

- The raw log filename ends in `-mock.log`, so it can never be mistaken for a real
  flight later.
- The `session` and every `frame` envelope carry `"simulated": true`.
- The UI shows an unmissable banner.

The worst outcome available in this project is somebody watching a simulated flight and
believing it.
