# 011 · Remaining seven panels

**Date** 2026-08-18
**Type** change
**Refs** ISS-08, ISS-11

## What

Built the seven placeholder panels out, completing the live view.

```
src/lib/geo.ts            projection, fix validity, scale bar
src/lib/attitude.ts       pitch/roll and whether they mean anything
src/components/Sparkline.tsx
src/panels/GroundTrackPanel.tsx    canvas XY trace
src/panels/AttitudePanel.tsx       canvas artificial horizon
src/panels/GpsPanel.tsx
src/panels/SpeedPanel.tsx
src/panels/EnvironmentPanel.tsx
src/panels/EjectPanel.tsx
src/panels/RawFeedPanel.tsx
src/styles/panels.css
src/lib/__tests__/logic.test.ts    vitest, 16 tests
```

## Why

Three panels carry logic that can mislead rather than merely look wrong, so each got a
deliberate design position rather than a straight rendering of the field:

**GPS "0,0" is not a position.** The firmware sends `0.0,0.0` whenever the fix is
invalid — it does not send null and does not omit the field. Plotted verbatim that puts
the CanSat in the Gulf of Guinea and drags a trace line across the Atlantic to get
there. `hasFix()` filters exact zeros before anything is drawn.

**Accelerometer attitude is only valid when the vehicle is not accelerating.** An
accelerometer measures gravity plus vehicle acceleration; under boost it is measuring
thrust, and at apogee there is no gravity vector to measure at all. The panel computes
pitch and roll but labels them unreliable — with the reason — when total acceleration
departs from 1 g or the spin rate exceeds 90 °/s. Drawing a confident horizon from
those numbers would be inventing information.

**Vertical rate matters more than GPS ground speed during descent**, because it is what
says whether the chute is working. Derived over a 5-sample window, since per-sample
differences at 1 Hz are mostly barometric noise. Timing is PC arrival time (ISS-08), so
the figure inherits link jitter.

**EJECT is a two-step control.** Arm, then fire, with arming lapsing after 10 s so a
parachute control is never left hot. The panel reports **command sent** and **vehicle
confirmed** as two separate states, because they are: the link carries no
acknowledgement, so deployment is confirmed only by `CHUTE:1` arriving in later
telemetry. If no confirmation arrives within 6 s it says so rather than continuing to
show a hopeful "awaiting".

**Ground track has no basemap** (ISS-11) — a plain XY trace in metres from the first
fix, with the launch point crossed and a scale bar. Square aspect, so a stretched trace
cannot misread as drift in a direction the vehicle never went.

**Raw feed pauses when scrolled up.** Reading back through history should not be yanked
away by the next packet.

## Result

**Verified.** 17 backend tests, 16 frontend tests, strict TypeScript build, and the
served bundle checked end to end with `/ws` still routing behind the static mount. No
external URLs in the built page.

One test failure was genuinely useful: the freefall case initially asserted that
`az = -0.8 g` should be flagged unreliable. It should not be — magnitude there is still
about 1 g, so gravity dominates and the reading is real: the vehicle is upside down.
The wrong thing was the test's expectation, not the code. Corrected, and a second case
added asserting that inverted-but-1 g is treated as a genuine attitude. Suppressing it
would hide real information during descent.

Bundle 215 kB raw / 76 kB gzipped.
