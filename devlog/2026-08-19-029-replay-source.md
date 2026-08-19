# 029 · Replay source — captured flights through the full stack

**Date** 2026-08-19
**Type** change
**Refs** ISS-16, ISS-14

## What

Built the file replayer that `sources/base.py` has been reserving a seat for, so a
captured SD log can be fed through the real pipeline as if it were arriving live.

Created:

- `backend/dashboard/sources/file_source.py` — `FileSource`, `simulated = True`
- `backend/devtools/run_replay.py` — entry point, deliberately not reachable from
  `python -m dashboard`
- `backend/tests/test_file_source.py` — 13 tests

Edited:

- `frontend/src/types/telemetry.ts` — `rssi`/`snr` now `number | null`; `generation`
  gains `'GEN3'`
- `frontend/src/lib/link.ts` — new `formatMeasurement`
- `frontend/src/components/StatusBar.tsx` — uses it instead of calling `.toFixed()`
  directly
- `frontend/src/lib/__tests__/logic.test.ts` — 3 tests for the absent-measurement path
- `backend/devtools/README.md` — replay section

Pacing comes from the capture's own `ms` field rather than a chosen interval, so replay
reproduces the cadence the vehicle actually ran at. `--speed` compresses it, `--loop`
repeats, `--hold` keeps the dashboard up after the capture ends, `--interval` forces a
fixed gap.

## Why

Aiman wanted to test the whole system against real flight data rather than the synthetic
profile. The SD card already holds framed GEN3 packets — byte for byte what the ground
station receives — so nothing needed converting. The seam was designed for this.

**Link quality is not synthesised.** RSSI and SNR are measured by the ground station's
radio as a packet arrives. A packet read from a file crossed no radio, so there is no
measurement, and both stay `null`. Appending a plausible-looking dBm figure was the
alternative considered and rejected: it would have avoided all the frontend work below,
at the cost of putting a fabricated measurement on the operator's screen.

Choosing `null` made the frontend's tolerance of it real work rather than a workaround —
replay is a legitimate mode, and the system should survive a packet that has no link
quality because sometimes packets genuinely do not.

## Result

Verified end to end against a running server: `session` reports `source=replay
simulated=true`, frames arrive as `generation=GEN3, ok=true` carrying real vehicle
values, and inter-frame gaps measured **0.511 s mean at `--speed 2`** against a true
1.000 s cadence — 2% over, which is Windows' ~15 ms timer granularity.

81 tests pass: 68 backend, 19 frontend (13 and 3 of them new). Typecheck clean.

Surfaced along the way:

- **Replaying would have crashed the dashboard.** `StatusBar.tsx:47` called
  `latest.frame.rssi.toFixed(0)` on a value the parser correctly reports as `null` for
  SD-sourced packets, taking the whole React render down with a TypeError. Fixed, and
  pinned by a test.
- **The server used to exit the moment a capture ended**, because `runner.serve` stops on
  `FIRST_COMPLETED`. Correct for the mock, wrong for a replay whose entire purpose is to
  be looked at. `--hold` added; the default still exits.
- **Windows timer granularity caps useful playback speed at roughly ×60.** Every
  `asyncio.sleep` shorter than ~15 ms costs 15 ms anyway. Documented in `--speed` help
  rather than worked around.
- **Tests must use `interval=0`, not a high `speed`.** Same granularity: at `speed=1000`
  the three content tests still slept ~3.4 s each. The suite went 0.26 s → 11 s → 0.30 s.

Still true, and expected: the envelope carries no `seq`, `vehicle_ms`, `crc_ok` or
`link`, because pipeline steps 2–3 are unbuilt. Replay works without them; charts run on
the PC arrival clock and there is no loss figure. Both captures are bench runs, so the
dashboard looks nearly static — flat altitude, no GPS fix (ISS-14), chute never
commanded.

ISS-16 is functionally delivered, though it was recorded as deferred. Worth reconciling
in `wiki/issues.md`.
