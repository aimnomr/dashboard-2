# 025 · GPS delivers zero bytes — root cause narrowed

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-14

## What

Aiman ran the relay pair from entry 023. It returned:

```
$GPSD,1,chars=0,ok=0,bad=0,inview=0,used=0,fix=0,hdop=-
$GPSD,1,no GGA sentence seen
```

Raised `ISS-14` and added `firmware/tools/UART_PinTest` to confirm the cause.

## Result

**`chars=0` is decisive.** Not corrupted data, not garbage — zero bytes ever arriving. A
wrong baud rate still yields garbage, because the UART samples edges and emits nonsense.
Zero means nothing reaches the pin, which eliminates sky view, antenna, cold start and
baud rate in a single reading. Every hypothesis raised over the previous two sessions is
now excluded.

**Leading cause: the pins are the USB port.** On the ESP32-S3, GPIO19 and GPIO20 are the
native USB D− and D+ lines, and the firmware uses exactly those for the GPS. With *USB CDC
On Boot* enabled in the Arduino board menu the USB peripheral claims both, and they stop
working as a UART silently — presenting as a dead module.

**Supporting evidence.** The GEN1 bench packet from 2026-08-18 also carried
`lat=0, lng=0, sat=0`. Three sessions across two firmware generations with no GPS data
fits "never worked on this wiring" far better than "recently broke".

Worth recording plainly: **the pin assignment came from `Pins_Assignment.md` and GEN1, and
was carried into GEN3 unchanged because GEN1 was treated as proven.** It was not proven —
GEN1's GPS had never produced a fix either. Inheriting a configuration on the assumption
that working firmware implies working hardware is what let this survive into a second
generation.

`UART_PinTest` tests the pin pair with a loopback jumper and no GPS attached, so the
question "do these pins work as a UART" is answered separately from "does the module
work". It also accepts alternative pin numbers, so a candidate pair can be validated
before anything is rewired.

**The relay pair itself works well** — RSSI −24 dBm, SNR 12.5 at bench range, and both the
digest and the rotating raw-sentence slot behaved as designed. The diagnostic did its job:
it converted an open question into a single decisive number.

## Not yet confirmed

The USB-pin explanation is the leading hypothesis, not a proven fact. `UART_PinTest` is
what confirms or refutes it.
