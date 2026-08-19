# 047 · The receiver's own fix verdict is now read

**Date** 2026-08-20
**Type** change
**Refs** ISS-14

Completes the item entry 046 surfaced and left open.

## What

`gpsFixQuality()` in `Sensors.ino` reads **GGA field 6** — the receiver's own statement
about its fix: `0` invalid, `1` GPS, `2` DGPS. TinyGPSPlus has no accessor for it, so it is
read through `TinyGPSCustom`, declared next to `gps` in the main sketch because
`TinyGPSCustom`'s constructor registers itself with the `TinyGPSPlus` instance and therefore
depends on construction order. The Arduino build concatenates the main sketch first, so
keeping them adjacent makes that dependency visible rather than load-bearing and invisible.

Both talkers are registered — `$GPGGA` and `$GNGGA`. Which one arrives is a property of the
hardware: a GPS-only NEO-6M sends the former, a multi-constellation module the latter.

The fix decision is now three conditions:

```c
int  fixQuality = gpsFixQuality();
bool fixFresh   = gps.location.isValid() &&
                  gps.location.age() < GPS_FIX_MAX_AGE_MS &&
                  fixQuality != 0;
```

## Why the third condition is not redundant with the second

Entry 046's age check catches a module that **stopped talking**. It does not catch one that
**keeps talking and says the fix is gone** — which is precisely what a receiver does when it
loses lock indoors: GGA keeps arriving at 1 Hz, on time, with the quality flag cleared and
the position field holding whatever it last had. Age would call that fresh. Only the flag
says otherwise.

That is the difference between detecting the fault after `GPS_FIX_MAX_AGE_MS` and not
producing it at all.

## The -1 case, which is the part worth being careful about

`gpsFixQuality()` returns **-1 when the field was never reported**, distinct from `0`
meaning the receiver actively rejects the fix, and only `0` vetoes.

A module that does not emit GGA — a different receiver, a different sentence configuration —
would otherwise silently blank the ground track forever, with the firmware confidently
reporting no fix while the module had one. A validity check that fails closed on missing
information is a worse failure than the one it prevents, because it looks identical to a
genuine loss of signal.

The quality term is age-checked for the same reason everything else here now is: `isValid()`
latches, so a module that went quiet would otherwise keep vouching for a fix it can no longer
see.

## Result

`MRC_FlightUnit_GEN3.ino` and `Sensors.ino`. No packet change, no airtime cost — this is a
decision the vehicle makes locally before choosing what to put in the packet, which is what
made it worth doing ahead of any format bump.

Not compiled or flashed; no toolchain here.

The hierarchy from entry 046 now reads:

| signal | status |
|---|---|
| GGA fix quality | authoritative — **read** ✓ |
| HDOP | actual accuracy — still not read |
| satellite count | proxy — read and sent |
| `lat`/`lng` ≠ 0,0 | inference — no longer the dashboard's only guard |

**HDOP is the one left.** It is `TinyGPSCustom(gps, "GPGGA", 8)` — the same mechanism, one
more line — and unlike fix quality it would need a packet field to be worth reading, since
its value is a number the ground wants rather than a decision the vehicle can make alone.
That belongs with the additional-fields decision, not ahead of it.

**Worth confirming on hardware:** that `$GPGGA` is what this NEO-6M actually emits. If it
sends neither talker, `gpsFixQuality()` returns -1 forever and this entry changes nothing —
which is the safe outcome by design, but also an invisible one. A single `[FLT]` print of
the quality value during bring-up would settle it.
