# 042 · The GPS fault was swapped TX/RX after all

**Date** 2026-08-19
**Type** fix
**Refs** ISS-14

**Supersedes entry 026's power diagnosis, and restores entry 025's layer while rejecting its
specific cause.**

## What

Aiman debugged the GPS and found the TX and RX pins swapped. Corrected in five places:

| File | Change |
|---|---|
| `MRC_FlightUnit_GEN3/Config.h:126` | `GPS_RX` 20 → **19**, `GPS_TX` 19 → **20** |
| `tools/GPS_Minimal/GPS_Minimal.ino:46` | same swap |
| `tools/GPS_Passthrough/GPS_Passthrough.ino:37` | same swap, plus the wiring diagram in its header |
| `tools/GPS_Relay_Flight/GPS_Relay_Flight.ino:65` | same swap |
| `tools/README.md:53` | the crossover note, which stated the reversed numbers as fact |

`GPS_RX` still means *the ESP32's RX pin*, connected to the module's **TX**. The convention
was never the problem; the two numbers under it were.

## Why the earlier diagnoses missed it

Entry 025 said the pins were the layer and named the wrong cause — GPIO19/20 claimed by the
ESP32-S3's USB peripheral. Entry 026 then rejected the whole layer on a good argument: an
unpowered module produces `chars=0` no matter how it is wired, and Aiman had just reported
the module no longer lighting up. That argument is still valid; it simply was not what was
happening.

The detail that made this durable is worth recording. **`tools/README.md` and the
`GPS_Passthrough` header both explained the crossover correctly and then stated the reversed
numbers as fact** — "Wiring TX to TX is the classic failure and produces `chars=0`" sat
directly above a pin table that did exactly that. Anyone checking the wiring against the
documentation would have confirmed the fault as correct. The documentation did not just fail
to catch the bug, it actively defended it.

That is also why `GPS_Passthrough` and `GPS_Relay_Flight` reported `chars=0` and were read as
corroboration: **all three tools carried the same swapped pair**, so they were not independent
witnesses. Three tools agreeing meant one mistake copied three times.

## Result

Not yet confirmed on hardware from this repo — no Arduino toolchain here, so Aiman builds and
flashes. The check is unchanged and now cheap: run `GPS_Minimal`, expect characters instead of
`chars=0`.

`ISS-14` still reads as a power fault and should be rewritten once a flashed build confirms
this. Deliberately not rewritten here: two entries have now changed the diagnosis of this
issue without a measurement in between, and a third would be the same mistake again.

If characters flow, the standing note in `status.md` — *"`ISS-14` GPS unpowered. Starts with a
multimeter on the module's VCC"* — is obsolete, and the multimeter step in entry 026 is no
longer the decisive one.

Worth carrying forward: the two power hypotheses in entry 026 (switched `Ve` rail, and
brownout under full LoRa + SD load) were never tested and were not disproved by this. They
remain plausible causes of *other* faults, and the observation behind them — the module
lighting up before and not after — is still unexplained. The `Pins_Assignment.md` detail that
the SD card is specified on external 5 V is a real constraint someone hit once, and it has not
gone away.
