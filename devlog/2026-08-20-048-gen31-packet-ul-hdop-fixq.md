# 048 · GEN3.1 — `ul`, `hdop` and `fixq` added to the packet

**Date** 2026-08-20
**Type** change
**Refs** ISS-02, ISS-08, ISS-14

The packet format bump proposed across entries 037, 046 and 047, chosen over the `vb`
battery field and the `st` status bitmask. Three fields **appended**:

| # | field | fmt | meaning |
|---|---|---|---|
| 18 | `ul` | `%lu` | total uplink commands received — pings + ejects |
| 19 | `hdop` | `%.1f` | horizontal dilution of precision. **0.0 = not reported** |
| 20 | `fixq` | `%d` | receiver's verdict: -1 not reported · 0 invalid · 1 GPS · 2 DGPS |

Measured cost: **105 → 116 characters**, about 179 → 190 ms of airtime at SF7.

## Appended, never inserted — and this is the whole design

`hdop` and `fixq` belong next to `sat` semantically. Putting them there would have moved
`chute` from index 17 to 19, and three things read that index:

- the ground station's `parseChute()`, which drives the eject retry loop
- `FLIGHT21/22.CSV` — 1013 packets, the corpus that pins the parser against real
  firmware output, and 17-field forever
- any vehicle not yet reflashed, talking to a ground station that has been

Appending means every existing index is unchanged and the two shapes differ only in how
much they carry. `verify_gen3.py` proves it arithmetically: the GEN3.0 golden checksums
`DA98` and `AEAF` are byte-identical after the bump.

Semantic tidiness was worth less than every existing index staying put.

## The bug this nearly shipped

`parseChute()` in the ground station found the chute value by walking **back** from `*`
and taking the last field. Correct while `chute` was last. After appending it returns
`fixq` — and `fixq` is **1 on any normal GPS fix**, which `lastChute >= 1` reads as a
confirmed deployment.

The ground station would have declared EJECT successful on the first packet after arming,
stopped the burst after one attempt, and shown `EJECT CONFIRMED` on its OLED — for a
vehicle that had heard nothing. Given entries 039 to 044 were an eleven-hour investigation
into an eject that never arrived, shipping a *false confirmation* of the same command
would have been the worst possible regression.

Rewritten to count forward to a named `CHUTE_FIELD_INDEX 17`, which also survives the
next field being appended — the failure this one was, being a rule that depended on
nothing ever changing.

## Everything updated

**Firmware — flight unit**
`Config.h` (Telemetry gains `hdop`/`fixq`, `PACKET_BUF` worst case now 144),
`MRC_FlightUnit_GEN3.ino` (HDOP custom terms; `ul` = `pingCount + chuteCommands` at the
call site), `Sensors.ino` (`gpsHdop()`, fields populated), `Packet.ino` (format string,
`packetBuild()` signature).

`hdop` and `fixq` are populated **whatever the fix state**, deliberately not gated on
`fixFresh`. When the position is withheld these two are the only things that say why —
"no signal" versus "receiver says invalid" versus "module not talking" is a distinction
that lived on a dead OLED until now.

**Firmware — ground station**
`Radio.ino` (`parseChute` rewritten), `Config.h` (buffer comment; the PING section no
longer instructs anyone to watch a screen they cannot see).

**Backend**
`parser.py` — `GEN3_EXTENDED_FIELDS`, dual-shape GEN3 acceptance, `ul`/`fixq` as ints,
plausibility bounds for all three. **Two exact shapes, not "17 or more"**: 18 or 19
vehicle fields is a truncation that survived its checksum, and parsing whatever prefix
fits would put real numbers in the wrong columns.

Legacy generations now carry the three keys as `None` too, so every frame the parser
emits has the same shape whatever produced it. `None` is not `0`: `ul=0` claims the
uplink has never worked and `fixq=0` claims the receiver rejects the fix. Neither may be
manufactured on behalf of firmware that said nothing.

**Frontend**
`types/telemetry.ts`, `lib/geo.ts` (`fixStale` prefers `fixq` over the satellite count,
still ignoring -1), `panels/GpsPanel.tsx` (HDOP-led quality label, and a stale fix now
names its cause), `panels/EjectPanel.tsx` (**the uplink readout** — the answer to the
question entries 037 to 044 were argued without), `views/ChannelsView.tsx` (two charts).

**Wiki**
`gen3-packet-format.md` — no longer "Not yet agreed"; GEN3.0/3.1 both documented, with
the reasoning for appending. `pre-launch-checklist.md` section D now leads with the
dashboard, which works **sealed and at altitude** — the first time that check has been
possible at the moment it matters.

**Not touched:** `devtools/mock_source.py` is GEN2 and unaffected. Step 4 of the GEN3 plan
(a GEN3 mock with injectable gaps and CRC failures) is still unbuilt, and would now need
to emit GEN3.1.

## Result

**125 backend, 73 frontend, 14/14 `verify_gen3.py`.** Twelve new tests: six in the backend
for the dual shape — including that appending moved no existing field, and that a
half-extended packet is rejected rather than guessed — and three in the frontend for
`fixq` outranking `sat`.

Firmware not compiled or flashed; no toolchain here.

**Flash both units together.** A GEN3.1 vehicle with a GEN3.0 ground station is the exact
false-confirmation case above. The reverse is safe — a GEN3.1 ground station reads a
GEN3.0 vehicle correctly, which is what `test_gen30_packet_still_parses_after_the_bump`
and the `3.0 valid commanded` case in `verify_gen3.py` exist to guarantee.

Worth confirming on hardware once flashed: that this NEO-6M emits `$GPGGA` or `$GNGGA` at
all. If it sends neither, `hdop` stays 0.0 and `fixq` stays -1 forever — safe by design
(entry 047), and invisible without looking.
