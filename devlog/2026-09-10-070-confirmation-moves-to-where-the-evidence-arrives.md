# 070 · EJECT confirmation moves to where the evidence arrives

**Date** 2026-09-10
**Type** fix
**Refs** 044, 058, 061, 063, 064, 065, 069, ISS-17

Ground station only. Closes devlog 065 fault 1 — the fourth appearance of the
absolute-test bug, and the only fault on the release path whose cost is unbounded rather
than measured in milliseconds.

## What

### The bug, restated from the code

`fireEjectBurst()` had two exits and only the early one recorded anything. The
run-out-of-attempts exit printed `EJECT burst complete` and wrote neither `chuteBaseline`
nor `ejectConfirmed`, and the function was `void`, so `handleCommand()` had nothing to
branch on.

When the release happened but its confirming packet landed *after* the burst gave up —
the burst blocks ~1.4 s and eats the packets that would confirm it — `lastChute` rose past
`chuteBaseline` while `ejectConfirmed` stayed false. The next EJECT then skipped the
re-arm block (gated on `ejectConfirmed`), reached `lastChute > chuteBaseline` on its first
iteration, printed **`EJECT confirmed after 0 attempt(s)` and transmitted nothing at all.**

Two of the three real bursts in 065's log ended in that state, and the log ends in it:
`chuteBaseline` 4, `lastChute` 7, `ejectConfirmed` false.

### The fix

**Confirmation is not something a blocking function can wait for**, so it stopped trying.
The decision moved to `radioPoll()`, where the evidence actually arrives and where the
vehicle-reboot detection already lived:

```c
if (ejectAwaitingConfirm && lastChute > chuteBaseline) {
  ejectAwaitingConfirm = false;
  ejectConfirmed       = true;
  ejectConfirmedMs     = millis();
}
```

`ejectAwaitingConfirm` is set at the point a burst actually **transmits**, so both exits
leave it set. `ejectConfirmed = true` now happens in exactly one place in the whole
firmware.

That inverts the property that caused the bug. `lastChute > chuteBaseline` is no longer
sufficient on its own — a burst must be outstanding — so a counter left high by an earlier
release cannot confirm anything by itself. It is also the *fastest* option: a burst that
genuinely went unheard leaves the baseline valid and untouched, so the next EJECT
transmits immediately with no cooldown.

Three rules the stress test added, none of which the design holds without:

- **Vehicle reboot.** `chute` returns to 0, so no false confirmation — but the pending
  wait would stick true forever and `chuteBaseline` would describe a release count the
  vehicle no longer has. Both are cleared in the existing `ul`-fall handler, beside the
  `assumedRepeat` reset already there. `ejectConfirmed` is deliberately not touched: the
  mechanism really was driven, and the vehicle coming back does not undo it.
- **`RESET:CHUTE` mid-wait.** It moves `chuteBaseline` to a value captured before
  `fireConfigBurst()` ran, and that burst polls the radio — so a delayed confirmation
  could land during it and then satisfy the test against the *new* baseline, printing
  "confirmed" seconds after the operator was told the vehicle is freshly re-armed.
  Pending waits are discarded on a successful reset.
- **A timeout, `EJECT_CONFIRM_TIMEOUT_MS` 5000.** In SINGLE mode the vehicle's latch never
  expires, so a second EJECT drives nothing, `chute` never rises, and no packet can ever
  clear the flag. Aged out in `uplinkPoll()` — an unconditional tick — because
  `radioPoll()` only runs when packets arrive and the cases this exists for are exactly
  the ones where they stop. Expiry confirms nothing, blocks nothing and moves no
  baseline; it is the "not confirmed" answer said out loud instead of left pending.

The burst's early exit kept a job, but a smaller one: stop wasting airtime once
`radioPoll()` has confirmed. It reads the shared flag rather than recomputing the
comparison, so exactly one place decides what "confirmed" means. A post-loop re-check was
added for the confirmation that lands during the final attempt's wait — copied from
`fireConfigBurst()`, which has had that third exit all along and is the pattern 058 got
right and the eject path never copied.

### Console strings

`EJECT confirmed after N attempt(s)` is gone. N is genuinely not knowable at the new print
site — the packet can arrive cycles after the burst that caused it ended. It is replaced
by `EJECT confirmed, chute 4 -> 5`, the same counter-transition style `fireConfigBurst()`
already uses for `ul`, which says only what is actually known. `burst complete` now ends
`- awaiting confirmation` rather than implying the question is closed. Nothing parses these
(`parser.py:164` reports `[GCS]` lines and parses none), so they were free to change.

### A committed model, at last

`backend/tests/test_eject_state_machine.py` — 10 tests and one strict `xfail`, modelling
the latch in Python because `arduino-cli` is not here.

This area had **zero** test coverage. 058 diagnosed the first appearance of this bug with
exactly such a model and did not commit it; 063 and 065 then found the same bug twice
more, each time from a bench log costing a flight unit, a ground unit and an afternoon.
The regression test replays the 065 sequence directly.

The `xfail` is `strict` and records the limitation rather than hiding it: **`chute` cannot
say why it rose.** An auto-eject release moves the same counter an operator EJECT does, so
a rise seen while a burst is pending may be the vehicle acting on its own. The pre-070
in-burst test had the identical ambiguity. Resolving it needs a packet field, declined
twice on other grounds. If that test ever starts passing, someone has fixed it and the
suite will say so.

## Why

065 called this the third appearance of one bug — an absolute test where a baseline was
needed — after 058 and 063. It is really something narrower and more stubborn: **a
confirmation being decided somewhere it cannot reliably be observed.** 058 moved the
comparison to a baseline and left it inside the burst; the burst is the one place
guaranteed to be looking away when the answer arrives.

The two "confident console line for something that did not happen" cases 065 complained
about are now both gone. 067 removed the false `chute` rise on receipt; this removes the
false `EJECT confirmed after 0 attempt(s)`.

## Result

**Not compiled and not flashed.** `arduino-cli` is still not on this machine and no
hardware has run this. The ground station in every log to date is the
`Sep  7 2026 19:48:44` build, which has none of 067, 069 or this.

Backend **164 pass** (154 before, +10 here) with 1 strict `xfail`; `verify_gen3.py` 14/14.
Frontend untouched. The model is a MODEL and can drift from the C it describes — its
docstring says so, and it changes when the eject path changes or it is worse than nothing.

**The commanded release path, end to end, after 069 and 070:**

| | before | after |
|---|---|---|
| typical response | ~220 ms | **~56 ms** |
| worst case | ~446 ms | ~400 ms |
| a swallowed EJECT | **never fires** | fixed |

**Still open:** 065 fault 4 — a non-finite `alt` fires the auto-eject trigger rather than
disarming it, and there is still no `isfinite` guard anywhere in the flight firmware. The
auto path also remains where the seconds are, and both need a decision about trigger
behaviour rather than a patch. Auto-eject has still never been tested on hardware.
