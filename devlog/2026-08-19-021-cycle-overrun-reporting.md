# 021 · Cycle overrun reporting and resync

**Date** 2026-08-19
**Type** fix
**Refs** —

## What

Added overrun detection to the flight unit's deadline scheduler, and documented the one
place that knowingly breaks the cadence rule.

## Why

Found during a review pass before bench testing. The deadline scheduler held 1 Hz
correctly in the normal case, but two situations were unhandled.

**A cycle that overran was silent.** The budget says ~650 ms of a 1000 ms period, and the
SD write is the term least predictable from a datasheet — it is exactly what the plan says
to measure on hardware. Without a report, an overrun would only show up later as
mysteriously irregular telemetry.

**A large overrun would have caused a catch-up burst.** `nextCycleAt += CYCLE_PERIOD_MS`
each iteration means falling a full period behind leaves the next deadline already in the
past, so `holdUntil()` returns immediately and cycles run back to back until the schedule
is caught up. A burst of packets breaks cadence in the other direction and floods a
channel that may be shared with other teams.

## Result

Overruns are counted and printed with the amount. Falling more than a whole period behind
resynchronises `nextCycleAt` to now, so the cost of a stall is one late packet rather than
a burst.

The servo path in `Chute.ino` is now marked as a **knowing** violation: its 1000 ms hold
lands on top of ~650 ms of work, so the cycle it fires on will overrun. Judged acceptable
— the release matters more than that packet — but recorded as a decision rather than left
as an oversight. The non-blocking alternative is noted in place for whoever picks the
servo.

Default configuration (`CHUTE_USE_SERVO 0`) has no delay and is unaffected.
