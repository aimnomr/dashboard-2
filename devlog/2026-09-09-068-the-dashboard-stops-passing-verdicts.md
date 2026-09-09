# 068 · The eject control leaves the dashboard, and the attitude panel stops judging

**Date** 2026-09-09
**Type** decision
**Refs** 049, 059, 063, 064, ISS-08

Written after the fact. Two subtractive UI decisions taken on 2026-09-09 and committed in
`7c81c00` and `ec6f068`. Both reduce what the dashboard does on the operator's behalf, and
they are recorded together because they were taken together — not because one implies the
other.

## What

### 1 · The Arm and Eject controls were removed from the dashboard

`EjectPanel` no longer carries a release control, and there is no keyboard shortcut for one
either. The panel keeps the chute state indicator and PING. Arm went with Eject: it existed
only to gate Eject and had nothing left to guard.

`sendCommand('ping')` is now the only call site in the entire frontend.

**The command path is untouched.** `backend/dashboard/api.py` still maps `EJECT` to
`CMD:EJECT`, `devtools/send_command.py` is unchanged, and a manual release is sent from a
terminal with `python -m devtools.send_command EJECT`. What changed is the surface, not the
capability.

This reverses the direction of 063, which existed to make the repeat release from 061
reachable from the only graphical path to the uplink. That path is now closed again, by
choice rather than by the 058-class bug 063 was fixing.

### 2 · A `SET:DROP` control lived in the panel for part of the day and was removed

Recorded in the panel's own source comment. It never appeared in a commit in its working
state — `ec6f068` carries only the aftermath. `dropValidation()`, `DROP_MIN_M` and
`DROP_MAX_M` remain in `lib/link.ts`, still pinned against the backend by a test, for
whenever a control wants them again. `send_command SET:DROP:15` is unaffected.

This is the sixth place the auto-eject bounds are written down.

### 3 · The attitude panel no longer says when the attitude is untrustworthy

Three signals were removed from `AttitudePanel` and `PoseView`:

- the pose model desaturating to grey when the accelerometer is not a usable reference
- the artificial horizon desaturating for the same reason
- the `Attitude unreliable — <reason>` banner

Every attitude now renders exactly as any other does. `computeAttitude()` still returns
`reliable` and `reason`, `attitudeWarning()` is still exported and still tested, and
`UNRELIABLE_CONFIRM` and the two-frame confirmation are all intact in `lib/attitude.ts`.
The panel simply reads none of it. `AttitudePanel` no longer takes `history` at all.

## Why

**On the eject control**, from the source: the record of a commanded release does not
depend on the button existing. `ul` rises in every packet the vehicle sends, which reaches
the raw log, the SD card and every replay; and the ground station's `[GCS] EJECT armed` /
`attempt n/5` / `confirmed after n attempt(s)` lines are written verbatim by `rawlog.py`,
which filters nothing by design. `chute` rising with `ul` unchanged remains the only
ground-side proof that a release was automatic, and that pair still means what it meant.

The unstated half is the obvious one: a parachute release is the one irreversible thing
this system can do, and a button on a browser page is a poor place to keep it. A terminal
command is harder to press by accident and leaves a shell history.

**On the attitude signalling**, from the source: the panel is to show the attitude it has at
every instant, with nothing in the rendering discerning one pose from another. What the
verdict was computed *from* stays on screen as numbers — magnitude as the `g` figure in the
panel note, spin rate in the readout — so the operator reads those and draws their own
conclusion. The thresholds were measured from real logs and are worth keeping, and
restoring the display is a smaller change than rebuilding it.

## Result

Frontend 121 tests pass and `npx tsc --noEmit` is clean, `noUnusedLocals` included — the
`history` prop and the `reliable` parameter were removed from every signature that carried
them, not left dangling.

Two things to hold against these, both open:

**The attitude decision cuts against the project's stated principle.** `CLAUDE.md` opens
with *"a number shown on the dashboard that the system cannot actually support is the
failure mode this whole project is organised against"*, and the greying was the mechanism
enforcing exactly that for attitude: under boost, in freefall or tumbling above 90 deg/s,
the accelerometer is not an attitude reference and the pose shown is not the pose. A
confident-looking model during ascent is a claim the system cannot support. The argument
that the raw inputs are still on screen is real, but it moves the judgement from the panel
to the operator at the moment the operator has least attention to spare. **Worth a second
look before launch; not overturned here.** The machinery is all still in `lib/attitude.ts`,
so restoring the display is a small change, exactly as the source comment says.

**`status.md` Next 12 is now wider, not narrower.** It read "no dashboard UI for the GEN4
commands — `send_command` remains the only path". After today the dashboard sends `PING`
and nothing else: it cannot fire a release, cannot set the release mode, cannot see the
release mode, and cannot set the auto-eject bounds. That may well be the right shape for a
flight, but it should be a decision that was made rather than a gap that widened, and the
pre-launch checklist should say which terminal is holding the eject command and who is
sitting at it.
