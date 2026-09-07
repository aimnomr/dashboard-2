# 060 · The GEN4 sync word moved off `0xAB`

**Date** 2026-09-07
**Type** change
**Refs** 053, `333468e`

Recorded after the fact. The change was committed directly as `333468e` on 2026-09-07; this
entry exists because the devlog records every executed change, and a one-line radio config
edit that silently strands two flashed units is not a change to leave unrecorded.

## What

**`SYNC_WORD` `0xAB` → `0xAA`, on both GEN4 ends.**

```
firmware/MRC_FlightUnit_GEN4/Config.h:24     0xAB -> 0xAA
firmware/MRC_GroundStation_GEN4/Config.h:27  0xAB -> 0xAA
```

Nothing else moved. Frequency, bandwidth, spreading factor, coding rate and TX power are
untouched, and the `$MRC` packet marker is a separate mechanism at a separate layer.

**GEN3 was deliberately not changed.** `MRC_FlightUnit_GEN3/Config.h:20` and
`MRC_GroundStation_GEN3/Config.h:23` remain `0xAB`.

## Why

From the commit message:

> Changed the default sync word since the AI will prefer to use this word so changed to
> something else to not make it similar to others

`0xAB` is the value that turns up in example code and in generated sketches, which makes it
the value another team in the same field is most likely to be sitting on. The sync word is
the radio's first cheap filter — traffic with a different one is rejected in hardware,
before a packet is ever assembled — so sharing it with a neighbour spends receiver time and
CRC checks on frames that were never ours.

This does not make the link private and is not meant to. `ISS-13` is still the real
frequency-coordination problem and no config value closes it.

## Result

**Both GEN4 ends moved together, so they remain consistent with each other.** The pair is
correct as committed.

**⚠ Neither unit has been reflashed.** The units on the bench carry the `Sep 4 2026
23:42:49` build, which is `0xAB`. Until BOTH are flashed with this change the link is dead,
and it presents as a total receive failure — no packets, no error — not as a config
mismatch. Flashing only one end produces exactly the same silence.

**GEN3 and GEN4 no longer interoperate at all.** They were already separate sketches with
separate packet expectations, but a GEN3 ground station could previously hear a GEN4
vehicle's carrier. That is now false at the radio layer.

**The value is duplicated across two files with nothing pinning them together.** Same shape
as the `CHUTE_PIN` divergence in 057 and the auto-eject bounds noted in `status.md`. No
check exists that would catch the two drifting apart; the failure mode is silence.
