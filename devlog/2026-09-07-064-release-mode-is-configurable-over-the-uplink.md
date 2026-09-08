# 064 · SINGLE and MULTI release modes, switchable from the ground

**Date** 2026-09-07
**Type** change
**Refs** 053, 058, 061, 063

061 made repeat releases possible and 063 made them reachable. Both were unconditional.
This adds the switch, and settles what it is *not* allowed to change.

## What

**`SET:REPEAT:0|1`**, a fifth key in the existing GEN4 SET grammar.

```
1  MULTI   the drive latch expires after CHUTE_REARM_MS; repeat releases can be commanded
0  SINGLE  one drive per boot; RESET:CHUTE is the only re-arm      (pre-061 behaviour)
```

**`CHUTE_AUTO_REARM` became the power-on DEFAULT rather than the decision.** The `#if` in
`chuteTick()` is now a runtime test against `chuteRepeat`, initialised from the define and
moved by `chuteSetRepeat()`. A reboot forgets the override, exactly as the apogee config
returns to its compile-time defaults.

**The ground station tracks the mode and refuses differently in each.** In SINGLE it says
so immediately instead of counting down a cooldown that will never help:

```
[GCS] EJECT already confirmed, ignoring - release mode is SINGLE
[GCS] send RESET:CHUTE to re-arm, or SET:REPEAT:1 for repeat releases
```

**The vehicle's restart resets that assumption.** `radioPoll()` treats a FALL in `ul` as a
reboot — the counter only climbs within a boot — and returns `assumedRepeat` to
`VEHICLE_DEFAULT_REPEAT`.

**`repeat=` added to the SD `#` config line**, which is the only in-flight record of what
the vehicle was actually configured to do.

**Nine backend tests**, 145 → 154. Three of them read the firmware headers directly.

## Why

**Auto-eject is deliberately excluded, and that was the whole question.** The descent
condition does not stop being true once it has fired: a vehicle `DROP` metres below apogee
and still falling satisfies the rule on every subsequent cycle. An expiring latch there
would drive the mechanism every second for the length of the fall — twenty or thirty sweeps
and continuous current draw, on a vehicle that already reboots around eject activity.
Repeating that path safely needs a re-arm *condition* — a fresh climb through `armAltM` —
and the decision taken was that the ground command stays the only re-arm. So `SET:REPEAT`
governs the commanded path and nothing else, and `autoEjectFired` and `chuteEverFired` are
untouched.

**Uplink rather than a compile-time define, because the unit is sealed.** That is the
problem GEN4 exists for. The cost is a fifth value in the `api.py` / ground / vehicle /
`COMMANDS.md` mirror, paid knowingly.

**`VEHICLE_DEFAULT_REPEAT` is the dangerous one, so it has a test rather than a comment.**
It is a hand-kept copy of `CHUTE_AUTO_REARM`, and it is the only mirrored constant in this
system the ground can never verify at runtime: GEN3.1 has no config fields, so the ground
station cannot read the vehicle's mode and only ever knows what it last told it. If the two
drift, the console believes a rebooted vehicle is in MULTI when it is in SINGLE, sends an
EJECT the vehicle refuses to act on, sees `chute` rise on the received packet, and reports a
release that never happened — devlog 058's failure arriving through a constant nobody
thought of as protocol. `CHUTE_PIN` (057), the sync word (060) and the GPS pins (062) all
drifted exactly this way. This one is pinned by
`test_the_ground_station_assumes_the_vehicles_actual_default_release_mode`.

**It fails to SINGLE.** Wrong in that direction costs a needless `RESET:CHUTE`. Wrong in the
other direction it lies about a release.

## Result

**The ground's belief is an assumption and is labelled as one everywhere it appears** — in
the variable's comment, in the operator message, and in `COMMANDS.md`. The `#` lines on the
SD card remain the only truthful record of the vehicle's config, readable after recovery and
not before. That is the accepted cost of leaving the packet at GEN3.1, reaffirmed here
rather than reopened.

**A test now pins the cooldown relationship too.** `EJECT_REARM_MS >= CHUTE_REARM_MS`, the
direction that is silent when it breaks, and previously only a comment in two files.

**A test pins that the vehicle knows every SET key the backend will send**, by grepping
`Apogee.ino` for each key in `GEN4_SET_KEYS`. `ul` rises whether the vehicle applies a value
or rejects it, so a key the firmware does not know looks confirmed from the ground and
changes nothing.

**Turning repeat off does not re-latch an already re-armed mechanism.** `SET:REPEAT:0`
applies from that moment forward, like `SET:AUTO:0` leaving apogee state intact. For a known
state it is `SET:REPEAT:0` then `RESET:CHUTE`.

**The dashboard cannot set the mode and does not know it.** `EjectPanel`'s cooldown from 063
is unconditional, so on a SINGLE-mode vehicle its Eject button re-enables after three
seconds and the ground station refuses the command. The operator sees the refusal in the raw
feed. Not fixed here: there is still no dashboard UI for any GEN4 command, and building one
for this key alone would be the wrong shape.

**Nothing is compiled or flashed.** `arduino-cli` is not installed on this machine. Backend
154 and frontend 107 pass; neither covers firmware C.
