# 024 · First SD card log analysed

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-08

## What

Aiman supplied `logs/raw/FLIGHT22.CSV`, a real SD log from the GEN3 flight unit with some
rows removed by hand to keep it small. 85 packets, `seq` 1–200, 199 seconds of session.

## Result

**The storage path is clean. 85/85 checksums valid** — not one corrupted write. The CRC
survives build → SD → readback, so it will also catch card corruption after a hard
landing, which is the case that matters.

**File structure is as designed**: two `#` comment lines, then framed packets, no stray
output. The documented pandas recipe loads the real file correctly — 85 rows, correct
dtypes — so the recipe is verified against actual hardware output rather than a synthetic
sample.

**Cadence is exact.** Across 199 seconds:

```
overall ms per seq       : 1000.00
dt where seq steps by 1  : min 1000, max 1000, mean 1000.0
outside 1000 +/- 5 ms    : 0
```

Zero jitter, zero overruns. The deadline scheduler holds 1 Hz and the SD write fits inside
the slack with room to spare.

**Correction to entry 022.** The accelerometer bias was reported there as −82 mg, from
five packets. That reading was contaminated by motion — the gyro reaches 143 °/s in this
log, so the unit was being handled. Filtering to genuinely still rows (`|gyro| < 3 °/s`,
21 of them) gives **0.953 g, a bias of −47 mg** — comfortably inside the MPU6050's ±50 mg
(X,Y) / ±80 mg (Z) zero-g tolerance rather than at its edge. No action needed.

Lesson worth keeping: a five-sample static reading was not enough to separate bias from
handling, and the gyro was the field that revealed it.

**The barometer is performing well.** Pressure varied 0.19 hPa over the session, which
predicts 1.6 m of altitude noise at ~8.3 m/hPa. Observed altitude spread: exactly 1.6 m,
with a correlation of −0.993. The derivation is sound.

This also validates a packet-format decision retroactively: the barometric noise floor is
~1.6 m, so `alt` at `%.1f` is already ten times finer than the sensor resolves. Trimming
it from `%.2f` cost nothing real, and `%.0f` would still be defensible if a byte were ever
needed.

**GPS still has no fix** — `sat=0` for all 85 rows across 207 seconds of uptime. Not
conclusive alone, since a cold start can take 15 minutes, but this is now the second
session with no fix. Resolving it needs `inview` from the relay pair (entry 023): that is
the one field none of this data can provide, and it separates a dead antenna from one that
is merely still acquiring.
