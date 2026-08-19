# 051 · The uplink works, and there is a log of it

**Date** 2026-08-20
**Type** investigation
**Refs** ISS-02, ISS-14

Closes the investigation that ran from entry 039 through 044.

## What

Aiman flashed GEN3.1 to both units and ran a session at 01:58. From
`logs/raw/20260820-015822-serial.log`:

```
[GCS] PING queued                x3      ->  ul  0 -> 1 -> 2 -> 3
[GCS] EJECT armed
[GCS] EJECT attempt 1/5
[GCS] EJECT attempt 2/5
[GCS] EJECT attempt 3/5
[GCS] EJECT confirmed after 3 attempt(s) ->  chute 0 -> 2,  ul 3 -> 5
```

The counters move in the telemetry itself. This is not an inference from a serial print
on the vehicle's own USB; it is the vehicle reporting, over the air, how many commands it
has received.

## What this settles

**The timing diagnosis was correct.** Entries 039, 040 and 043 argued that the ground
station was transmitting into the vehicle's deaf period — 45 transmissions across two
sessions, never heard. A phase-independent burst landed on **attempt 3 of 5**, inside the
window the burst was deliberately sized to guarantee (entry 044: spacing under the listen
window, span over the cycle period). Three of five is what that arithmetic predicts.

**`ISS-02` is resolved.** It has been wrong in three ways over the project's life: first
"the ground firmware is missing" (it existed, entry 043), then "it transmits but is never
heard" (true, entry 039), now neither. It should be rewritten rather than silently closed.

## Four things proved themselves in the same log, none previously observed

**`ul` is the witness entry 037 asked for.** Three PINGs incremented it *before* EJECT was
attempted. The non-destructive pre-launch check finally works, from the dashboard, on a
sealed unit — the exact capability that entry 037 identified as missing and that entries
039 to 044 were all argued without.

**`parseChute()` read `chute=2` correctly** from a 20-field packet. That is the
index-forward rewrite from entry 048 doing its job; the walk-back version it replaced
would have read `fixq`.

**The GPS staleness fix works.** `fixq` went 1 -> 0 partway through and `lat`/`lng` went to
`0.00000` with it, rather than freezing on a position the vehicle could no longer confirm
— the exact fault entry 046 was written for, now demonstrably not happening.

**HDOP is arriving**: 15.3 early, 25.5 later. Both poor, and previously invisible.

## What it does not settle

`chute >= 1` means the vehicle received the command and drove the servo. **It does not mean
the parachute opened.** There is no feedback sensor and there never has been; no reading in
this log or any other can confirm a canopy. S8 stands.

The link itself is now the open problem. The same session shows **53% cumulative packet
loss**, RSSI at **-109 dBm** early before recovering to -24, and 32 `RX error code -7`
checksum failures. Rolling loss ended at 0%, so this is position or interference rather
than a constant fault — which makes it `ISS-13` arriving in measured form for the first
time, and the largest thing standing between here and a launch.

## Result

Nothing built. This entry exists because the result was recorded only in `status.md`,
which is rewritten every session, and the devlog is what survives.

Sessions 1 to 3 built the dashboard. Session 4 found out whether it was telling the truth.
