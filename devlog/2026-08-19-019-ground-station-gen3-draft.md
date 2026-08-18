# 019 · Ground station GEN3 draft

**Date** 2026-08-19
**Type** change
**Refs** ISS-02, ISS-07, ISS-08, ISS-10, ISS-13

## What

First GEN3 firmware. Ground station only — the flight unit follows.

```
firmware/MRC_GroundStation_GEN3/
├── MRC_GroundStation_GEN3.ino   setup, loop, shared state
├── Config.h                     every tunable, nothing tunable elsewhere
├── Radio.ino                    receive, classify, CRC, forward, uplink TX
├── Uplink.ino                   command intake, retry state machine
└── Display.ino                  OLED
firmware/tests/verify_gen3.py    CRC + parser reference, 11 cases
```

Preceded by confirmation of the last two open decisions: all five precision trims
accepted, and PascalCase-by-role file naming.

Placed under a new top-level `firmware/` directory. Easy to move if it belongs elsewhere.

## Why

Ground station first because it is the missing piece (`ISS-02`), it is the smaller file,
and it can be tested against the **existing GEN1 flight unit** — telemetry keeps flowing
while the uplink is developed, so nothing waits on both units being rewritten at once.

## Result

**The blocking receive is gone.** GEN1's `radio.receive()` halts the CPU until a packet or
timeout, so a command sitting in the serial buffer waited up to a second before anything
looked at it. `radioPoll()` and `uplinkPoll()` now share a 5 ms tick and neither blocks.

**Timed retry implemented.** `uplinkOnPacketReceived()` is called from the receive path
immediately after a packet is forwarded, because that is exactly when the vehicle opens
its listen window. Confirmation is checked *before* transmitting, so a packet that already
carries `chute ≥ 1` does not trigger another pointless transmission on a shared band.

**CRC is recomputed on the ground station for one purpose only.** Forwarding to the PC is
verbatim regardless — the dashboard must be able to see corruption. But the retry loop
reads `chute` from the packet, and a corrupted packet with a garbage `chute` would stop
retries against a vehicle that never heard the command. `lastChute` is therefore only
updated when the checksum verifies; a failed check leaves the previous value alone rather
than guessing.

**Two independent layers reject foreign traffic:** the `$MRC` marker check runs before
CRC ever executes, and the CRC catches corruption in what remains. Foreign packets are
counted and reported as `[GCS] foreign packet, N so far` (`ISS-13`).

**Verified.** `firmware/tests/verify_gen3.py` transliterates the C and passes 11 cases —
valid packets, a single flipped bit, truncation at three different points, a missing
star, a wrong team marker, an empty and an overlong chute field, and garbage. The two
worked-example checksums are pinned as golden values so a wire-format change cannot pass
silently.

Corrected while doing this: the example packets previously written into
`gen3-packet-format.md` carried **invented checksums**. Replaced with real ones (`DA98`
and `AEAF`) and the CRC variant is now specified precisely — CRC16/CCITT-FALSE, poly
`0x1021`, init `0xFFFF`, no reflection, no final XOR, over the bytes between `$` and `*`.
Three implementations have to agree on that and none of them existed when the examples
were written.

Also replaced Arduino `String` with plain C in the parsing helpers. A long flight is
exactly where heap fragmentation would surface.

Worst case measured against the 256-byte buffers: 133 bytes on the vehicle, 146 at the
PC — **109 bytes of headroom**.

## Not done

Not compiled. There is no Arduino toolchain in this environment, so the C is reviewed and
its logic cross-checked in Python, but it has never been through a compiler. Expect the
first build to surface something small.
