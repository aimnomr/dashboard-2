# 076 · Flight configuration restored after the bench run

**Date** 2026-09-10
**Type** change
**Refs** 071, 072, 073, 074, 075

`AUTO_EJECT_DROP_M` **0.5 → 2.0**. One line. It is the last of the bench values from
073–075 to come back, and the vehicle is at flight configuration again.

## What

| | bench (075) | flight (076) |
|---|---|---|
| `AUTO_EJECT_ARM_ALT_M` | 30.0 | 30.0 — never moved |
| `AUTO_EJECT_DROP_M` | 0.5 | **2.0** |
| `AUTO_EJECT_CONFIRM_N` | 3 | 3 — restored in 074 |
| `AUTO_EJECT_SAMPLE_MS` | 125 | 125 |

**The bounds deliberately stay where the bench work left them**: `AUTO_EJECT_DROP_MIN_M`
0.5 and `AUTO_EJECT_ARM_MIN_M` 0.5, rather than the original 2.0 and 5.0.

They only widen what `SET` will accept, and what a vehicle flies is the default above.
Leaving them low means the next bench session costs three uplink commands instead of a
code change across three mirrored files — and this threshold has already moved four times
in one day, each move touching all three. That churn is the argument.

The cost is stated rather than glossed: **a mistyped `SET:DROP` can now reach 0.5 m, where
the original 2.0 floor would have refused it.** 0.5 is the lowest value that can tell a
descent from the barometer's noise at all, so nothing below it is reachable — but 0.5
itself is a bench threshold, not a flight one, and it is now typeable in flight.

## Result

Backend **189 pass**; frontend 122; `verify_gen3.py` 14/14.
**Not compiled and not flashed** at the time of writing.

`apogeeBegin()`'s NOT FLIGHT SAFE banner is now silent, which is the point of it — a
banner that never goes quiet is a banner nobody reads. Two tests changed direction to
match:

- **`test_the_compiled_config_is_flight_configuration`** replaces
  `test_the_compiled_config_is_bench_sensitivity_not_flight`. That test's docstring said
  passing was a warning; this one's says the opposite.
- **`test_the_boot_banner_is_silent_at_flight_configuration`** is new, and pins the
  inverse of what 073–075 pinned.

`test_the_firmware_banner_condition_catches_this_config` and
`test_the_drop_default_sits_on_its_own_floor` were removed rather than inverted: both
asserted properties that only hold at bench sensitivity, and a test kept alive by
rewriting its meaning each time the config moves is worse than no test. The floor itself
is still pinned by `test_set_cannot_reach_an_unusable_threshold`.

## What is still not known

**A bench run was performed between 075 and this entry. Its result is not recorded here**
— no log from it has been read, and nothing in this entry is evidence about whether
auto-eject fired, what it fired on, or how the servo behaved. That is the first hardware
exercise this mechanism has ever had and it deserves its own entry, written from the log
rather than from memory.

Until then the standing statement is unchanged: **auto-eject has never been observed
firing on hardware in any record this project holds.**
