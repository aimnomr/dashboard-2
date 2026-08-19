# 030 · Channels view, three-axis IMU, and integrated yaw

**Date** 2026-08-19
**Type** change
**Refs** ISS-08

## What

Two requests: every numeric field should be viewable as a graph, and the attitude panel
should cover all three axes of both sensors rather than two.

**Backend — pipeline step 3, partially.** `pipeline.py` now carries `seq`, `vehicle_ms`
and `crc_ok` in the frame envelope. Loss accounting (`link`) is deliberately *not*
included; that is `linkstats.py`, step 2, and subtracting two `seq` values in a UI would
get baselines, restarts and duplicates all wrong.

Created:

- `backend/tests/test_pipeline_envelope.py` — 8 tests
- `frontend/src/views/ChannelsView.tsx` — every numeric channel, charted
- `frontend/src/lib/timebase.ts` — chart x axis, rule S7, plus reboot handling
- `frontend/src/lib/rates.ts` — vertical rate, moved out of `SpeedPanel` and now on the
  vehicle clock
- `frontend/src/lib/__tests__/yaw.test.ts` — 11 tests
- `frontend/src/lib/__tests__/timebase.test.ts` — 13 tests

Edited: `pipeline.py`, `types/telemetry.ts`, `useTelemetry.ts`, `lib/attitude.ts`,
`components/TimeSeriesChart.tsx` (single-series → multi-series, with a legend),
`components/StatusBar.tsx` (view switch), `App.tsx`, `panels/AltitudePanel.tsx`,
`panels/AttitudePanel.tsx`, `panels/SpeedPanel.tsx`, `styles/global.css`.

## Why

**The graphs needed somewhere to live.** The flight grid is a fixed 3×3 filling exactly
one screen with no scrolling, tuned for reading at a glance while something is in the
air. Ten more always-on charts would have cost the thing it is for. A second view keeps
the launch layout untouched and gives analysis its own screen — which is also exactly
what checking a replayed capture wants.

**"Attitude only captures 2 axes" was half right, in an instructive way.** All six axes
were already being *read* — `hypot(ax, ay, az)` and `hypot(gx, gy, gz)`. What was missing
was that the six raw values were never *displayed*, and that the solution had no yaw.

Yaw is not an implementation gap. An accelerometer cannot measure it at any price:
gravity gives roll and pitch an absolute reference, but rotation *about* the gravity
vector leaves the accelerometer reading unchanged. It has to be integrated from `gz`, and
the MPU6050 is a 6-axis part with no magnetometer, so nothing corrects the result. The
number is therefore relative to boot and drifts for as long as it runs — which is what
the panel says, in those words, rather than presenting a heading that looks like a
compass bearing.

**Entry 014 removed `integrateSpin()` and asked that 3D pose not be re-proposed without
new information.** There is new information: `attitude.ts` said *"no onboard timestamp to
integrate gyro against (ISS-08)"*, and GEN3 has carried `vehicle_ms` since entry 022. The
stale comment is corrected. This is not a re-proposal of the Three.js model — no 3D
rendering was added and no dependency was taken.

Integration is against the **vehicle** clock, never arrival time. Arrival jitter divides
straight into a rate and integrates into heading error indistinguishable from real
rotation. GEN1 and GEN2 therefore get no yaw at all rather than a plausible-looking one.

## Result

Verified in the browser against a live replay: both views render, no console errors,
yaw reading 218° with "relative to boot · drifting · 48 s integrated", RSSI and SNR
showing `—`, and the x axis labelled *vehicle clock*.

121 tests pass — 78 backend (8 new), 43 frontend (24 new). Bundle 221 kB / 77.6 kB
gzipped, up 6 kB.

Design decisions worth keeping in view:

- **A reboot does not rewind the x axis.** Plotting a reset `vehicle_ms` literally would
  send the trace back across the chart and draw a horizontal streak through the whole
  flight. The gap contributes no elapsed time, and the trace *breaks* there instead —
  visible live during `--loop` replay, in the temperature and pressure traces.
- **Yaw is discarded on reboot**, because the boot orientation it was measured from no
  longer exists.
- **Gaps longer than 3 s are not integrated across.** The vehicle kept rotating during the
  silence and nothing recorded it; assuming the rate held would invent the unobserved
  part. The interval is skipped and the estimate flagged.
- **Rotation past 180 deg/s is flagged as unresolvable.** At 1 Hz that is beyond Nyquist,
  so the reconstruction can be turning the wrong way entirely — not merely imprecise.
- **Vertical rate moved to the vehicle clock**, removing the arrival-jitter error the old
  comment in `SpeedPanel` acknowledged.

Surfaced:

- **The view switch rendered stacked vertically** at first — `.statusbar__item` sets
  `flex-direction: column`, and reusing that class inherited it. Fixed, and confirmed by
  screenshot rather than by reasoning about the cascade.
- **A stale CSS cache made the first fix look like it had failed.** Worth remembering
  before chasing a styling bug that is already fixed: hard-reload first.
- `wiki/decisions/frontend.md` summarises why 3D attitude was dropped, and now describes
  a constraint (no onboard clock) that no longer holds. Not edited here — worth a
  decision of its own.
