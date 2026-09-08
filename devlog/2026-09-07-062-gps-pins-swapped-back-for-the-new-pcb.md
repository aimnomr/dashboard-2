# 062 · GPS RX/TX back to 20/19 for the new PCB

**Date** 2026-09-07
**Type** change
**Refs** 025, 026, 042, `ISS-14`

The new board routes the GPS pair the other way round, so the firmware follows it.

**This is not a revert of 042.** Entry 042 was correct for the board it was written for, and
stays correct for that board. The numbers below are back at their pre-042 values because the
hardware moved, not because 042 was wrong. Read in order the two entries agree; read out of
order they look like a loop.

## What

**`GPS_RX` 19 → 20, `GPS_TX` 20 → 19**, in every place that names them:

```
firmware/MRC_FlightUnit_GEN3/Config.h:184          GPS_RX / GPS_TX
firmware/MRC_FlightUnit_GEN4/Config.h:242          GPS_RX / GPS_TX
firmware/tools/GPS_Minimal/GPS_Minimal.ino:48      GPS_RX_PIN / GPS_TX_PIN
firmware/tools/GPS_Passthrough/GPS_Passthrough.ino:41   + header diagram, + line 30
firmware/tools/GPS_Relay_Flight/GPS_Relay_Flight.ino:67
firmware/tools/README.md:75                        wiring block + the prose under it
firmware/tools/UART_PinTest/UART_PinTest.ino:45    already 20/19 — annotated, not changed
```

**GEN3 was changed as well as GEN4.** There is one flight-unit board and both sketches
target it, so both follow the trace. If a GEN3 unit is ever kept on the old PCB, GEN3's
`Config.h` is the file to pin back to 19/20 — nothing else in the tree is generation-specific
about this pair.

**The wiring text now leads with the crossover rather than the numbers.** `GPS_RX` means the
ESP32's RX pin and connects to the module's **TX**; that has been true through both changes
and is the only part worth memorising.

## Why

The trace on the new PCB is reversed relative to the old one. Nothing about the code's
intent changed — the same signal is wanted on the same peripheral, at a different physical
pin.

**The history is worth keeping straight, because this pair has now been argued about in four
entries.** 025 blamed the ESP32-S3 USB peripheral claiming GPIO19/20. 026 overruled it and
blamed power. 042 overruled 026, found the pins genuinely swapped, and fixed them. 062
changes them again for an unrelated reason. Anyone reading 042 alone and comparing it to the
current tree will conclude the fix was lost — hence the pointer in both `Config.h` files.

## Result

**The propagation pass found three sites 042 missed, wrong for nineteen days.**

```
GPS_Passthrough.ino:82   the runtime banner printed "Module TX -> pin 20, module RX ->
                         pin 19" while the #define three lines up said the opposite
tools/README.md:75-76    the wiring block said 20/19 while its own next paragraph
                         explained GPS_RX as 19
UART_PinTest.ino:45      "the pair currently used for the GPS" — still 20/19
```

042 counted "five places" and corrected five, but the pair appears in eight. All three
stragglers described the pre-042 wiring, which means **they are correct again today by
coincidence** — they were never updated, and the hardware came back to meet them. That is
luck, not correctness, and it is exactly the kind of luck that hides a real mismatch next
time. All three are now checked and annotated.

**Nothing is compiled or flashed.** `arduino-cli` is not installed on this machine. A wrong
GPS pin does not fail loudly — it produces `chars=0`, which is the same symptom as a dead
module, an unpowered module, a missing ground and a claimed peripheral. `GPS_Passthrough` or
`UART_PinTest` on the bench is the cheap way to confirm the new numbers before assuming
them.

**`ISS-14` should be reread before the next GPS session.** Its head was rewritten around the
power diagnosis in 026 and again around the pin fault in 042; it does not know the pins have
moved a second time.

**The flight unit still needs a reflash for the sync word (060) regardless**, so this rides
along with it rather than adding a trip.
