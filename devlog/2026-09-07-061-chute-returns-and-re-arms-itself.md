# 061 · The release mechanism returns to ARMED and re-arms itself

**Date** 2026-09-07
**Type** change
**Refs** 044, 052, 053, 055, 058, `ISS-17`

The mechanism used to stay where it was driven until `RESET:CHUTE` came over the air. It now
returns on a timer, and the fire latch clears on a second, longer timer, so a release can be
commanded repeatedly without a reset between attempts.

## What

**`chuteFire()` no longer blocks.** The `delay(CHUTE_HOLD_MS)` is gone. The drive is one
register write; the return sweep and the re-arm are deadlines serviced by a new
`chuteTick()`.

**One flag became three, because they have three different lifetimes.**

```
chuteDriven      mechanism is away from rest, right now
                 cleared by the return sweep at CHUTE_HOLD_MS

chuteFired       the drive latch, suppresses repeats
                 self-clears at CHUTE_REARM_MS  (CHUTE_AUTO_REARM 1)

chuteEverFired   sticky for the boot, cleared ONLY by chuteResetLatch()
                 this is what chuteIsFired() reports
```

**`chuteTick()` is called from `loop()` and from both hold loops**
(`MRC_FlightUnit_GEN4.ino`: `loop()`, `holdUntil()`, `holdUntilListening()`), so both
deadlines are met to within a poll tick rather than to within a cycle.

**New vehicle config**, `MRC_FlightUnit_GEN4/Config.h`:

```
CHUTE_HOLD_MS     1000   repurposed: dwell at RELEASE before the horn returns
CHUTE_AUTO_REARM     1   0 restores the pre-061 one-drive-per-boot behaviour
CHUTE_REARM_MS    3000   must exceed the ground station's eject burst span
```

**The ground station stops saying no once the vehicle has re-armed.** `EJECT` was refused
outright while `ejectConfirmed` was set, and only `RESET:CHUTE` cleared it. It is now
refused only inside a cooldown mirroring the vehicle's:

```
MRC_GroundStation_GEN4/Config.h   EJECT_REARM_MS 3000
MRC_GroundStation_GEN4.ino        uint32_t ejectConfirmedMs
Uplink.ino                        cooldown-aware console guard; ejectConfirmedMs stamped
                                  at the point ejectConfirmed is set
```

Past the cooldown the ground re-arms itself the way `RESET:CHUTE` does — by moving
`chuteBaseline` to the current `lastChute`, never by asking the vehicle to zero anything.

**Carried across every document and comment that described the old behaviour.**

```
COMMANDS.md                     the EJECT/RESET:CHUTE notes; two claims that were
                                already stale before today (see Result)
firmware/.../Apogee.ino         the two-latch header, and why plain RESET after a
                                fire still does nothing useful
firmware/.../MRC_FlightUnit_GEN4.ino   sketch header
firmware/.../GroundStation/Config.h    the RESET:CHUTE warning block
frontend/src/lib/link.ts        the three routes by which chute reaches 2
frontend/src/panels/EjectPanel.tsx     the `commanded` gate, marked as an open gap
wiki/decisions/frontend.md      the EJECT requirements, and rule S8's status
```

## Why

**A servo at `RELEASE_DEG` has nowhere to sweep from.** `chuteResetLatch()` did write the
armed angle, so the horn did return — but only on a command that has never once succeeded
over the air. On the bench every repeat release cost two commands, and `ISS-17` means the
second of them was broken from the day it was written until 058.

**The re-arm delay has to outlast the eject burst, and that is the whole reason it is not
`CHUTE_HOLD_MS`.** `chuteCommands++` sits *outside* the fire latch
(`MRC_FlightUnit_GEN4.ino:159,287`), so the counter already rises on every received eject
packet whether or not anything was driven. The burst is 5 attempts 300 ms apart:

```
span = (EJECT_ATTEMPTS - 1) * (EJECT_RETRY_MS + airtime) = 4 * ~351 = ~1404 ms
```

Clearing the latch at 1000 ms would put attempts 4 and 5 of a *single operator EJECT* past
the re-arm, driving the mechanism a second time from one command. 3000 ms leaves ~1.6 s of
margin. This is the same coupling `Uplink.ino` already documents between `EJECT_RETRY_MS`
and the vehicle's `LISTEN_WINDOW_MS`, and it is just as silent when it breaks.

**`chuteEverFired` exists so the apogee logic does not notice any of this.**
`apogeeUpdate()` guards on `chuteIsFired()` at `Apogee.ino:160` to avoid auto-ejecting after
the ground already released. Had `chuteIsFired()` reported the self-clearing latch, that
guard would have expired 3 s after a commanded release and auto-eject would have become
eligible again mid-flight. Reporting the sticky flag instead keeps the auto-eject path
byte-identical to 053.

