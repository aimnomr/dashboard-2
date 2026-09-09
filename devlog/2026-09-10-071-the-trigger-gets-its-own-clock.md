# 071 · The trigger gets its own clock, and stops trusting a NaN

**Date** 2026-09-10
**Type** decision
**Refs** 036, 052, 053, 062, 065, 066, 069, 070

Flight unit. Closes devlog 065 fault 4 and the code half of fault 3, and decouples the
auto-eject trigger from the telemetry cadence. All three are trigger behaviour rather than
patches, which is why this is one entry and typed as a decision — 065 said the guard and
the baseline band would each be exactly that.

**`AUTO_EJECT_CONFIRM_N` stays at 3.** The confidence is unchanged; only the clock moved.

## What

### 1 · A non-finite altitude is rejected, and state is held

```c
if (!isfinite(alt)) { apogeeRejected++; return false; }
```

Nothing below that line runs: `apogeeAlt` is not moved, `apogeeArmed` is not changed,
`descentCycles` is neither reset nor incremented, the latch is not touched. The rule does
not advance on evidence it does not have, and picks up where it left off when good data
returns.

**Reject-and-hold was chosen over reject-and-reset.** Resetting would discard a genuine
descent already two samples into confirmation because of one bad reading — which is the
opposite of what `confirmN` is for. Holding costs nothing on a transient and is the same
answer the rule would give if the sample had simply not been taken.

The old behaviour was the dangerous one. Every comparison against NaN is false, and the
rule read those falses in **opposite directions**: `alt > apogeeAlt` false froze the
apogee, `alt >= armAltM` false meant it could not arm — so the pad was safe — but
`drop < cfg.dropM` false meant `descentCycles` was **never reset**, so it climbed to
`confirmN` and deployed the vehicle wherever it happened to be.

### 2 · The altitude baseline is checked before it is trusted

`baseAltitude` was captured once at the end of calibration with no check at all. 065 fault
3 is what that costs: a vehicle restarted while its BME280 was returning garbage, captured
the baseline during the corruption, and flew reading about **−1740 m** with a correct
pressure beside it — unable to arm at the 30 m default or at the 5 m floor.

Now: up to `ALT_ZERO_ATTEMPTS` (5) reads, 200 ms apart, requiring a finite value inside
`ALT_ZERO_MIN_M`…`ALT_ZERO_MAX_M` (−500 to 5000 m). The band is deliberately wide — it is
a sanity check, not a site survey, and the failure it rejects read −1740 m.

**If every attempt fails it refuses to zero rather than zeroing wrongly.**
`altitudeZeroed` stays false, `sensorsAltitude()` reports 0.0, and 0.0 can never pass the
arming floor — so the trigger is inert while telemetry, the uplink and the commanded
release all still work. Said loudly on serial and on the OLED. A vehicle that cannot
auto-eject is a disappointment; one that thinks it is 1.7 km underground is a liar.

### 3 · The trigger samples on its own clock

`apogeeTick()` joins `chuteTick()` in all four poll loops, self-throttled to one altitude
read per `AUTO_EJECT_SAMPLE_MS` (125). `apogeeUpdate()` now has exactly one caller.
`sensorsAltitude()` was extracted so the packet and the trigger cannot come to disagree
about how high the vehicle is.

**Telemetry is untouched at 1 Hz.** This is not the 2 Hz proposal rejected in 036 — no
packet is sent any faster, and the packet format does not move.

### 4 · Cooperative, not a second core — and the reason is the sensor

The ESP32-S3 has a spare core, and a FreeRTOS task would be genuine parallelism. It was
not taken, and the deciding fact is not caution:

**`bme.begin()` is called at `Sensors.ino:54` with no `setSampling()`**, so the Adafruit
default applies — `MODE_NORMAL`, **16× oversampling on temperature, pressure and
humidity**, `FILTER_OFF`. A conversion at 16× takes **~98 ms typical, ~113 ms worst
case**, and in normal mode the data registers only update at that rate.

**The barometer is the rate limit, not the CPU.** A parallel task would sample no faster
than the cooperative one, while putting a mutex between two contexts on the I²C bus that
dropped *both* sensors across five restarts in session `20260909-024620`, making
`chuteFire()` reentrant across cores with a TOCTOU window on its own latch, and driving
`ESP32Servo` from two contexts. All cost, no gain, on a path that fires a parachute.

125 ms sits just above the worst-case conversion so every sample is a fresh measurement.
Read faster and one physical measurement satisfies two confirmations — the exact thing
`confirmN` exists to prevent.

## Why

`confirmN` was counted in whole telemetry cycles, so three confirmations cost 2 s plus up
to another second of sampling granularity. In free fall from apogee, with the 10 m
threshold:

| | before | after |
|---|---|---|
| confirmation window | 2000 ms | **250 ms** |
| sampling granularity | up to 1000 ms | up to 125 ms |
| total from apogee | 3.4 – 4.4 s | **~1.7 – 1.8 s** |
| **altitude lost** | **57 – 96 m** | **~14 – 16 m** |

Drag-free on both sides, so the comparison is fair even though the absolute figures are
upper bounds. Most of what remains is the physical fall to `dropM`, not the trigger.

For scale: 069 and 070 together took the *commanded* path from ~220 ms to ~56 ms. **This
is worth roughly forty times that**, and it is on the path that has to work when the
ground station is not heard.

The guard is not optional at this rate. At one sample per second three NaNs took 3 s to
deploy an armed vehicle; at 125 ms they take **~375 ms**. Raising the rate without the
guard would have made fault 4 substantially worse, which is why they are one entry.

## Result

**Not compiled and not flashed.** `arduino-cli` is still not on this machine. **Auto-eject
has still never fired on hardware** — eighth session — and nothing here is evidence that
it will.

Backend **184 pass** (164 before) with 1 strict `xfail`; `verify_gen3.py` 14/14.
`backend/tests/test_apogee_trigger.py` is new: 19 tests modelling the rule, reading
`AUTO_EJECT_*` and `ALT_ZERO_*` out of `Config.h` rather than carrying copies, so the
mirrored-constant trap cannot bite it. `status.md` Next 6 has asked for this trace for
three sessions — sessions 5, 7 and 8 each found a real error in this state machine
without a committed test. The fault 4 regression is the first test in the file.

It is a MODEL and can drift from the C. Its docstring says so.

**Known and accepted:**

- **Sampling is not uniform.** The loop blocks in `radio.transmit()` for ~231 ms plus the
  SD write and OLED, so roughly 700 ms of every 1000 has a poll loop running — expect
  ~5–6 samples a second, not 8. `confirmN` counts samples, so a gap straddling the
  transmit delays a decision by up to ~231 ms; it never resets it.
- **Reporting lags actuation.** With ~6 decision points a second and one packet, most
  automatic releases are now reported on the *following* packet, up to ~1 s later. The
  mechanism is driven the moment the rule decides; only the ground's view of it waits.
- **Holding is not recovering.** A barometer that fails permanently leaves the trigger
  frozen for the rest of the flight. It will not fire wrongly and it will not fire at all.
  The uplink is the backup for that case, which is what the uplink has always been for.
- **Three samples now filter a ~250 ms excursion, not a ~2 s one.** If the barometer
  proves noisy in flight, raise `confirmN` rather than lowering the rate — at 8 Hz the
  existing ceiling of 10 is still only 1.25 s, which is faster than today's 3 *and*
  carries three times the evidence.
- **`COMMANDS.md` now describes `SET:CYCLES` in samples.** The command, its bounds (1–10)
  and its wire format are unchanged; what a cycle *means* is not.
