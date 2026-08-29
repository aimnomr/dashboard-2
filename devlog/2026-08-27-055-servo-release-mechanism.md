# 055 · The release mechanism is a servo, and both generations now say so

**Date** 2026-08-27
**Type** change
**Refs** —

`Chute.ino` has said "the mechanism is not yet chosen" since GEN3. It is chosen.

## What

**Both flight generations switched to the servo path.** `CHUTE_USE_SERVO` 0 → 1 in
`MRC_FlightUnit_GEN3/Config.h` and `MRC_FlightUnit_GEN4/Config.h`, taking
`Chute.ino` off `digitalWrite(CHUTE_PIN, HIGH)` and onto `chuteServo.write()`.
GEN3 was edited by hand; GEN4 mirrors it exactly.

**The angles were wrong and are now the measured ones.**

```
        was                     now
ARMED    0 deg                  90 deg
RELEASE 90 deg                 160 deg
```

The old pair were placeholders written before a mechanism existed. Flipping
`CHUTE_USE_SERVO` without correcting them would have driven the horn to 0 deg at
boot — 90 deg away from where the real mechanism arms — and then "released" to 90,
which on this hardware is the armed position. It would have booted into a strained
state and then failed to deploy, with nothing on screen to say so.

**`firmware/tools/ServoEjectTest/`**, the bench rig committed as a tool. Throws the
servo from a button instead of a radio. Documented in `firmware/tools/README.md`
alongside the GPS diagnostics.

**One stray keystroke removed** from the GEN3 hand-edit: `A BACKUP to thes uplink`
in the auto-eject comment.

## Why

The servo was the working mechanism on the bench and had been for some time; the
firmware was still compiled for a relay it does not have. The gap between "what is
on the bench" and "what is in `Config.h`" is exactly the kind of thing that is
discovered on a launch day.

Committing the bench sketch rather than leaving it in a scratch folder is the same
argument as `UART_PinTest`: the numbers in it have to agree with the numbers in
`Config.h`, and a file that is not in the tree cannot be checked against one that is.

## Result

**`CHUTE_PIN` is `D3` in both files and this is unverified.** `D3` is not defined by
the Heltec V3 variant as far as anything here can establish — every other pin in
`Config.h` is a raw GPIO number, and the ESP32 Arduino core defines `D0`-`D13` only
for board variants providing an Arduino-shield mapping. If it is not defined, both
sketches fail to compile with *'D3' was not declared in this scope*. Nothing in this
repo can settle it: there is no Arduino toolchain here and there never has been.

Resolve it by compiling `Serial.println(D3);` on the actual board. If it prints a
number, both files are already correct. If it does not compile, both files need the
same GPIO number, and 47 — GEN2's inherited value — is still a valid choice on the
ESP32-S3: not a strapping pin, no collision in `Config.h`, and free of the native-USB
trap that GPIO 19/20 carry.

**The servo angles now live in three places** — the bench sketch and two `Config.h`
files — and only prose keeps them together. The same shape as the auto-eject bounds,
and it will drift the same way.

**Flipping `CHUTE_USE_SERVO` activates a blocking `delay(CHUTE_HOLD_MS)`** on the
cycle the release lands, on top of ~650 ms of work. `Chute.ino` documents this as a
knowing violation of the 1 Hz rule: the scheduler reports the overrun and
resynchronises rather than firing a catch-up burst, so the cost is one late packet at
the moment of deployment. Previously theoretical, now live on both generations.

**The GPIO 47 boot-float window is unchanged and now matters more.** `chuteBegin()`
runs at `MRC_FlightUnit_GEN4.ino:118`, after `Serial`, two delays and `displayBegin()`
— several hundred ms during which the release line is an undriven input with no
pulldown. On the relay path an external gate pulldown answers this. On a servo the
question is different, not absent: an unheld signal line during boot is a horn that
may twitch before `write(ARMED_DEG)` ever runs.

**Nothing here was compiled.** Same gate as entries 052 and 053. This entry adds two
things that must be checked on hardware before the next flight: that `D3` resolves,
and that the horn boots to 90 deg rather than jumping.
