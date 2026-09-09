# 075 · `DROP` settles at 0.5 m, the usable floor

**Date** 2026-09-10
**Type** decision
**Refs** 071, 072, 073, 074

`AUTO_EJECT_DROP_M` **0.2 → 0.5**, and `AUTO_EJECT_DROP_MIN_M` back up to **0.5** in all
three mirrors. `AUTO_EJECT_CONFIRM_N` stays at 3. Fourth and last move of this threshold
today: 2.0 → 1.0 → 0.2 → 0.5.

## What

| | 074 | 075 |
|---|---|---|
| `AUTO_EJECT_DROP_M` | 0.2 | **0.5** |
| `AUTO_EJECT_DROP_MIN_M` | 0.2 | **0.5** — flight `Config.h`, ground `Config.h`, `api.py` |
| `AUTO_EJECT_CONFIRM_N` | 3 | 3 |

## Why

0.5 m is not a preference, it is where the measurement stops working. The BME280 at 16x
pressure oversampling with `FILTER_OFF` has ~1.3 Pa RMS, about **0.11 m** — but `drop` is
measured against a **running maximum**, which noise ratchets upward and never releases:

```
E[max of n samples] = mu + sigma * sqrt(2 ln n)   ->  ~0.30 m after 40 samples (5 s)
```

A stationary unit therefore shows a typical `drop` of ~0.30 m from noise alone. 074 set
the threshold under that on purpose and predicted self-triggering. 0.5 m clears the drift
with margin: firing needs a further ~1.8 sigma excursion on three consecutive samples,
roughly 0.2% across a whole bench run.

**The bound went back up with the value, which is the part worth recording.** 074 lowered
it to 0.2 and noted the lost safeguard in as many words: a mistyped `SET:DROP` could reach
a threshold the sensor cannot support, where the bound would previously have refused it.
There is no legitimate configuration below the noise floor, so nothing should be able to
reach one by typo. The floor is back at the same number as the value it guards.

Going below 0.5 needs the sensor changed rather than the threshold — `FILTER_X16` cuts
pressure noise about fourfold, which would make 0.2 m viable at the cost of ~30 samples of
settling, about 3.75 s of lag. That is lag in the one place lag costs a parachute, so it
would be a bench-only sampling profile and a deliberate change rather than a passing one.
Not done here.

## Result

Backend **190 pass** (189 before); `verify_gen3.py` 14/14; frontend 121.
**Not compiled and not flashed. Auto-eject has still never fired on hardware.**

`USABLE_DROP_FLOOR_M` is now a named constant in `test_apogee_trigger.py` with the
derivation in its comment, and two tests hang off it:

- **`test_drop_is_at_or_above_the_usable_noise_floor`** — replaces 074's inverted
  `test_the_noise_floor_warning_also_fires`, which asserted the *opposite* because that
  was the state 074 chose.
- **`test_set_cannot_reach_an_unusable_threshold`** — pins the bound to the same number,
  so the safeguard cannot be quietly lowered again without a test failing.

The threshold moved four times today and the tests broke on three of those moves, each
time because they encoded a number rather than the reason for it. That is the same lesson
072 and 073 both recorded, and it is now spent: the floor is derived from sensor noise in
one place and everything else refers to it.

`AUTO_EJECT_DROP_M` remains off flight intent (0.5 against 2.0), so `apogeeBegin()` still
prints its NOT FLIGHT SAFE banner at every boot. The second, louder noise-floor banner no
longer fires. **To restore flight configuration: `AUTO_EJECT_DROP_M 2.0f`, and nothing
else.**