**Re-capturing `chuteBaseline` on the ground is not optional.** `lastChute` is already above
the old baseline from the previous release, so a cooldown re-arm that left it alone would
make `fireEjectBurst()` confirm on its first test and transmit nothing — the precise failure
058 diagnosed for `RESET:CHUTE`, reintroduced through a new door.

**Removing the delay closes a violation the code already admitted to.** The old comment
called the 1000 ms hold "a knowing violation of the 1 Hz rule, not an oversight", and
invited exactly this fix: *"drive the servo without blocking and let it travel across the
next cycle."* It cost one late packet at the moment of deployment, which is the moment the
ground most wants a packet.

**Refusing to send is safer than sending into a latched vehicle.** Inside the cooldown the
console still says no, because a burst sent then would be received, counted into `chute`,
and drive nothing — showing the operator a release that did not happen. `EJECT_REARM_MS`
shorter than `CHUTE_REARM_MS` recreates that, which is why both files say to change them
together and which direction is dangerous.

## Result

**⚠ Not compiled and not flashed.** `arduino-cli` is not installed on this machine, so
nothing here has been through a compiler. `firmware/tests/verify_gen3.py` still passes 14/14
but exercises Python packet and CRC parsing only — it does not touch firmware C and is not
evidence for this change. The first real test is a build.

**`chute` in the packet is unchanged, and still does not count releases.** It counts eject
packets *received*: one operator command whose burst lands twice moves it by 2 and drives
the mechanism once. That was already true before this entry — it is the ordinary
explanation for the `0 -> 2` steps in `20260905-024859-serial.log`, which 059 had already
described and which were briefly mistaken for reboot artefacts while reading the Sep 5
captures. A counter that means "mechanism drives" would need either a packet field, which
`status.md` records as declined twice, or a redefinition of `chute` that would break the
ground's `lastChute > chuteBaseline` confirmation. Neither was done.

**The bench loop is now `EJECT` … wait 3 s … `EJECT`.** `RESET:CHUTE` is still supported, is
still immediate, and is still the only way to re-arm a vehicle built with
`CHUTE_AUTO_REARM 0`. It is also still the only thing that clears `chuteEverFired`.

**`ISS-17` remains unproven on hardware.** Nothing in this entry tests it. The gate is
unchanged: `PING → EJECT → RESET:CHUTE → EJECT`, which no capture in `logs/raw/` has ever
run — `RESET:CHUTE` appears in none of them.

**The non-servo path changed behaviour too.** With `CHUTE_USE_SERVO 0` the pin used to latch
HIGH until a reset; it now returns LOW at `CHUTE_HOLD_MS` like the servo returns to ARMED.
For a burn wire that is the correct behaviour and arguably a fix, but it is inert today
(`CHUTE_USE_SERVO 1`) and has never been run on that hardware.

**GEN3 is untouched.** It has no `RESET:CHUTE` and no re-arm, and its mechanism still stays
where it was driven.

**The propagation pass found two claims that were already false before 061.**
`COMMANDS.md` still described `fireEjectBurst()` as testing `lastChute >= 1`, which 058
replaced with `lastChute > chuteBaseline` nine days earlier, and still told the reader "the
dashboard currently renders only `chute === 1` as deployed", which 059 fixed on the same
day it was written. Both are corrected. `wiki/decisions/frontend.md:60` carried the
"decided, not yet implemented" parenthetical that 059 explicitly deferred to a wiki pass;
that pass happened here.

**The dashboard cannot send the second release, and that is now an open decision rather
than a silent gap.** `EjectPanel` replaces the Arm and Eject controls with a banner as soon
as `chute > 0` and never restores them, because `chute` is monotonic and only a vehicle
reboot returns it to 0. The firmware now supports repeat releases that the only graphical
path to the uplink cannot reach; `send_command EJECT` is the workaround. Restoring a
parachute control after a release needs its own arming shape and was not designed here.
Recorded in `wiki/decisions/frontend.md` and in the panel itself.

**`backend/devtools/mock_source.py` does not model any of this.** It clears a
`_chute_deployed` flag on `RESET:CHUTE` and has no notion of a re-arm timer. Left alone: it
still emits GEN2 with no `$MRC` and no CRC, so it cannot exercise GEN4 at all and a partial
fix would only make it look more current than it is. Fourth session on that list.

**`COMMANDS-QUICK.md` was deliberately not touched.** It carries invocations and nothing
that can go stale — no counts, no behavioural claims — which is the property that keeps it
correct without maintenance. The `RESET:CHUTE` warning on its one relevant line is still
true.

**The comment in `chuteResetLatch()` was corrected.** It claimed the horn "stays wherever it
travelled to", which contradicted the armed-angle write three lines below it and is doubly
wrong now.

**Two new values are duplicated across the two Config.h files** — `CHUTE_REARM_MS` and
`EJECT_REARM_MS` — with nothing pinning them together. Third instance of this shape after
`CHUTE_PIN` (057) and the auto-eject bounds. The failure mode here is a confirmed release
that never happened.
