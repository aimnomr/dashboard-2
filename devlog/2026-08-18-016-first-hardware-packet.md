# 016 · First real hardware packet

**Date** 2026-08-18
**Type** investigation
**Refs** ISS-08

## What

Aiman supplied the first telemetry line captured from the GEN1 test firmware:

```
31.5,70.4,1010.0,-0.9,0.92,0.38,-0.05,-0.4,-0.3,4.1,0.00000,0.00000,0.0,0,-11.0,12.25
```

Ran it through the real parser rather than reading it by eye. Added it to
`backend/tests/test_parser.py` as a captured-hardware fixture (five cases). Backend
tests now 22.

Also tidied `frontend/tsconfig.json`: the `exclude` key had been removed with the
deletion of `rocketModel.ts`, but its comment and a dangling comma were left behind,
describing a file that no longer exists.

## Result

**Parses cleanly as GEN1, 16 fields, no range warnings.** The GEN1 path was written from
documentation alone and had never seen real hardware output; it works.

Three findings from the data:

**The unit is not flat.** Gravity sits on the X axis — `ax = 0.92`, `az = -0.05` — where
a flat, still board reads `az ≈ +1.0`. Total magnitude is 0.9966 g, so the accelerometer
is healthy and reading pure gravity; the board is simply on its side, tilted about 22°
within that plane. Computed attitude: pitch −67°, roll 98°.

This is self-consistent with the firmware. `calibrateMPU()` subtracts the `ax`/`ay`
offsets captured at boot, so a unit calibrated *on its side* would read near zero on
those axes. It does not, so it was calibrated flat and moved afterwards. Worth
confirming, because if that board is sitting flat on a desk, the axis assumption or the
mounting is wrong.

**`gz = 4.1 °/s` at rest is gyro bias**, ten times the residual on `gx` and `gy`, and
this is *after* the firmware subtracts `gyroZoffset`. Integrated over a 78 s flight that
is **320° of rotation that never happened**.

That retroactively justifies reverting the 3D pose (entry 014). The gyro-integrated spin
would have shown the vehicle turning a full revolution while sitting motionless on a
bench — visibly and confidently wrong. The 2D horizon shows spin as a *rate*, which is
merely noisy rather than false.

**`RSSI = -11 dBm` is hotter than anything in `CANSAT_DATA`** (max −14, mean −55.6), and
SNR at 12.25 dB is near the SX1262 ceiling. The units were adjacent on a bench. No link
expectations should be drawn from these numbers, and receiver saturation at very close
range can produce bench packet errors that disappear at real distance.

## What the dashboard does with it

Chute shows **UNKNOWN** rather than ARMED, because GEN1 carries no chute field and
absent is not "not deployed". Ground track and GPS show **no fix**, because the firmware
reports `0.00000, 0.00000` when the fix is invalid and the display filters exact zeros.
EJECT will transmit but never confirm — GEN1 has no uplink.

None of these are faults. This single packet happens to exercise both the no-fix filter
and the absent-chute path, which are two of the places where a plausible-looking wrong
answer would otherwise reach the operator.
