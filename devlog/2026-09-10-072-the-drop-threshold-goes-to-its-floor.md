# 072 · The drop threshold goes to its floor, now that it means something

**Date** 2026-09-10
**Type** decision
**Refs** 052, 053, 065, 071

One number. `AUTO_EJECT_DROP_M` **10.0 → 2.0** in the GEN4 flight unit, which is
`AUTO_EJECT_DROP_MIN_M` — the trigger now has no margin left on that axis.
`AUTO_EJECT_CONFIRM_N` stays at **3**.

## What

```c
#define AUTO_EJECT_DROP_M        2.0f   /* was 10.0f */
```

GEN3 is deliberately left at 10.0 — see Result.

## Why

**It could not usefully be lowered before 071.** At one sample per second and a 20 m/s
descent, the first post-apogee sample is already 20 m down, so every threshold under
~20 m fired on the same sample: `DROP:10` and `DROP:2` were indistinguishable, and the
effective rule was "one sample after apogee". At 125 ms a sample is well under a metre of
travel early in the fall, so the threshold resolves and the number finally means what it
says.

With 071 the confirmation stopped being the dominant term. Of the ~1.7 s that remained at
`DROP:10`, **1.43 s was the fall to reach the threshold** and only 0.25 s was the triple
check. The threshold was the bottleneck, so the threshold moved.

Free fall from apogee, drag-free, `CONFIRM_N` 3 throughout:

| | fall to threshold | granularity | confirmation | total | altitude lost |
|---|---|---|---|---|---|
| before 071 (`DROP:10`, 1 Hz) | 1.43 s | ≤1.00 s | 2.00 s | 3.4 – 4.4 s | 58 – 96 m |
| after 071 (`DROP:10`, 8 Hz) | 1.43 s | ≤0.125 s | 0.25 s | 1.7 – 1.8 s | 14 – 16 m |
| **after 072 (`DROP:2`, 8 Hz)** | **0.64 s** | ≤0.125 s | 0.25 s | **0.89 – 1.01 s** | **4 – 5 m** |

Across 071 and 072 together: **58–96 m becomes 4–5 m.**

## The part that is not an improvement

**The two changes compound, and not in the safe direction.** The rule now wants a **5×
smaller drop held for an 8× shorter window** than the vehicle that flew before them —
2 m over 250 ms, where it was 10 m over 2 s.

Sensor noise alone cannot span that. At 16× pressure oversampling the BME280's RMS noise
is ~1.3 Pa, about 0.11 m, so a 2 m excursion is roughly 18σ. What can span it is a real
pressure disturbance lasting 375 ms: a gust, slipstream over a vent, a payload bay
equalising. Those are not noise and no amount of oversampling filters them.

`AUTO_EJECT_ARM_ALT_M` is unchanged at 30 m and is what keeps this off the pad. The
exposure is narrower than "it might fire early": it is **a transient during ascent, above
30 m, faking a 2 m dip below the highest altitude seen, for three consecutive samples.**

**The correction, if it shows up, is `SET:CYCLES` and not `SET:DROP`** — `DROP` has
nothing below it now, and `CYCLES` is settable over the uplink at the pad with no
reflash:

| `CYCLES` at `DROP:2` | window | altitude lost |
|---|---|---|
| 3 *(as flown)* | 250 ms | 4 – 5 m |
| 5 | 500 ms | 6 – 8 m |
| 10 *(ceiling)* | 1.125 s | 15 – 17 m |

Even at the ceiling that is no worse than the old `DROP:10 / CYCLES:3`, with three times
the evidence behind it. That is the escape hatch, and it is worth knowing about before
standing at a pad rather than after.

## Result

**Not compiled and not flashed.** Auto-eject has still never fired on hardware — eighth
session — and it has now been changed three times (071 twice over, and this) since the
last time anyone could have observed it.

Backend **186 pass** (184 before) with 1 strict `xfail`; frontend 121; `verify_gen3.py`
14/14.

Two tests were added to `test_apogee_trigger.py`: one pinning `DROP` to
`AUTO_EJECT_DROP_MIN_M` so the fact that it sits on its own floor is recorded rather than
remembered, and one asserting the 5× / 8× compounding above so the number cannot drift
without something saying so.

**One test failed on this change and deserved to.**
`test_a_non_qualifying_sample_resets_the_count` used a dip to 95 m against a 100 m apogee
— a 5 m drop, which was "non-qualifying" at `DROP:10` and is qualifying at `DROP:2`. The
test was written around numbers that happened to work rather than around the threshold it
was testing. Rewritten relative to `DROP_M`. Worth recording because it is the same class
of thing the models exist to catch, caught by the models.

**GEN3 keeps `DROP` at 10.0 m, deliberately**, the same way 067 left its chute counting
alone. GEN3 samples once per second and has no `SET`, so 2 m there is the no-op this entry
describes, with no way to correct it in flight. 10 m is the right default for a 1 Hz
trigger. Recorded so it reads as a decision rather than a missed mirror — CLAUDE.md's
standing trap is that these drift silently, and this one is divergent on purpose.

**`AUTO_EJECT_SAMPLE_MS` was reviewed and left at 125.** It is not a compromise between
speed and load: I²C costs about 8 ms per second at this rate, under 1 % of the cycle, so
load is not the binding constraint. The barometer is — ~113 ms worst-case conversion, so
125 ms is the fastest rate at which every sample is a fresh measurement. Faster
double-counts one physical reading as two confirmations; slower now costs real altitude,
because after 072 the trigger's own terms are 37 % of the total rather than 22 %.
