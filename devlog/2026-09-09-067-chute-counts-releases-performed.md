# 067 · `chute` counts releases performed, not eject packets received

**Date** 2026-09-09
**Type** fix
**Refs** 044, 059, 061, 064, 065, ISS-17

Written after the fact — the change was committed in `ec6f068` and is recorded here.
Closes fault 2 of devlog 065, by a different route than the one 065 proposed.

## What

`chuteFire()` now returns whether it **actually drove the mechanism**, and every caller
increments the counter on that return instead of on its own receipt:

```c
bool chuteFire() {
  if (chuteFired) return false;      /* repeat inside the latch - drove nothing */
  …
  return true;
}
```

Three call sites, all in `MRC_FlightUnit_GEN4`:

| site | before | after |
|---|---|---|
| front listen window (`.ino:174`) | `if (radioListenForEject(…)) { chuteCommands++; chuteFire(); }` | `if (radioListenForEject(…) && chuteFire()) { chuteCommands++; }` |
| back-half hold (`.ino:312`) | `if (… radioServiceUplink()) { chuteCommands++; chuteFire(); }` | `if (… radioServiceUplink() && chuteFire()) { chuteCommands++; }` |
| auto-eject (`.ino:196`) | `if (apogeeUpdate(tm.alt)) { chuteCommands++; chuteFire(); }` | `if (apogeeUpdate(tm.alt) && chuteFire()) { chuteCommands++; }` |

The auto-eject guard cannot currently fail — `apogeeUpdate()` already refuses on
`chuteIsFired()`, so the latch cannot be set when that line is reached. It is written in the
same shape anyway, on the reasoning recorded in the source: a guard that is unreachable
today is cheaper than one that is missing when the guard above it moves.

**A second, separate correction rode along.** `radioListenForEject()` was the one poll loop
in the cycle that never called `chuteTick()`, so a release driven early in the 400 ms
listen window had its return sweep deferred to `holdUntilListening()` later in the same
cycle — up to ~450 ms late. `chuteTick()`'s own comment claimed deadlines were met to within
`LISTEN_TICK_MS`, and for that window the claim was false. All four poll loops now service
it: `loop()`, `holdUntil()`, `holdUntilListening()` and `radioListenForEject()`.

Harmless as the constants stand — `CHUTE_HOLD_MS` is 1000 and the horn simply dwelled
longer. It stops being harmless the moment `CHUTE_HOLD_MS` is tightened toward the width of
the window, so the loop was corrected rather than the comment.

`COMMANDS.md` was updated in the same commit, including the paragraph 065 flagged as wrong.

## Why

065 fault 2: the two intake paths disagreed about what `chute` meant.
`radioServiceUplink()` returned per packet and `radioListenForEject()` collapsed a whole
400 ms window into one bool, so an eject burst landing in the back half counted every
attempt and one landing in the front counted once. Burst spacing (~351 ms) is under the
window width by construction, so two attempts in one window was routine rather than rare.

065 proposed returning a **count** from `radioListenForEject()` and adding it — one line
each side, which would have made both paths agree on "packets received".

**Counting on the drive was chosen instead, and it is the better fix.** Returning a count
would have made the two paths agree on a number nobody needs: receipt is what `ul` is for,
and `ul` already rises on every packet the vehicle hears, including the four remaining
attempts of a five-shot burst. One field per question. 061 put the increment outside
`chuteFire()` deliberately — the reasoning then was that the ground needs to see receipt
whether or not anything was driven — and `ul` had already made that reasoning redundant.

`CHUTE_REARM_MS` (3000 ms) is still longer than the burst span (~1404 ms), so one operator
EJECT still cannot drive the mechanism twice.

## Result

**Not compiled and not flashed.** `arduino-cli` is still not on this machine, and the
ground station in every log from today is still the `Sep  7 2026 19:48:44` build. No
hardware has run this code. Backend 154 and frontend 121 pass, `verify_gen3.py` 14/14, but
none of those exercise flight firmware.

**`chute` now means something different, and three documents still carry the old meaning.**
`COMMANDS.md` was corrected in the same commit. `CLAUDE.md:123` and `status.md:161` were
not, and both still tell a reader that the counter rises per packet and that one EJECT
moves it by about three. `CLAUDE.md` is the file every new session reads first.

`MRC_FlightUnit_GEN3` was not touched and still counts on receipt. That is a divergence of
the kind this project keeps paying for — `CHUTE_PIN` (057), the sync word (060), the GPS
pins (062), `VEHICLE_DEFAULT_REPEAT` (064) — but it is the defensible direction here: GEN3
has no auto-eject and no `SET`, and changing a generation nothing is flying against gains
nothing. Recorded so it is a decision rather than an oversight.

**One behaviour changed that is worth stating plainly.** A repeat EJECT arriving while the
vehicle's latch still holds now moves `chute` by nothing, so the ground station's burst
runs to `burst complete` and reports no confirmation. Before this change the counter rose
on receipt and the ground station printed `EJECT confirmed after n attempt(s)` for a
command that drove nothing — one of the two "confident console line for something that did
not happen" cases 065 complained about. **That false confirmation is gone.** What replaces
it is an honest non-confirmation, which is the correct outcome and also feeds fault 1: a
burst that ends without confirming is the state fault 1 mishandles, and fault 1 is still
open and still unfixed in `fireEjectBurst()`.
