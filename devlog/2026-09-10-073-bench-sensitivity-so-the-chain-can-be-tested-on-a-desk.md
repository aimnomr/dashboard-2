# 073 · Bench sensitivity, so the chain can be tested on a desk

**Date** 2026-09-10
**Type** decision
**Refs** 052, 053, 065, 071, 072

**⚠ This entry makes the vehicle NOT FLIGHT SAFE on purpose, and says how to undo it.**

Auto-eject has never fired on hardware — eighth session — and it has now been rewritten
three times (071 twice over, 072) since the last time anyone could have observed it. The
blocker has always been the same: arming needs a real climb, and there is no stairwell
available. This entry removes that blocker by making the whole chain exercisable by hand
on a desk.

## What

**Bounds lowered, three mirrors each.** These are the numbers `SET` will accept, and the
project's standing trap is that they drift, so both halves and the backend moved together:

| | was | now | mirrors |
|---|---|---|---|
| `AUTO_EJECT_DROP_MIN_M` | 2.0 | **1.0** | flight `Config.h:212`, ground `Config.h:171`, `api.py:65` |
| `AUTO_EJECT_ARM_MIN_M` | 5.0 | **0.5** | the same three |

**Boot defaults set to bench values:**

```c
#define AUTO_EJECT_DROP_M        1.0f   /* was 2.0 - now at its own floor */
#define AUTO_EJECT_CONFIRM_N    1       /* was 3 - NO CONFIRMATION AT ALL */
```

`AUTO_EJECT_ARM_ALT_M` stays at **30 m**. The pad interlock did not move and should not:
the desk test reaches its arming altitude with `SET:ARM:0.5`, sent per session, so a
rebooted vehicle still cannot arm on the ground.

**A boot banner**, printed by `apogeeBegin()` whenever `confirmN <= 1` or
`dropM <= AUTO_EJECT_DROP_MIN_M`:

```
[FLT] ****************************************************
[FLT] ** AUTO-EJECT IS AT BENCH TEST SENSITIVITY        **
[FLT] ** drop 1.0 m over 1 sample(s) - NOT FLIGHT SAFE  **
[FLT] ** raise SET:CYCLES and SET:DROP before flying    **
[FLT] ****************************************************
```

There is no NVS. What is compiled is what a rebooted vehicle returns to, so a brownout on
a pad silently restores exactly these values — which is why this says so at every boot
rather than trusting anyone to remember.

## Why

**`CONFIRM_N 1` means there is no confirmation.** One qualifying sample drives the
mechanism. That is not a smaller filter than 3, it is the absence of one: a single
anomalous pressure reading is a deployment. It is acceptable on a desk, where the operator
is holding the unit and the servo is unloaded, and it is not acceptable anywhere else.

**`DROP 1.0` sits on its own floor.** Sensor noise still cannot reach it — ~0.11 m RMS at
16× pressure oversampling puts 1 m at roughly 9σ — but real pressure disturbances are not
sensor noise, and there is no margin left below.

**`ARM_MIN 0.5` is what makes the desk test possible.** A unit on a table reads ~0 ± 0.11 m
and cannot reach 0.5 m from noise, so it will not arm by sitting there. Lift it a metre by
hand and it arms; put it back down and the 1 m drop fires it. The whole chain — sample,
arm, detect, drive, count, report — in two movements, with no stairwell.

The bounds moved rather than only the defaults because `SET:ARM:0.5` has to be *accepted*,
and the ground station pre-validates against its own copy before transmitting.

## Result

Backend **187 pass** (186 before); `verify_gen3.py` 14/14; frontend untouched.
**Not compiled and not flashed.**

`test_apogee_trigger.py` was restructured, and the restructuring is the point. Eight of
its tests broke on this change because they had been reading the flown config and
describing whatever was in it. Rule tests now pin their own thresholds through `rule()`;
only a handful named `test_the_compiled_config_*` read `Config.h`, and those exist to
report the danger rather than to bless it:

- **`test_the_compiled_config_is_bench_sensitivity_not_flight`** — passing is a *warning*.
  Its docstring says: if you are preparing to fly, this test should be failing.
- **`test_the_firmware_banner_condition_catches_this_config`** — pins the banner's
  condition against the values it guards, so they cannot drift apart.
- **`test_the_drop_default_sits_on_its_own_floor`** — records that there is no room below.

The same lesson as 072, twice over now: a test written against "whatever is configured"
stops testing anything the moment the configuration is what you are worried about.

### To restore flight configuration

```c
#define AUTO_EJECT_DROP_M        2.0f
#define AUTO_EJECT_CONFIRM_N    3
```

and update the assertions in `test_the_compiled_config_is_bench_sensitivity_not_flight`.
The bounds may stay lowered — they only widen what `SET` accepts, and the defaults are
what a vehicle actually flies. The reasoning for 2.0 / 3 is in 071 and 072; nothing here
argues against it.

### The desk procedure this exists for

1. Flight unit and ground station on USB, dashboard running. **Servo connected but
   unloaded** — the mechanism free to move, nothing under tension.
2. `SET:ARM:0.5`, confirm `ul` rises.
3. Lift the unit ~1 m by hand, hold. Expect `[FLT] auto-eject ARMED at X m`.
4. Lower it to the desk. Expect `[FLT] AUTO-EJECT apogee X alt Y drop Z over 1 samples`
   and the horn to sweep.
5. On the ground: **`chute` rises with `ul` UNCHANGED.** That pair is the only evidence
   the release was automatic rather than commanded.
6. `RESET:CHUTE` to re-arm between runs — never plain `RESET`, which leaves the fire latch
   set.

**Nothing about this validates flight timing.** A hand movement is not a descent; it
exercises the logic and the wiring, not the 4–5 m figure from 072. That number stays
theoretical until something falls.
