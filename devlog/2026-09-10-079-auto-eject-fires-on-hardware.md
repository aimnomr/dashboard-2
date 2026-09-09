# 079 · Auto-eject fires on hardware, for the first time

**Date** 2026-09-10
**Type** investigation
**Refs** 052, 053, 065, 067, 069, 070, 071, 072, 073, 074, 075

Source: `logs/raw/20260910-032536-serial.log`, 1,084 packets across four vehicle boots.
Ground station build stamp `Sep 10 2026 03:23:50` — the first build carrying 070.

**The auto-eject trigger released the mechanism three times, and the ground could tell it
apart from a commanded release each time.** Eight sessions after it was written, and after
being rewritten four times in the two days before this run.

## What the log shows

### 1 · Three automatic releases

`chute` rising with `ul` UNCHANGED is the only ground-side proof a release was automatic,
and it is the pair the packet format was deliberately shaped to carry (052, and the GEN3.2
bump declined twice to preserve it):

```
seq=54   chute 0->1   ul 6->6    <<< automatic
seq=181  chute 1->2   ul 6->8        commanded
seq=186  chute 2->3   ul 8->9        commanded
seq=19   chute 0->1   ul 4->4    <<< automatic   (boot 2)
seq=28   chute 0->1   ul 1->1    <<< automatic   (boot 3)
seq=906  chute 0->1   ul 0->1        commanded   (boot 4)
```

### 2 · The trigger behaved exactly as specified

Boot 1, altitude by packet:

```
seq=45  0.3     seq=50  1.4     seq=53  1.2
seq=47  1.0     seq=51  1.6     seq=54  0.6   <- chute 0->1
seq=48  0.7     seq=52  1.7
```

Lifted by hand from 0.3 m to an apogee of 1.7 m, armed past `SET:ARM:0.5` on the way up,
lowered, and released at 0.6 m — a drop of 1.1 m against the 0.5 m threshold then
compiled. Boot 2 is the same shape: apogee 1.9 m at seq 18, released at 0.2 m, a 1.7 m
drop.

Both released in the packet immediately after the descent began, which is the reporting
behaviour 071 predicted: the mechanism is driven the moment the rule decides, and the
ground sees it on the next packet.

### 3 · devlog 067 is confirmed on hardware

```
seq=181   chute 1->2   ul 6->8
```

**`chute` +1 against `ul` +2.** One operator EJECT, two uplink packets heard, one release
counted. Before 067 that same burst would have moved `chute` by two — the counter rose per
packet received. This is the fix working, in the exact arithmetic 065 fault 2 recorded.

### 4 · devlog 070 is confirmed on hardware, including the case it was written for

The console carries the new strings and they behave as designed:

```
[GCS] EJECT attempt 3/5
[GCS] EJECT confirmed, chute 0 -> 2
[GCS] EJECT burst stopped after 3 attempt(s) - already confirmed
[GCS] vehicle restarted - chute baseline and any pending EJECT confirmation cleared
```

**The automatic release at seq 54 did NOT falsely confirm anything.** It raised `chute`
from 0 to 1 while `chuteBaseline` was still 0 — precisely the condition that, before 070,
would have left `lastChute > chuteBaseline` with `ejectConfirmed` false and silently
swallowed the next EJECT. It did not, because confirmation now requires
`ejectAwaitingConfirm`, and no burst was outstanding. The operator's EJECT two minutes
later transmitted normally and confirmed on its third attempt.

The reboot handler added in 070 also fired, on a real vehicle restart.

⚠ The confirmation line reads `chute 0 -> 2` because the baseline was still 0 while the
automatic release had already moved the counter to 1. Both numbers are true and the
message is not wrong, but it invites being read as "one command released twice". The
`X -> Y` form was chosen in 070 precisely because attempt counts were no longer knowable;
this is the cost of that choice showing up in the first log that contains it.

## What it does not show

**`SET:ARM:0.5` failed twice before it took.** Two bursts reported `NOT confirmed after 5
attempts - ul did not rise`, and a third confirmed `ul 0 -> 1`. `ul` was demonstrably
moving during the failures — it reached 6 on boot 1 — so the ground station was not seeing
the packets that would have confirmed it. That is a link-quality symptom, not a trigger
one, and it sits with `ISS-13`.

**15 consecutive `RX error code -7`** (CRC mismatch) appear in one stretch. Also link
quality.

**Nothing here validates the flight timing.** A hand lift is not a descent. The 1.7–1.8 s
and 14–16 m figures from 071/072 remain arithmetic — this run exercised the logic, the
wiring and the servo, at a descent rate of roughly a metre a second.

**Nothing here was run at flight thresholds.** The vehicle was at `ARM 0.5 / DROP 0.5`,
the bench configuration 073–075 existed to create. 076 and 078 restored `ARM 30 / DROP 2.0`
and the original bounds, and **that configuration has not been flashed or run.**

## Result

No code changed. This entry is the reading.

**The standing statement in CLAUDE.md and status.md — "auto-eject has never been tested on
hardware", carried for eight sessions — is now false and should be retired.** What replaces
it is narrower: the trigger has fired three times, by hand, at bench thresholds, on a
descent about thirty times slower than a real one, and never at the values it would fly.
