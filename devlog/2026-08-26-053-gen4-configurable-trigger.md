# 053 · GEN4 — the auto-eject trigger is configurable from the ground

**Date** 2026-08-26
**Type** change
**Refs** —

Retuning the descent trigger no longer means reflashing the vehicle.

## What

**New sketches, GEN3 untouched.** `firmware/MRC_FlightUnit_GEN4/` and
`firmware/MRC_GroundStation_GEN4/`, copied from GEN3 and modified. GEN3.1 stays
intact and flashable — it is the only firmware pair proven on hardware (entry 051)
and losing the ability to fall back to it was not worth the tidiness.

**The packet is unchanged.** GEN3.1 byte for byte: no new fields, no checksum
change. `parser.py`, `contract.py`, the dashboard and all 214 tests needed nothing.

**New uplink grammar.** The PC prefix is stripped before transmission, as for
EJECT and PING:

```
CMD:SET:DROP:15.0   -> SET:DROP:15.0     drop threshold, m      2..100
CMD:SET:CYCLES:2    -> SET:CYCLES:2      confirm cycles         1..10
CMD:SET:ARM:50.0    -> SET:ARM:50.0      arming altitude, m     5..200
CMD:SET:AUTO:0      -> SET:AUTO:0        disable the rule
CMD:RESET           -> RESET             trigger state only
CMD:RESET:CHUTE     -> RESET:CHUTE       trigger state + the fire latch
```

`EJECT`, `PING`, `RESET` and `RESET:CHUTE` stay EXACT matches. Only `SET:` is a
prefix match, so the loosest parsing in the protocol is confined to the one command
that carries a value.

**The four `#define`s became boot defaults.** `Apogee.ino` copies them into a
runtime `cfg` struct at startup and reads the copy. A reset returns the vehicle to
exactly the compiled values; there is no NVS.

**Bounds are rejected, never clamped**, and checked in three places: `api.py` before
the bytes leave the PC, the ground station before it transmits, and the vehicle
before it applies. Three copies of the same numbers is two too many and is recorded
as such in all three files.

**`ul` is the confirmation signal.** `fireConfigBurst()` mirrors `fireEjectBurst()` —
same burst geometry — but stops early when `ul` rises rather than `chute`. The
ground station gained `parseUl()` at field 18; `parseChute()` and it now share one
`parseFieldInt()` walk rather than being two near-identical copies.

**PC side.** `api.py`'s two-entry allowlist became `translate_command()`, a grammar
that still refuses anything not matching exactly. `mock_source.py` mirrors it, per
its own docstring. New `backend/devtools/send_command.py` sends one command through
the running dashboard's WebSocket.

## Why

Every threshold change meant a reflash, and entry 052 left `AUTO_EJECT_CONFIRM_N` as
a number that could only be chosen well once a real descent rate had been measured —
which cannot happen before the flight that needs the number.

`RESET:CHUTE` exists for a narrower reason: the fire latch in `Chute.ino` is
one-shot, so a deployment test could previously be run once per power cycle and the
CanSat had to be opened between attempts.

**Two reset commands, not one**, because there are two latches and they fail in
opposite directions. Trigger state cannot drive the mechanism; the fire latch makes
a fired chute fireable again. Sharing a token would mean the harmless operation was
unavailable without the dangerous one.

## Result

Traced against a Python model of the state machine, seven scenarios. Six behaved as
designed: `SET:CYCLES` zeroes the counter so a shortened count cannot fire on
retroactive credit; `SET:AUTO:0/1` suspends and resumes with apogee intact;
`SET:ARM` does not disarm an already-armed vehicle; `RESET` after a fire re-arms the
rule but the latch still blocks it; `RESET:CHUTE` allows a re-test; a ground EJECT
after an auto-fire moves the counter without driving the mechanism.

**The seventh corrected a claim made in entry 052's session and written into the
first draft of this code.** RESET mid-descent was described as cancelling auto-eject
for the rest of the flight, on the reasoning that the vehicle would have to climb
past the arming floor again. It does not. Arming tests altitude above BOOT, not a
climb, so a vehicle still high when RESET arrives re-arms on the next cycle against
a freshly-zeroed apogee: traced, RESET at 150 m re-armed at 140 m and fired at
120 m. **RESET re-bases the trigger; `SET:AUTO:0` is the cancel.** The comments now
say so.

A power cycle behaves differently for a reason worth keeping straight: it re-zeroes
the barometric baseline too, so the vehicle returns reading ~0 m and genuinely
cannot re-arm. RESET leaves the baseline alone.

**Config changes are written to the SD card** as `#` lines mid-file. Verified that
the parser classifies `#` as status rather than as a rejected frame, so they cost
nothing on replay. They are the only in-flight record of what the vehicle was set to
do — a consequence of leaving the packet at GEN3.1, decided deliberately.

**Known limit, accepted when the packet was left alone:** `ul` rising proves the
vehicle received A command. It does not prove which, or that the value was applied
rather than refused. The ground station pre-validates so a refusable value should
never reach the air, and the applied values are readable over USB on the bench or
from the SD card after recovery — but a sealed, flying vehicle cannot be asked what
it is configured to do.

**Edge case, deliberately pessimistic:** a config burst sent before any telemetry has
arrived starts with `lastUl = -1` and can never see a rise, so it runs all attempts
and reports "not confirmed" even if packets start arriving mid-burst.

**Mixed pairs degrade safely, unlike GEN3.0/GEN3.1.** A GEN3 vehicle ignores `SET`
as foreign traffic and never moves `ul`, so a GEN4 ground station reports failure
loudly. A GEN3 ground station answers `CMD:SET:...` with `unknown command`.

**Not built:** no dashboard UI — commands go through `send_command.py`. The mock
accepts the GEN4 commands but simulates no trigger to retune. Nothing was compiled;
there is no Arduino toolchain here.
