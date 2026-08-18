# 009 · Backend spine and mock source

**Date** 2026-08-18
**Type** change
**Refs** ISS-02, ISS-05, ISS-07, ISS-08, ISS-12

## What

Built the ingest spine: `source → raw log → parser → WebSocket`.

```
backend/
├── dashboard/              LAUNCH CODE
│   ├── __main__.py         python -m dashboard --port COM12
│   ├── sources/base.py     the seam
│   ├── sources/serial_source.py
│   ├── rawlog.py           flush + fsync every line
│   ├── parser.py           GEN2 canonical, GEN1 tolerated
│   ├── hub.py              WebSocket fan-out
│   ├── pipeline.py         ordering: raw log BEFORE parse
│   ├── api.py              WebSocket, session, uplink command
│   └── runner.py           wiring shared by both entry points
├── devtools/               DEV ONLY — imports dashboard, never the reverse
│   ├── mock_source.py      8-phase flight, port of MRC_FlightUnit_V7.ino
│   ├── run_mock.py         python -m devtools.run_mock
│   └── README.md
└── tests/test_parser.py
```

Supersedes part of entry 008: the mock is a **source** plugged into the existing seam,
not a standalone mock server. 008 recorded the standalone design.

## Why

Putting the mock at the very top of the pipeline means the raw logger, parser and
transport are the real launch code in both paths. A standalone mock server would have
needed its own envelope builder — a second parser, free to drift, which is the v1
failure mode in a new form.

Dev and launch are separated by entry point and package, not by a flag. `python -m
dashboard` has **no option that selects simulated data**; reaching the mock requires
deliberately running a different module. A `--source mock` flag is exactly the kind of
thing that gets fumbled at the pad.

## Result

**Verified working.** 17 parser tests pass. End-to-end smoke test over a real
WebSocket: session message, 41 frames, `[GCS]` status lines passed through, EJECT
round-trip returning `sent: true` with the chute flag flipping to 1 on subsequent
frames, and a raw log written with its metadata sidecar.

Corruption handling verified separately across 300 injected failures — **31 distinct
rejection reasons, every one preserving the original text**. One case worth recording:
a dropped field shifts every later field, so `CHUTE:0` lands in the `sat` slot and is
caught there. Nothing is silently repaired.

Decisions implemented rather than merely documented:

- **Raw log is flushed and fsync'd per line.** At 1 Hz the cost is irrelevant; a power
  loss cannot cost more than the line in flight.
- **`errors="replace"`, not `"ignore"`**, when decoding serial bytes. v1 used `ignore`,
  which silently repaired garbage into plausible-looking lines. Replacement characters
  fail parsing loudly instead.
- **GEN1's absent chute flag is `None`, not `0`.** Absent is not "not deployed" — the
  UI must show unknown, never a reassuring ARMED.
- **Range checks warn, never reject.** A genuinely anomalous flight must still reach the
  screen. Bounds are deliberately wider than the simulator clamps, since real logged
  altitude already went to −14.3 m.
- **`rx_index`, not `seq`.** Counts lines arrived, not packets sent (ISS-08).
- **Serial ports are enumerated, never hardcoded** (ISS-05). `--list-ports` reports the
  missing-driver case against ISS-12.
- **Uplink is an allowlist**, not a passthrough — it fires a parachute.

The mock feed is deliberately imperfect: ~2% malformed lines, ~4% `[GCS]` status lines,
with `--clean` to disable. A UI built against a perfect feed has never been tested
against the feed it will get.

`.gitignore` extended for `.venv`, `__pycache__`, `node_modules` and `logs/`. The
`logs/` entry is marked pending — whether flight logs are committed is still an open
question.
