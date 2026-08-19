# 041 · Yaw removed, and the pose model was mirrored

**Date** 2026-08-19
**Type** change
**Refs** ISS-08

## What

Two changes to the attitude display, both prompted by Aiman putting it on hardware.

**1 · `integrateYaw()` is gone.** Repeated hardware testing showed the integrated heading was
not reliable enough to use for attitude. Removed from `lib/attitude.ts`, along with
`YawEstimate`, `MAX_INTEGRATION_DT` and `ALIAS_RATE`; the readout and both footnotes are gone
from `AttitudePanel`; the yaw tick is gone from `PoseView`.

**Every raw channel stays.** `gx`, `gy` and `gz` are still parsed, still charted in
ChannelsView, still logged. What went is the *derived* heading, not the measurement.

The reasoning was already in the code and entry 030 — 6-axis part, no magnetometer, relative
to boot, unbounded drift, aliasing past 180 deg/s at 1 Hz. That reasoning was right and the
estimate was labelled honestly; it still was not usable. A number that has to be disclaimed
every time it is read is not carrying its weight on a screen meant to be read at a glance.
`lib/attitude.ts` keeps a block recording this so it is not re-proposed a third time — after
`integrateSpin()` in entry 014 and `integrateYaw()` in 030, that is a real risk. Re-proposing
yaw now needs new hardware, not new code.

**2 · The pose model was mirrored — `viewTransform` was a reflection, not a rotation.**

Aiman reported the model mirrored, with roll matching reality and pitch reversed. The cause
is one sign in `lib/pose.ts`. With `x: p.x` the matrix rows are `(1,0,0)`, `(0,se,ce)`,
`(0,ce,−se)` and the determinant is `se·(−se) − ce·ce = −1`. A determinant of −1 is a mirror.

The mirror plane is `x = 0`, and conjugating the two body rotations through
`M = diag(−1,1,1)` explains the asymmetry exactly:

| | |
|---|---|
| `M · Ry(pitch) · M = Ry(−pitch)` | pitch axis lies *in* the plane → **reversed** |
| `M · Rx(roll) · M = Rx(roll)` | roll axis is *normal* to it → **unaffected** |

So "roll matches, pitch doesn't" is not two bugs, it is the signature of this one. Fixed by
negating x, making screen-right world −x and the determinant +1.

This is the second half of the bug entry 038 fixed. That entry corrected the elevation terms
in the same function and left the parity error sitting behind them.

## Why the sign, and not the pitch

Negating the pitch fed to `projectMesh()` would also have made the lean look right, and the
symptom alone cannot tell the two apart. The determinant can: it is objective evidence that
the camera is the faulty part, independent of what the model looks like. Fixing the pitch
would have left the projection a mirror, with inward-facing normals and every future addition
to that space — an axis triad, a velocity vector — silently reversed again.

## Result

`frontend/src/lib/attitude.ts`, `lib/pose.ts`, `components/PoseView.tsx`,
`panels/AttitudePanel.tsx`, `App.tsx`, `styles/panels.css` edited.

- `AttitudePanel` no longer takes `history` — with yaw gone it needs only the latest frame
- `.attitude__readout` returns to one column; two columns existed only to fit a fourth value
- `drawYawRing()` became `drawBezel()`. The dashed ring stays because it is also the frame
  for the no-data state, which would otherwise be an empty rectangle

**Tests: 118 backend, 59 frontend.** Two new ones in `pose.test.ts` close the gap that let
this through twice:

- **the determinant is +1** — the single assertion that would have caught it when written
- **a positive pitch puts the nose on a named screen side**, and negative on the other

Every pre-existing camera test passes just as happily under a reflection, which is why
eleven of them missed it. Entry 038 recorded that the tests all checked the body and none
checked the camera; the truer statement is that they checked the camera's *axes* and never
its *handedness*.

**Left for Aiman:** `frontend/src/lib/__tests__/yaw.test.ts` (11 tests) must be deleted — it
is the only remaining reference to `integrateYaw` and the only thing `tsc --noEmit` still
complains about. Deletion is not mine to do.

**Not yet verified in the browser.** The parity fix also flips every face normal in
`faceLight()`, since those are cross products taken after the transform — under the old
determinant they pointed inward, so the shading has been inverted all along. Each face's
`light` becomes `1 − light`. That is now geometrically correct but visibly different, and the
light vector and the cap winding at `pose.ts:146` deserve eyes on them against a live replay.
The stripe also moves to the other side of the model, which is cosmetic.
