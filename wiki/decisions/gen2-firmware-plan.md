# GEN2 Firmware Plan — superseded

**Superseded 2026-08-18 by `gen3-firmware-plan.md` and `gen3-packet-format.md`.**

This document proposed merging GEN1's sensors into GEN2's two-way structure while keeping
GEN2's packet unchanged. It was replaced the same day, after review, by a GEN3 plan that
also closes `ISS-07`, `ISS-08` and `ISS-10` in the packet itself.

What changed, and why:

| Superseded here | Replaced by | Reason |
|---|---|---|
| 600 ms listen window | 400 ms | the timed uplink removes the need for a long window |
| Blind 5 × 300 ms `EJECT` burst | retry timed to follow each received packet | ~100% hit rate instead of 40–60%, and no telemetry lost |
| Confirmation stops on `chute = 1` | stops on `ack > 0` | separates "uplink worked" from "chute fired" |
| GEN2 packet unchanged | GEN3 packet | adds `seq`, `ms`, CRC16, `ack`; makes `chute` a bare integer |
| Faster SD writes considered | open/close per write retained | data safety chosen over speed |
| Three confirmation levels | four | `ack` splits the most useful pair apart |

Retained from this plan and carried into GEN3: the HSPI ownership decision, the four
GEN1 faults to fix rather than carry forward, `listenForEject()` kept verbatim, ground
station built first, and the cycle-budget method.

See `gen3-firmware-plan.md`.
