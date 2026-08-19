# 040 · The flight unit's command path is not the fault

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-02

## What

Aiman's hypothesis after entry 039: the EJECT command is not properly received or processed
*by the flight unit*. That is a different claim from the timing diagnosis, and it is worth
settling on its own — if the vehicle has no working command path, the ground-station timing
fix in 039 would be a fix to the wrong unit.

Audited the receive path by inspection, diffed the radio configuration on both sides, and
analysed a second hardware run.

**1 · The command path exists, is complete, and is enabled.**

| Step | Location |
|---|---|
| `ENABLE_UPLINK` is `1` | `MRC_FlightUnit_GEN3/Config.h:47` |
| Every cycle, *first* action: `radioListenForEject(400)` | `MRC_FlightUnit_GEN3.ino:115-121` |
| `startReceive()`, poll DIO1 on a 5 ms tick, `readData()`, `trim()` | `Radio.ino:38-53` |
| `incoming.equals(EJECT_TOKEN)` → `heardEject = true` | `Radio.ino:58-61` |
| → `chuteCommands++`, `chuteFire()`, `[FLT] EJECT received, count N` | `MRC_FlightUnit_GEN3.ino:117-120` |
| `incoming.equals(PING_TOKEN)` → `pingCount++`, `[FLT] PING received, count N` | `Radio.ino:66-73` |

Nothing is missing and nothing is stubbed. The path terminates in `chuteFire()`.

**2 · The two units agree on every radio parameter and both tokens.**

Diffed `MRC_FlightUnit_GEN3/Config.h` against `MRC_GroundStation_GEN3/Config.h`:

| | Flight | Ground |
|---|---|---|
| `FREQ_MHZ` | 919.0 | 919.0 |
| `BANDWIDTH_KHZ` | 125.0 | 125.0 |
| `SPREADING` | 7 | 7 |
| `CODING_RATE` | 5 | 5 |
| `SYNC_WORD` | 0xAB | 0xAB |
| `TX_POWER_DBM` | 17 | 17 |
| `EJECT_TOKEN` | `"EJECT"` | `"EJECT"` |
| `PING_TOKEN` | `"PING"` | `"PING"` |

Identical throughout. There is no mismatch for a packet to fall through, which rules out the
class of fault that entry 033 found on the *serial* side.

**3 · A second hardware run reproduces the failure exactly.**

`logs/raw/20260819-212959-serial.log`, 405 lines, 386 packets, three boots (sequence restarts
at 44→1 and 225→1).

- `[GCS] EJECT armed` → 15 attempts, one interleaved after each `$MRC` packet → `gave up after 15`
- **No attempt carries ` FAILED TO TRANSMIT`**, so `radio.transmit()` returned
  `RADIOLIB_ERR_NONE` every time — the ground station genuinely keyed up all 15 times
- **`chute = 0` in all 386 packets.** No exceptions
- **Zero PING attempts** in the entire session
- One `[GCS] RX error code -7` (CRC mismatch) at line 45; nothing else anomalous

**4 · The wrong assumption is stated twice, and both copies are in the ground station.**

Entry 039 quoted `Uplink.ino:84`. There is a second, in `MRC_GroundStation_GEN3/Radio.ino:126-127`:

> *"The vehicle transmits, then immediately opens its listen window. This is that moment —
> the single best time in the whole cycle to send a command."*

It is the worst. `MRC_FlightUnit_GEN3.ino` **listens at line 115 and transmits at line 133** —
listen first, transmit last. After transmitting the vehicle still runs the SD write, the OLED
and the hold; its next window opens at the start of the *next* cycle, roughly 354 ms later.

## Why this matters

It moves the fault off the vehicle by evidence rather than by assumption.

Three explanations for "not received or processed" are now closed by inspection: no missing
handler, no token mismatch, no radio-parameter disagreement. Combined with `radio.transmit()`
reporting success on all 15 attempts, the command demonstrably leaves the ground station's
antenna and the vehicle demonstrably has code waiting for it. What has never been shown is
that the two happen at the same time.

That leaves the timing diagnosis of entry 039 as the sole surviving explanation. It is now
better supported — but it is still not *confirmed*, and the distinction is the whole point of
this entry. **Code inspection can prove a handler exists; it cannot prove a packet reaches it.**

## Result

Nothing changed. No firmware was edited and the fix proposed in 039 is still unwritten.

`ISS-02` remains as entry 039 left it: the ground unit firmware exists and transmits, and no
uplink has been shown to reach a vehicle. Its text in `wiki/issues.md` still describes the
original GEN2 problem and is now stale in a way worth rewriting — deliberately not done here,
because the confirming measurement below should come first.

**The decisive test from entry 039 has still not been run**, and it is worth recording *why*
two hardware sessions did not answer it:

- Both `20260819-172515-serial.log` and `20260819-212959-serial.log` are the **ground
  station's** USB port. `[FLT] PING received` prints on the **flight unit's** port. Neither
  log could have shown it even if the uplink worked perfectly.
- No PING was pressed in either session.

So the test is unchanged and still outstanding: tether the **flight unit** at 115200, press
Ping, watch for `[FLT] PING received, count N`. Absent → timing, as diagnosed twice over.
Present → the fault is downstream in `chuteFire()` or the servo.

One thing that would make this cheaper to answer next time, noted and not built: the proposed
`ul` field from entry 037 would put uplink health in telemetry, where the ground station's own
log would record it. Every session so far has been unable to see the vehicle's side of the
link without a second cable.
