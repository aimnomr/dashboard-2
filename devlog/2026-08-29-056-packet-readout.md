# 056 · The packet is readable as numbers, and the field table comes from the backend

**Date** 2026-08-29
**Type** change
**Refs** 048, 049, 050

The channels view charted every field and answered the wrong question. A trace says what
a channel has been doing; reading its current value off one is guesswork, and a channel
that has quietly frozen draws the same flat line as one that is genuinely steady.

## What

**A numeric readout of the live packet**, at the top of the channels view, above the
charts. `frontend/src/components/PacketReadout.tsx`, with the logic in
`frontend/src/lib/packet.ts`.

One row per field, **in wire order** — `seq` first, `snr` last, 1-based indices down the
left. That ordering is the feature, not a default: laid out this way the readout lines up
with a line in the raw feed, so field 14 here is the fourteenth comma-separated value
there and a suspect number can be traced to the bytes that carried it without counting
commas by hand. Chart order would read more naturally and lose exactly that.

Five columns and a flag cell:

```
  #  field   value       unit     Δ
  1  seq        95              +1
  2  ms      102657     ms     +1000
  4  hum       51.1     %RH       ·        ▪ flat 95
 13  lat    0.00000     deg       ·        ⓘ no fix  ▪ flat 95
 18  ul           —     count     —        not in this packet
 21  rssi     -35.0     dBm    +0.5        ✳ after CRC
```

**Two signals, because the question has two directions.** The Δ column answers *is this
moving*. The `▪ flat N` marker — shown once a value has held for ten packets — answers
*has this stopped*, which is the harder one to notice and the more diagnostic. Counted in
packets rather than seconds, so a dropped packet cannot inflate the claim. The scan stops
at 300 and says `300+` rather than walking 5000 frames per field per second.

**The field table is fetched, not written.** `_session_message()` now carries
`contract`, `fields` and `outside_checksum`, and `contract.field_table()` generates the
list from `parser.FIELD_DOC` — the same table already written into every log's
`.meta.json`. Index, label, unit, precision and sentinel meanings all come from there.
Nothing in the frontend decides how a field is rendered.

**Sentinels are annotated, never substituted.** `lat` reading `0.00000` stays
`0.00000` with a faint `no fix` beside it.

**Four backend tests and twenty-one frontend tests.** 145 backend, 94 frontend, 239 total.

## Why

**The precision is the argument for fetching the table.** Five decimals on a latitude is
about a metre; three is about a hundred, and both look equally authoritative on screen.
`%.5f` is the firmware's own spec, carried through the parser and the contract untouched,
so rendering from it is the only way the dashboard shows what the vehicle actually
transmits rather than what a house default felt like.

The drift argument is the same one `types/telemetry.ts` already makes about parsing, and
the same one entry 050 makes about the sidecar: a second description of the wire format
is free to diverge from the real one, and it is believed anyway. A hand-written field
table in the frontend would have been that failure with a shorter fuse. It costs ~3 KB
once per connection and one import.

**Sentinels stay visible because this is the surface that has to match the raw line.**
Rewriting `0.00000` into "no fix" would make the packet readout the single view that
cannot be checked against the bytes, which is most of what it is for.

## Result

**A reboot suppresses every Δ, and stops a flat run.** Detected from the vehicle clock
going backwards — the same signal `timebase()` breaks the charts on. Without it a reboot
reports `seq -1480`, which reads as catastrophic packet loss when nothing was lost. The
flat run stops there too: a sensor reading the same value either side of a power cycle
was re-initialised in between, so that is two runs, and calling it one blames hardware
that has since restarted. Both are live on `20260829-125122-serial.log`, which contains
six restarts — the readout shows `flat 95` against 189 packets received, bounded by the
last of them.

**An absent field is not a frozen one.** GEN3.0 carries no `ul`, `hdop` or `fixq`, and an
SD capture carries no `rssi` or `snr`. Those rows show `—` and `not in this packet`, and
take no flat marker. Counting nulls as a run would have put `flat 300+` beside every
GEN3.1 field on an older flight and blamed a sensor for missing firmware. Verified by
replaying `FLIGHT21.CSV`, where rows 18–22 read exactly that.

**The header says `contract GEN3.1`, not `GEN3.1`.** The version describes the table the
readout is built from, not the packet on screen — a GEN3.0 vehicle renders against the
same table, and a bare version string beside it reads as a claim about the data. Which
fields that packet actually carried is stated per row. This was caught only by putting a
GEN3.0 capture through it, not by reasoning about it.

**The flat marker is deliberately not a status colour.** A flat channel is often correct:
`chute` holding 0 for a whole flight is the system working. The marker states the fact
without asserting a fault, because one that cried wolf on `chute` and `ul` would be
switched off in the head within a flight.

**`sat` and `hdop` sit flat for entire bench runs and now say so out loud.** `ISS-14` was
already known; this is the first surface that would have shown it without anyone going
looking.

**The readout is channels-only.** The flight grid stays tuned to fill one screen with the
figures that matter in the air, per the rule in `App.tsx`. Twenty-two rows of diagnostics
would cost it the thing it is for.

**Nothing here touches the packet, the parser or the firmware.** The wire format is
unchanged, `verify_gen3.py` is untouched, and no field was added to anything. This reads
what was already arriving.
