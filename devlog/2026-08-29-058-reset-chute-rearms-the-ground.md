# 058 · RESET:CHUTE re-arms the ground station, not just the vehicle

**Date** 2026-08-29
**Type** fix
**Refs** 052, 053, 057

Found on hardware, running entry 057's own bench plan:

```
PING          -> ul rose
EJECT         -> servo threw
RESET:CHUTE   -> servo returned to ARMED
EJECT         -> nothing
```

Reported as an unresponsive servo. The servo was fine. **The ground station never
transmitted.**

## What

Two latches live on the GROUND unit, and until now `RESET:CHUTE` cleared neither:

1. `ejectConfirmed` (`Uplink.ino:87`) — checked before `handleCommand()` does anything
   else, set during the first successful burst, and cleared by nothing at all.
2. `lastChute >= 1` (`Uplink.ino:285`) — the burst's early exit. The vehicle deliberately
   does not zero `chuteCommands` on a reset, so this stayed true against the PREVIOUS
   release forever.

Either one alone is enough to swallow the command. In the trace above the first is what
fired, so the operator saw `EJECT already confirmed, ignoring` and no `EJECT attempt 1/5`.

**`chuteBaseline`**, new global in the main sketch, 0 at boot. The burst now tests
`lastChute > chuteBaseline` instead of `lastChute >= 1`. Identical arithmetic until a
reset moves the baseline, so a fresh session is unchanged.

**`fireConfigBurst()` returns `bool`** — whether `ul` rose. The other two callers ignore
it.

**`RESET:CHUTE` clears both latches, and only on confirmation.** On success:

```
[GCS] EJECT re-armed at ground, chute baseline 1
[GCS] the next release must exceed that to confirm
```

On failure it says so and stays latched.

## Why

**The vehicle was right to keep its counter.** `Apogee.ino:212` calls zeroing
`chuteCommands` "the one lie this system must not tell" — it would make a fired chute look
armed. The fix therefore had to live entirely on the ground, and it does: no firmware
change to the CanSat, ground unit reflash only.

**Baselining is not a new idea here.** `fireConfigBurst()` has always confirmed against a
captured `ul` baseline rather than an absolute value. The eject burst was the odd one out,
testing an absolute `>= 1`, and that asymmetry is the whole bug. The two bursts now work
the same way.

**Clearing the latches unconditionally would be worse than not clearing them.** If the
vehicle never heard the reset, its own fire latch is still set. A subsequent EJECT would
transmit, be received, increment `chute` to 2, and drive nothing — a release shown to the
operator that never happened. `ul` rising is a sound confirmation for this command
specifically: unlike `SET`, `apogeeReset()` has no rejection path, so received means
applied.

## Result

**`RESET:CHUTE` now does what `Apogee.ino:41` has always claimed** — "the ONLY way to
re-run a deployment test on a sealed unit without opening it". It never has until now. The
command reached the vehicle and cleared its latch correctly; the ground station then
refused to send the EJECT that would exercise it, so the feature was unreachable end to
end from the day it was written.

**Entry 057's Gate 8 step 3 was wrong.** It asserted `RESET:CHUTE` then `EJECT` fires
again, as an expected result. It could not. The plan was run as written and the
contradiction is what surfaced this.

**Traced, not compiled.** A Python model of both units' latch state reproduces the failure
exactly, shows the fix transmitting on the second EJECT, and confirms fresh-boot behaviour
is byte-identical between old and new. That is the strongest check available here — there
is still no Arduino toolchain, and this needs the ground unit flashed and the four-command
sequence re-run.

**A re-armed release takes `chute` to 2, and the dashboard cannot read that.**
`lib/link.ts:92` and `EjectPanel.tsx:26` both test `chute === 1` exactly, so the status
chip falls to "Unknown" and the Arm/Eject buttons return on a vehicle whose chute has
fired. Previously reachable only through an eject burst landing in two vehicle cycles;
now it is on the normal path of every bench re-test. Two lines, `=== 1` to `>= 1`, and
still unbuilt — `status.md` Next 9, the oldest debt in the tree.

**The ground OLED still reads `CHUTE CMD x1` after a re-arm.** Correct and left alone: a
chute was commanded once, and the display says so. Only the mechanism was re-armed.

**GEN3 is untouched.** It has no `RESET:CHUTE`, so it has no way to re-arm and the latch
is right there. GEN3 remains the only pair proven on hardware.
