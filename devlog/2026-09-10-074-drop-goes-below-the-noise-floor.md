# 074 · `DROP` goes to 0.2 m, which is below the noise floor

**Date** 2026-09-10
**Type** decision
**Refs** 052, 065, 071, 072, 073

Supersedes 073's bench configuration. `AUTO_EJECT_CONFIRM_N` goes back to **3**;
`AUTO_EJECT_DROP_M` goes to **0.2**, and the bound moved to 0.2 to allow it.

**This threshold is expected to self-trigger.** That is not a risk this entry is hedging
against — it is the predicted behaviour, and it is written down here so the bench result
is legible when it happens.

## What

| | 073 | 074 |
|---|---|---|
| `AUTO_EJECT_DROP_M` | 1.0 | **0.2** |
| `AUTO_EJECT_CONFIRM_N` | 1 *(no confirmation)* | **3** *(flight value)* |
| `AUTO_EJECT_DROP_MIN_M` | 1.0 | **0.2** — flight `Config.h`, ground `Config.h`, `api.py` |

Two new constants, and they are the useful part:

```c
#define AUTO_EJECT_FLIGHT_DROP_M     2.0f
#define AUTO_EJECT_FLIGHT_CONFIRM_N  3
```

The boot banner was anchored to `AUTO_EJECT_DROP_MIN_M` in 073. That was wrong, and one
day of use proved it: the floor is the lowest value `SET` will *accept*, and it has now
been lowered twice — 2.0 → 1.0 → 0.2 — to reach bench values. **A banner tied to the floor
goes quiet at exactly the moment it is most needed**, because lowering the floor is what
you do on the way to an unsafe configuration. It is anchored to the flight intent instead,
which does not move for testing.

The banner also gained a second, louder stanza below 0.5 m:

```
[FLT] ** drop is BELOW THE NOISE FLOOR - expect it to  **
[FLT] ** self-trigger. SET:DROP:0.5 is the usable floor **
```

## Why 0.2 will not work, stated before the test rather than after

The BME280 at 16× pressure oversampling with `FILTER_OFF` has ~1.3 Pa RMS, about
**0.11 m**. So 0.2 m is ~1.8σ — thin, but not obviously hopeless.

**The running maximum is what makes it hopeless.** `drop = apogeeAlt − alt`, and
`apogeeAlt` only ever climbs. Noise ratchets it upward and nothing brings it back, so the
reference drifts away from the true altitude as:

```
E[max of n samples] ≈ µ + σ·√(2 ln n)
```

At n = 40 — five seconds at 8 Hz — that is **0.30 m**. A *stationary* unit therefore shows
a typical `drop` of ~0.30 m from noise alone, already past a 0.2 m threshold, and with
`CONFIRM_N` 3 it fires within three samples of arming whether or not anything descends.

**0.5 m is the lowest genuinely usable threshold** without changing the sensor's
filtering. It clears the ~0.30 m drift with margin: firing then needs a further ~1.8σ
excursion on three consecutive samples, roughly 0.005% per triple and ~0.2% across a whole
bench run.

The value is here because it was asked for explicitly, and the bound had to move for it to
be reachable at all. **If the desk test fires before the unit is lowered, that is this,
and `SET:DROP:0.5` fixes it in one command with no reflash.**

There is a way to make 0.2 m real — enabling the BME280's IIR filter (`FILTER_X16`) cuts
pressure noise roughly fourfold, to ~0.03 m, which would put 0.2 m at ~7σ. It is not done
here: the filter settles over ~30 samples, about 3.75 s at this rate, which is lag in the
one place lag costs a parachute. It would be a bench-only sampling profile, and that is a
change worth making deliberately rather than in passing.

## Result

Backend **189 pass** (187 before); `verify_gen3.py` 14/14; frontend untouched.
**Not compiled and not flashed. Auto-eject has still never fired on hardware.**

Three tests added to `test_apogee_trigger.py`:

- **`test_the_noise_floor_warning_also_fires`** — records that `DROP` is under 0.5 and why
  that matters, so the expected self-triggering is in the suite rather than only in prose.
- **`test_the_flight_intent_constants_match_what_the_devlogs_argued`** — pins
  `AUTO_EJECT_FLIGHT_*` against 2.0 / 3, so the banner's reference point cannot drift from
  what 071 and 072 argued for.
- **`test_the_firmware_banner_condition_catches_this_config`** was rewritten to test the
  new anchor rather than the moving floor.

`test_the_compiled_config_is_bench_sensitivity_not_flight` still passes, and passing is
still a warning rather than a reassurance.

### To restore flight configuration

```c
#define AUTO_EJECT_DROP_M        2.0f
#define AUTO_EJECT_CONFIRM_N    3      /* already correct */
```

`CONFIRM_N` needs no change — 074 put it back. Only `DROP` is off flight intent now, which
is a smaller hole than 073 left. Update the assertions in
`test_the_compiled_config_is_bench_sensitivity_not_flight` when you do.

The bounds may stay at 0.2: they only widen what `SET` accepts, and what a vehicle flies
is the default. But note that a 0.2 m floor means a mistyped `SET:DROP` can now reach a
value the sensor cannot support, where previously the bound would have refused it. That is
a real loss of a safeguard, taken knowingly.
