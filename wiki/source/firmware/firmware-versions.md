# Firmware Versions in the Source Set

Three `.ino` files, from **two different generations** of the project. They are not compatible
with each other.

| File | Role | Board | Sensors | Packet | Two-way |
|---|---|---|---|---|---|
| `13.CANSAT_919MHZ.ino` | Flight unit | Heltec V3 | **Real** — BME280, MPU6050, NEO-6M, SD | 14 fields | ❌ TX only |
| `MRC_FlightUnit_V7.ino` | Flight unit | Heltec V4 | **None — simulated** | 15 fields | ✅ listens for `EJECT` |
| `14.GROUND_919MHz.ino` | Ground unit | Heltec V3 | — | appends RSSI/SNR | ❌ RX only |

## Generation 1 — real sensors, one-way

`13.CANSAT_919MHZ.ino` + `14.GROUND_919MHz.ino` + `serial_to_mqtt_V3.py`.
This chain is internally consistent: 14 fields + 2 appended = the 16 the bridge expects.

## Generation 2 — simulated, two-way

`MRC_FlightUnit_V7.ino` + Node-RED dual-debug flow. Adds the chute/EJECT capability and a 15th
field. **The matching ground unit firmware is missing** from the source set — the flowchart
describes a ground unit that reads serial and calls `fireEject()`, but `14.GROUND_919MHz.ino`
does neither.

## What this means

- The build with **real sensors has no parachute deployment**. The build with **parachute
  deployment has no real sensors**. Neither file is a flight-ready article on its own.
- Merging them is firmware work owned by another team member, not a dashboard task — but the
  dashboard's packet contract depends on which way it lands.

## `MRC_FlightUnit_V7.ino` version history (from its own header)

> **FIX from V6:** Dropped `radio.receive()` entirely — timeout param behavior varies between
> RadioLib versions and was blocking forever. New RX approach: `startReceive()` → poll
> `LORA_BUSY` + `DIO1` in a `millis()` timed loop → `readData()` if packet found → `standby()`
> to cancel if timeout expires. This works correctly on ALL RadioLib versions.
> RX window = 800 ms, TX after, ~1 s total cycle.

V1–V6 are not in the source set.

## Notable firmware behaviours worth knowing downstream

- **Halt-on-error.** BME280, MPU6050 and LoRa init failures all enter `while(1) delay(1000)` —
  the unit stops dead. Only SD card failure is non-fatal.
- **Blocking receive on the ground unit.** `radio.receive()` blocks until a packet or timeout,
  which is why a `[GCS] Timeout - no packet` line appears between telemetry lines.
- **Screen rotation costs time.** The flight unit re-reads BME280 and MPU6050 inside its display
  functions, separately from the transmit path, and ends each loop with `delay(200)`.
