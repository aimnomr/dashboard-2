# Firmware Generations

Three `.ino` files across **two generations**. The generations differ in **communication
direction**, not in hardware — both share substantially the same component list.

| | GEN1 | GEN2 |
|---|---|---|
| Comms | **One-way** — downlink only | **Two-way** — downlink + uplink |
| Purpose | baseline telemetry | adds redundancy: ground-initiated eject |
| Flight unit | `13.CANSAT_919MHZ.ino` | `MRC_FlightUnit_V7.ino` |
| Ground unit | `14.GROUND_919MHz.ino` | **missing from source set** — see `ISS-02` |
| Packet | 14 fields (16 at PC) | 15 fields (17 at PC) — **canonical** |
| Chute control | none | `EJECT` uplink → GPIO47 |

**GEN2's packet is the format the dashboard targets.** It is derived directly from GEN1 with
the chute flag appended and precision raised. See `packet-format.md`.

## Why two-way exists

The uplink is a **redundancy path**. It lets the ground station command parachute deployment
rather than relying solely on the flight unit's own logic — a backup for the case where onboard
deployment does not fire.

## Component list

Both generations use the same sensor and peripheral set — see `../hardware/flight-unit.md`.
The board is a Heltec WiFi LoRa 32; the V3/V4 discrepancy across the source files is a naming
error only, not a hardware difference (`ISS-03`, resolved).

## ⚠️ `MRC_FlightUnit_V7.ino` as supplied contains no sensor code

The file is a **bench-test build**: it generates a synthetic 78-second flight in software rather
than reading BME280/MPU6050/GPS. It exists to exercise the two-way path and the dashboard
without flying anything. The sensor reads from `13.CANSAT_919MHZ.ino` still need merging in for
a flight article.

For dashboard purposes this is a feature, not a problem — it is a hardware-free packet source
producing correctly-formatted GEN2 telemetry on demand. Its simulated flight profile is
documented in `lora-link-and-protocol.md`.

## `MRC_FlightUnit_V7.ino` version history (from its own header)

> **FIX from V6:** Dropped `radio.receive()` entirely — timeout param behavior varies between
> RadioLib versions and was blocking forever. New RX approach: `startReceive()` → poll
> `LORA_BUSY` + `DIO1` in a `millis()` timed loop → `readData()` if packet found → `standby()`
> to cancel if timeout expires. This works correctly on ALL RadioLib versions.
> RX window = 800 ms, TX after, ~1 s total cycle.

V1–V6 are not in the source set.

## Firmware behaviours that affect the dashboard

- **Halt-on-error.** BME280, MPU6050 and LoRa init failures enter `while(1) delay(1000)` — the
  unit stops dead and the downlink goes silent. Only SD card failure is non-fatal. A silent link
  therefore has several distinct causes the dashboard cannot tell apart.
- **`[GCS]`-prefixed status lines** are interleaved with telemetry on the serial stream and must
  be filtered, not parsed. Includes `[GCS] Timeout - no packet` on every receive timeout.
- **Blocking receive** on the GEN1 ground unit means timing between lines is irregular.
- **GEN2 halts after 78 s** — the simulator stops transmitting at the end of its phase sequence
  and displays "FLIGHT COMPLETE". Expect the feed to stop, not to loop.
