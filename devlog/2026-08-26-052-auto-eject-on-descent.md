# 052 · Auto-eject on descent, and `ul` decoupled from `chute`

**Date** 2026-08-26
**Type** change
**Refs** —

The flight unit can now release the parachute without being told to.

## What

**`Apogee.ino`** — new file. Tracks the maximum of `alt` every cycle, and fires once
when the vehicle has descended a set distance below it:

```
apogee - alt >= AUTO_EJECT_DROP_M, for AUTO_EJECT_CONFIRM_N consecutive cycles
```

Gated behind an arming floor: the rule is inert until altitude passes
`AUTO_EJECT_ARM_ALT_M`. Defaults are **30 m arm, 10 m drop, 3 cycles**, all in
`Config.h` and all meant to be changed. Apogee is tracked even with
`ENABLE_AUTO_EJECT` at 0 — the measurement outlives the trigger built on it.

**`MRC_FlightUnit_GEN3.ino`** — `apogeeBegin()` after `sensorsCalibrate()`, and the
rule evaluated between the sensor read and the packet build so a release is visible
in the same cycle's `chute`. On fire: `chuteCommands++` and `chuteFire()`, exactly as
an uplink command does.

**`Radio.ino`** — new `uplinkCount`, incremented inside the branches that matched
`EJECT_TOKEN` or `PING_TOKEN`. The packet's `ul` reads it directly.

**`Display.ino`** — the chute line now reads `AUTO x1` rather than `CMD x1` when the
rule fired, and `ARMED+A` when the chute is unfired with auto-eject armed behind it.

**`Storage.ino`** — the SD header described GEN3.0's 17 fields. Now GEN3.1's 20, and
the file identifies itself as `# MRC CanSat GEN3.1 flight log`.

**Wiki** — `chute` and `ul` restated in `decisions/gen3-packet-format.md`; the stale
socket-envelope example and parser error string in `decisions/frontend.md`; the
"19 fields at the PC" line in the dashboard-impact section.

## Why

The uplink was the only way to deploy. Entries 039 to 044 spent four sessions
establishing that it worked at all, and 051 finally proved it on hardware — but a
link that is proven is still a link that can fail at the moment it is needed, and
until now that failure took the parachute with it.

Barometric descent is the one deployment signal the vehicle can read without help.

**The arming floor is the whole safety argument.** Altitude is relative to boot, so a
unit on the pad reads ~0 and drifts. Without a floor, that drift sets an "apogee" of a
few centimetres and any dip below it is a live trigger a metre off the ground. The
failure direction is deliberate: a flight that never reaches 30 m never arms, and the
uplink stays the only path. Never arming is recoverable; arming on the pad is not.

**`ul` had to be decoupled.** It was computed at the packet as
`pingCount + chuteCommands`, an identity that held only while the uplink was the sole
thing that could move `chute`. A vehicle releasing on its own would have reported
`ul = 1` having never heard the ground station — the exact opposite of what the field
exists to prove. It is counted at the radio now, where the evidence is.

## Result

**`chute` now means "releases commanded, from either path"**, not "eject commands
received". It is no longer a measure of uplink quality; `ul` is, and that is now true
by construction rather than by coincidence.

**`chute ≥ 1` with `ul = 0` is a valid state** — an automatic release on a flight
where the ground station was never heard. Previously it would have looked impossible.

Traced against the wiki's V7 descent profile, at 1 Hz with ±0.15 m of baro noise:
arms at t=2 s, fires at t=20 s, **23 m below a 150 m apogee**. A single 12 m pressure
transient at apogee does not fire it — the consecutive-cycle counter resets on the
recovery. Twenty simulated minutes of pad noise never arm. A manual eject at 150 m
leaves `chute` at 1, not 2.

**Left open — the confirmation count costs real altitude in genuine freefall.** The
same trace at 30 m/s descent fires **90 m below apogee**, deploying at 60 m on a 150 m
flight. Three cycles is three seconds, and three seconds is cheap only while the
vehicle is falling slowly. `AUTO_EJECT_CONFIRM_N` is a one-character change and the
right value depends on a real descent rate, which has not been measured.

**Not built:** nothing verifies this rule mechanically. `firmware/tests/verify_gen3.py`
pins the packet; the apogee state machine has no equivalent, and was checked by a
throwaway trace rather than a committed test.

**Not compiled.** There is no Arduino toolchain here — Aiman builds and flashes.

**Not touched:** `backend/devtools/mock_source.py` still emits GEN2 and cannot exercise
this. `wiki/source/**` is external fact and stays as written.
