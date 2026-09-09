# 078 · Three fins, and the SET bounds back to the full envelope

**Date** 2026-09-10
**Type** change
**Refs** 052, 073, 074, 075, 076, 077

Two unrelated changes, both closing out the bench work.

## What

**The pose model has three fins, not four** (`rocketMesh()`, `finCount` 4 → 3), to match
the airframe it depicts. 077 chose four without reference to the vehicle; that was the
renderer's convenience, not a likeness.

The roll argument changes with it and is weaker: three fins are three-fold symmetric, so
nose-on they foreshorten to spokes that repeat every 120° rather than every 90°. The
longitudinal stripe is what resolves it in both cases, so nothing is lost that the stripe
was not already covering — and a model that does not match the vehicle teaches the
operator the wrong shape to look for, which costs more than a symmetry order.

**The `SET` bounds are back to their original values**, in all three mirrors:

| | 073–075 | 078 |
|---|---|---|
| `AUTO_EJECT_DROP_MIN_M` | 0.5 | **2.0** |
| `AUTO_EJECT_ARM_MIN_M` | 0.5 | **5.0** |

076 left them low on the argument that the next bench session would then cost three
uplink commands instead of a code change across three files. That trade is reversed here:
the bench run is done, and the bounds are the last thing standing between a typo and a
threshold the barometer cannot support.

**⚠ The desk test is no longer reachable over the uplink.** `SET:ARM:0.5` and any
`SET:DROP` under 2.0 are refused now — at the ground station before transmission, and at
the vehicle as defence in depth. Another bench session needs `MRC_FlightUnit_GEN4/Config.h`,
`MRC_GroundStation_GEN4/Config.h` and `api.py` edited together. That is deliberate, and it
is written into the Config.h comment so the next person does not have to rediscover which
three files.

For that day: **0.5 m is the lowest threshold that can distinguish a descent from the
barometer's noise**, because the running maximum drifts ~0.30 m upward in 5 s from noise
alone. Derived in 074 and 075. Do not go under it.

## Result

Backend **190 pass**; frontend 122; `verify_gen3.py` 14/14; `tsc --noEmit` clean.
**Not compiled and not flashed.**

The defaults are unchanged from 076 and are the flight values throughout:
`ARM` 30 m, `DROP` 2.0 m, `CYCLES` 3, `AUTO_EJECT_SAMPLE_MS` 125.

`test_set_cannot_reach_an_unusable_threshold` was rewritten from an equality to an
**inequality** against `USABLE_DROP_FLOOR_M`. Pinning the exact bound is what made that
test need editing on three of the four times this number moved today, and the safeguard it
exists to protect was never "the bound is 0.5" — it is "the bound is never below the noise
floor". `test_the_bounds_are_back_to_the_full_flight_envelope` pins the specific values
separately, where a deliberate change is expected to break it.

That is the third time in two days a test has needed rewriting because it encoded a value
rather than the reason for the value — 072, 075 and now this. The pattern is worth naming:
**a constant belongs in the assertion only when changing it should fail the test.**

## Still outstanding

**The bench run's result is still not recorded anywhere.** 076 said so and it remains
true: no log has been read, and this project still holds no evidence that auto-eject has
ever fired on hardware. That entry is the one this whole day was for.
