# 013 · 3D attitude with Three.js

**Date** 2026-08-18
**Type** change
**Refs** ISS-08

## What

Replaced the 2D artificial horizon with a Three.js 3D vehicle pose.

```
frontend/src/lib/rocketModel.ts    procedural placeholder + horizon reference
frontend/src/lib/attitude.ts       + integrateSpin()
frontend/src/panels/AttitudePanel.tsx   rewritten
frontend/vite.config.ts            chunkSizeWarningLimit raised, with reasoning
```

Added `three` 0.185 and `@types/three`. Five new tests covering spin integration; 21
frontend tests total.

Also confirmed **uPlot stays** — ~45 kB raw for axes, cursor, autoscaling and gap
handling on live multi-series data. The alternatives are heavier, and hand-rolling it
would be ~200 lines to own forever to save 15 kB on a page served from localhost.

## Why

A 2D artificial horizon is the wrong instrument for a CanSat. It was designed for
aircraft, which live in a narrow band of pitch and roll. A free-falling body can be at
any orientation, and the 2D horizon degenerates precisely when things get interesting —
inverted reads ambiguously, tumbling reads as noise.

The usual objection to Three.js is payload. It does not apply here: the page is served
from localhost off the same disk it was built on, so a 733 kB bundle costs a few
milliseconds of file read. Vite's 500 kB warning is about network download time, so the
limit was raised with that reasoning recorded in the config rather than left to nag on
every build.

## Result

**The design problem worth recording: a 3D model implies three degrees of freedom and
the MPU6050 gives two.** The accelerometer determines tilt exactly; it cannot determine
rotation about the gravity vector, and with no magnetometer there is no heading
reference at all. A freely rotating model would silently assert knowledge the vehicle
does not have — the same class of mistake as deriving packet loss from `rx_index`.

Resolved by separating the two claims:

- **Tilt** — exact, from a minimal rotation carrying the measured up-direction onto
  world up.
- **Rotation about vertical** — integrated from gyro z, labelled in the panel as
  relative and drifting.

Integration is capped at **2 s per step**. Without that, a dropout or a browser tab
backgrounded with throttled timers produces a delta of minutes, and integrating it whips
the model through dozens of revolutions that read as real motion. Tested, along with
negative rates, zero/NaN deltas, and wrapping over a 2000-step session.

**Rendering is driven by incoming telemetry**, once per frame at 1 Hz, not a
`requestAnimationFrame` loop. A dial redrawing 60 times a second to show data that
changes once a second would heat and drain a laptop on battery in a field. Pixel ratio
is capped at 2 for the same reason.

The reliability warning is unchanged — under boost, at apogee or while tumbling the
model desaturates and states the reason. The panel now carries two distinct notices,
because "the tilt may be meaningless" and "the rotation is approximate" are different
claims.

Placeholder vehicle is procedural: cylinder, nose cone, three fins, a contrasting band
and one differently-coloured fin so rotation is visible. A featureless cylinder would
spin invisibly. A faint horizon ring and dashed vertical give the eye something to read
tilt against. The real model swaps in via a loader call plus a scale and axis fix.

Bundle 733 kB raw / 207 kB gzipped, up from 215 kB / 76 kB. Build, 21 frontend tests, 17
backend tests and the end-to-end serve check all pass. **Not visually confirmed** — no
browser in this environment.
