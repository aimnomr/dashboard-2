# 045 · The attitude warning flickered, and the camera was below the horizon

**Date** 2026-08-19
**Type** fix
**Refs** ISS-08

Two faults Aiman found on hardware after entry 041, which fixed neither.

## 1 · "Attitude unreliable" on a unit sitting still

Measured across the three most recent hardware logs, with the accelerometer and gyro read
from the correct packet fields (`$8..$10` and `$11..$13` — the first pass at this had them
off by one and produced nonsense):

| log | packets | mean \|a\| | unreliable | lone single frames | longest run |
|---|---|---|---|---|---|
| `20260819-225006` | 260 | 0.917 g | 10 | **10 of 10** | 1 |
| `20260819-224339` | 240 | 0.943 g | 13 | 9 of 13 | 2 |
| `20260819-212959` | 386 | 1.139 g | 219 | 14 | **163** |

The thresholds were not wrong. **Every spurious warning on a stationary unit was a single
frame**, and the one genuinely sustained episode ran for 163. The two populations separate
almost perfectly at one sample of persistence.

The offending samples are not physical. A CanSat on a table does not spend exactly one second
at 0.365 g, or spin at 184 deg/s and stop. Those are sensor glitches, and at 1 Hz each one
blanked the panel and greyed the model for a full second.

**Fix — `attitudeWarning(history)` in `lib/attitude.ts`.** The warning now requires
`UNRELIABLE_CONFIRM = 2` consecutive bad frames, and clears on the first good one.

`computeAttitude()` stays pure and per-frame, and pitch, roll and spin still update instantly.
Only the *claim about* the readings is confirmed across frames. That split is the point: a
reading may lag nothing, but a claim that flickers is worse than no claim at all — an operator
who has watched the warning blink on a stationary unit has learned to ignore it, which is the
exact opposite of what it is for. Clearing is deliberately asymmetric, because "you can trust
this again" is safer to be quick about than "you cannot".

Against the logs: the 22:50 session would show **no** warning at all, 22:43 drops from 13 to
2, and the 163-frame episode still warns from its second frame. Real boost and real freefall
last many seconds and are unaffected beyond a one-second delay.

`AttitudePanel` takes `history` again — removed in 041 when yaw went, and needed once more for
a value derived over time. `PoseView`'s greying uses the same signal, so the shape and the
notice can no longer disagree.

## 2 · The pose model was still flipped — the camera was 22° below the horizon

Entry 041 fixed the mirror. It did not fix this, and the two are independent.

`viewTransform` was, after 041, a perfectly valid right-handed rotation: determinant +1, axes
correct, nose at the top of the screen. It was also **on the wrong side of the horizon**. The
elevation terms described a camera 22° *below* the origin rather than above it, so the tail
cap faced the viewer and an upright can presented its dark end — reading as inverted.

Confirmed before changing anything, by asserting it and watching it fail:

```
expected -0.37460659341591196 to be greater than 0.37460659341591196
```

Nose cap depth −0.375, tail cap +0.375, larger being nearer. The tail was in front.

The corrected basis:

```
right (x) = (-1,   0,   0)
up    (y) = ( 0, -se,  ce)
depth (z) = ( 0,  ce,  se)    <- +se puts the nose nearer the viewer
```

**This is the third distinct sign error in the same nine numbers** — the swapped elevation
terms in 038, the reflection in 041, and now the elevation's sign. Each survived every test
written for the one before it, because each test checked one row of the basis and the faults
were in the relationship *between* rows. The comment in `pose.ts` now says so.

## Result

`lib/attitude.ts`, `lib/pose.ts`, `panels/AttitudePanel.tsx`, `App.tsx` changed.

**Tests: 118 backend, 66 frontend** (was 59). Seven new:

- six in `logic.test.ts` for the warning — lone frame ignored, persistence warns, first good
  frame clears, nothing said before there are enough frames, the reason reported is the
  current one, and `UNRELIABLE_CONFIRM` actually governs the count
- one in `pose.test.ts` — **the nose cap must be nearer than the tail at rest**, which is the
  assertion that pins the camera to the correct side of the horizon

The pose test is the one worth keeping in view. Every previous camera test passes under both
elevations, which is precisely why this shipped.

**Still not verified in a browser.** The parity change in 041 also inverted every face normal
in `faceLight()`, and this entry's change moves the light source relative to the geometry
again. The shading is now geometrically correct and visibly different from anything seen so
far; the light vector and the cap winding at `pose.ts:146` want eyes on them against a live
replay.

**Firmware items surfaced, not fixed:**

- **The accelerometer reads ~0.917 g at rest, not 1.000.** An 8% scale error, consistent
  across every log. It is not the cause of the flicker, but it makes the reliability band
  asymmetric about the true rest point: 18% of margin below, 36% above. Worth a look at
  `MPU_ACCEL_RANGE` and the divisor in `Sensors.ino` against the datasheet.
- **The gyro produces large single-sample spikes** — 70.00, 110.8, 159.4, 184.9 deg/s on a
  stationary unit, one sample wide. A calibrated MPU6050 at rest should read under 2 deg/s.
  These look like I²C read glitches rather than motion. Debounced on the dashboard side now,
  but the vehicle is logging them to SD as fact.
