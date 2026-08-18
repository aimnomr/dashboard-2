# 018 · GEN3 plan, packet format, and link analysis

**Date** 2026-08-18
**Type** decision
**Refs** ISS-07, ISS-08, ISS-10, ISS-13

## What

Planning only — no code written, no existing behaviour changed. The only modified file in
the repo this session was `devlog/index.md`.

- `wiki/decisions/gen3-packet-format.md` — proposed GEN3 packet
- `wiki/decisions/gen3-firmware-plan.md` — flight and ground station plan
- `wiki/decisions/gen2-firmware-plan.md` — reduced to a superseded-by pointer
- `wiki/source/firmware/gen1-vs-gen2.md` — structural comparison
- `wiki/issues.md` — added `ISS-13`
- The three `.ino` sources were restored to `wiki/source/` by Aiman

## Why

Aiman is rewriting both units as GEN3, using GEN1 and GEN2 as reference, and asked for
the packet load compared with both plus guidance on spreading factor and on sharing the
band with other teams.

## Result

**`chute` and `ack` merged into one field.** Aiman pointed out the chute is servo-driven
with no feedback sensor, so there is no way to know the parachute opened — "command
acknowledged" and "servo driven" are the same fact. One field now carries both:
`0` armed, `N ≥ 1` commanded with `N` = commands received. Counting rather than flagging
keeps the uplink-quality diagnostic for free.

Consequence: **there is no fourth confirmation level and there cannot be one.** The
dashboard's "Deployed" label must become "Commanded" — the UI must not assert what no
sensor supports.

**Packet load, measured.** GEN3 is 107 bytes / 185 ms at SF7 against GEN2's 96 / 164 and
GEN1's 75 / 133. Three more fields for +11 bytes, and *more* efficient per field than
GEN2. Five precision trims were justified against actual sensor resolution, saving 6
bytes with no information lost — including a correction of earlier advice: `%.6f` on
latitude was previously called meaningfully better than `%.5f`, but at ~2.5 m receiver
accuracy the extra digit is below the noise floor.

**Spreading factor: SF7, and higher is not better.** Measured link margin at SF7 is
**70 dB at 150 m apogee** and 39 dB at 5 km. Each SF step buys ~3 dB and roughly doubles
airtime; SF10 alone exceeds the entire 1 Hz budget. There is no range problem to solve.

**The finding that matters: other teams sharing the band.** A packet is destroyed by any
overlapping transmission, so the vulnerable window is twice the airtime — 370 ms of every
second at SF7 with CSV.

| Other teams on the same frequency | Packets destroyed |
|---|---|
| 1 | 37% |
| 3 | 75% |
| 5 | 90% |

Raised as `ISS-13`, with the misconception stated explicitly: **a different sync word
does not prevent collisions.** It prevents decoding someone else's packet; it does
nothing about their RF energy landing on ours. The same applies to a team ID and to CRC —
those detect damage rather than prevent it. Only frequency separation isolates.

This also reframes SF7 as a *defence* rather than a compromise: shortest airtime is the
smallest collision target.

**Binary framing kept in reserve.** At 39 bytes versus 109 it halves collision exposure
(16% against one other team, versus 37%). Not adopted — readable packets have repeatedly
paid for themselves here — but it is the answer if a clean channel proves unavailable.

**Buffers set to 256** on both units, by construction rather than measurement.

Added to the ground station plan: count packets whose start marker is not `$TEAM_ID` and
report `[GCS] foreign packet, N so far`. That line separates "the band is busy" from "my
vehicle is silent", which are otherwise indistinguishable and look identical at exactly
the wrong moment.
