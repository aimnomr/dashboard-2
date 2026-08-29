# 059 · The chute renders as a count, and never as "Deployed"

**Date** 2026-08-29
**Type** fix
**Refs** 058, S8

Rule S8 was decided on 2026-08-19 and recorded in five places. It is implemented now,
ten days later, because entry 058 put its failure case on the normal path.

## What

**`chutePresentation()` shows the count instead of matching a value.**

```
        was                              now
0       Armed                            Armed
1       Deployed                         Commanded ×1
2       Unknown          ← the bug       Commanded ×2
7       Unknown          ← the bug       Commanded ×7
null    Unknown                          Unknown
-1      Unknown                          Unknown
```

**`EjectPanel` no longer says "deployed" either.** The banner reads `Release commanded
×N`, the vehicle chip reads `Mechanism driven` rather than `Confirmed`, and the footnote
says what the signal actually is: `chute` rising reports that the mechanism was driven,
never that a canopy opened.

**The panel confirms THIS command, not the last one.** It captures `chute` when Eject is
pressed and tests `chute > chuteAtSend`. The counter is monotonic and never returns to
zero — not even on `RESET:CHUTE`, which clears the vehicle's fire latch and deliberately
leaves the count alone — so an absolute test reported a new command confirmed before it
had been sent. Same baseline shape as the ground station in 058 and the `ul` baseline in
`fireConfigBurst()`.

**Six tests**, where there were none. Frontend 94 → 100.

## Why

**Two independent errors sat in three lines, and each hid the other.** The word was wrong
because no canopy sensor exists anywhere in this system, so "Deployed" is a claim the
hardware cannot support. The test was wrong because `=== 1` is not `>= 1`. Reading the
line, the word is what draws the eye; the equality looks like a detail.

**`chute === 2` was never hypothetical.** Grepping field 18 across the captures in
`logs/raw/`: `20260829-125122` reaches 2 and `20260827-120125` reaches 3. Both predate
this entry. The dashboard had been rendering a fired chute as "Unknown", with the Arm and
Eject controls restored beneath it, on real bench data already sitting in the repo.

Two ordinary paths produce it. An eject burst is 5 attempts over ~1.5 s against a 1 Hz
vehicle, so two attempts can land in two cycles. And after 058, `RESET:CHUTE` then `EJECT`
is a supported workflow that takes the count to 2 every time.

**Showing the count cannot go stale.** Any rule that matches specific values has to be
revisited when the numbers grow. `Commanded ×N` is correct for every N that will ever
exist, which is what S8 asked for in the first place.

## Result

**Verified against the capture that caused it.** Replaying `20260829-125122-serial.log`
now shows `CHUTE · COMMANDED ×2` in the status bar and `RELEASE COMMANDED ×2` in the
Uplink panel, where both previously read "Unknown" with the Eject controls back.

**There were no tests on `chutePresentation`, and that is most of why this survived.**
Three lines, exported, rendered in the most safety-relevant chip on the screen, and
nothing pinned them. The new block includes a guard that no state ever contains the string
"deploy", so S8 is now enforced rather than remembered.

**`status.md` Next 9 is closed.** It was described there as "two lines, recorded in five
places, still unbuilt — the oldest debt in the tree". It was not two lines, and it was two
bugs rather than one.

**`wiki/decisions/frontend.md:60` needs its parenthetical removed** — it still says
*"decided, not yet implemented: `lib/link.ts` still renders 'Deployed'"*. Left for the
wiki pass; `source/` and `decisions/` are edited deliberately, not in passing.

**Nothing about the vehicle changed.** The counter still means "releases commanded, from
either path", it still never confirms deployment, and no packet field moved.
