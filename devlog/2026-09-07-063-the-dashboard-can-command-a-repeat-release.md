# 063 · The dashboard can command a repeat release

**Date** 2026-09-07
**Type** change
**Refs** 058, 059, 061, S8

061 made the vehicle and the ground station accept repeat releases. The dashboard was the
last gate, and it was the only one that had never been deliberate.

## What

**The banner and the controls now coexist.** `EjectPanel` rendered *either* the
`Release commanded ×N` banner *or* the Arm/Eject pair, on `chute > 0`. Since `chute` is
monotonic and only a vehicle reboot returns it to 0, the controls left on the first release
and never came back. Both are now rendered; `commanded` reports and no longer gates.

**A cooldown replaces the latch.** The Eject button is disabled for `EJECT_REARM_MS` after
`chute` is seen to RISE, and reads `Re-arming · Ns` while it counts down. `rearmPresentation()`
lives in `lib/link.ts` beside `chutePresentation()`, as a pure function, because this
frontend has no component-test setup and untested UI logic in this panel is exactly what
059 was about.

**Tracked as a rise, not as a value.** Opening the dashboard on a vehicle that fired an hour
ago does not disable the control — that vehicle re-armed long ago. A fall (a reboot zeroing
the counter) records nothing, because a rebooted vehicle has an armed mechanism and no
cooldown to serve.

**The arming step is unchanged and applies to every shot.** `Arm` still lapses after
`ARM_TIMEOUT_MS`, and a repeat is not cheaper to fire than the first one. The button reads
`Eject again` once something has been commanded, so the operator is never told they are
arming a fresh vehicle.

**Seven tests**, frontend 100 → 107.

## Why

**The panel's gate was a side effect, not a decision.** 059 changed what `chute > 0` *said*
and left what it *did* alone. The banner replacing the controls made sense when a second
release required `RESET:CHUTE` — a command this panel does not offer — but it survived into
a world where the firmware supports repeats, and it made the only graphical path to the
uplink the one path that could not use them.

**A cooldown is the honest shape, and refusing here is a courtesy rather than a safeguard.**
The ground station refuses independently and the vehicle's own latch is what actually
decides. Pressing through this would not fire the mechanism — it would raise `chute` and
report a release that never drove anything, which is worse than refusing.

**Three copies of one number now exist**, in three languages: `CHUTE_REARM_MS`,
`EJECT_REARM_MS`, `EJECT_REARM_MS` in `link.ts`. The browser cannot read either firmware
constant. A test pins the value so the copy cannot drift silently, and the comment records
which direction is dangerous — this copy should be the longest of the three.

## Result

**All four gates that blocked a repeat release are now open**, and each for its own reason:

```
G1  EjectPanel           this entry — cooldown, not a latch
G2  handleCommand()      061 — EJECT_REARM_MS
G3  fireEjectBurst()     061 — baseline re-captured on re-arm
G4  chuteFire()          061 — CHUTE_REARM_MS
```

**Auto-eject is deliberately NOT repeatable, and this entry does not touch it.** The descent
condition stays true for the whole descent: once the vehicle is `DROP` metres below apogee
and still falling, every subsequent cycle satisfies the rule. Without `autoEjectFired` the
mechanism would be driven every second for twenty or thirty seconds — continuous current
draw on a vehicle that already reboots around eject activity. Repeating that path needs a
re-arm CONDITION, not a removed latch; the obvious one is requiring another climb past
`armAltM`. Not designed, not built.

**A manual release still disables auto-eject for the boot**, through `chuteEverFired`, and
plain `RESET` does not clear it. That trap is older than this entry (053) and repeat manual
releases make it much easier to walk into: bench-test the release, seal the unit, fly it,
and the automatic trigger never fires. `RESET:CHUTE` or a power cycle before flight.

**Nothing here has been exercised against hardware.** The tests cover the cooldown
arithmetic, not the panel, and there is still no component-test setup in this frontend. The
first real check is a bench run with a vehicle flashed past 061 — which, at the time of
writing, has not been confirmed to exist: the run in `20260907-164532-serial.log` carries a
ground station build stamped an hour before 061 was committed.
