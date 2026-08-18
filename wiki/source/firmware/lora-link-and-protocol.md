# LoRa Link and Two-Way Protocol

Source: `13.CANSAT_919MHZ.ino`, `14.GROUND_919MHz.ino`, `MRC_FlightUnit_V7.ino`,
`MRC_TwoWay_Flowchart.html`, `MRC_DualDebug_Flow.json`

## Radio parameters

Identical in all three firmware files — they must match exactly or the link fails.

| Parameter | Value |
|---|---|
| Frequency | **919.0 MHz** (noted in firmware as the Malaysian ISM band) |
| Bandwidth | 125 kHz |
| Spreading factor | SF7 |
| Coding rate | 5 (4/5) |
| Sync word | `0xAB` |
| TX power | 17 dBm |
| Chip | SX1262 |
| Library | RadioLib |

Call: `radio.begin(919.0, 125.0, 7, 5, 0xAB, 17)`

Telemetry rate: **1 Hz** (`TRANSMIT_INTERVAL 1000`).

## Downlink (telemetry) — implemented in both firmware generations

Flight unit transmits → ground unit receives → appends RSSI/SNR → USB serial to PC.

## Uplink (EJECT command) — simulator generation only

The two-way path exists **only** in `MRC_FlightUnit_V7.ino` and the Node-RED dual-debug flow.
`13.CANSAT_919MHZ.ino`, the build with real sensors, is transmit-only and has no chute pin.

Sequence, per `MRC_TwoWay_Flowchart.html`:

1. Operator presses **EJECT** in the Node-RED dashboard → message with `topic='eject'`.
2. A filter node passes only `topic=eject` → `serial out` to the ground unit COM port,
   sending the literal string `EJECT\n`.
3. Ground unit sees `EJECT` on serial → `fireEject()` → transmits `"EJECT"` over LoRa
   **5 times with 300 ms between retries**, then resumes `startReceive()`.
4. Flight unit, in its RX window, matches the string `EJECT` → sets `chute_deployed = true`
   → drives **GPIO47 HIGH** (servo/relay).
5. Subsequent telemetry carries `CHUTE:1`; the dashboard banner turns red and pulses.

⚠️ The flowchart describes ground-unit `fireEject()` behaviour and a 10 ms non-blocking serial
poll, but the ground unit firmware in the source set (`14.GROUND_919MHz.ino`) contains
**neither** — it uses a blocking `radio.receive()` and never reads from serial. The ground unit
firmware matching the flowchart is **not present** in the source set.

## Flight unit RX/TX cycle (simulator)

`MRC_FlightUnit_V7.ino` documents a deliberate design change from V6:

> Dropped `radio.receive()` entirely — timeout param behavior varies between RadioLib versions
> and was blocking forever. New RX approach: `startReceive()` → poll `LORA_BUSY` + `DIO1` in a
> `millis()` timed loop → `readData()` if packet found → `standby()` to cancel if timeout expires.

Cycle: **800 ms listen window** (polled every 5 ms on DIO1) → `standby()` → transmit telemetry
→ repeat. Total ≈ 1 s.

Once the chute is deployed the unit skips listening but still `delay(800)` to hold cycle timing
constant.

## Half-duplex consequence

One radio, one frequency, one sync word. The flight unit cannot listen while transmitting, so
an `EJECT` sent during the ~200 ms TX slot is lost — hence the 5× retry at 300 ms spacing on
the ground side. Command delivery is best-effort with **no acknowledgement**: nothing tells the
operator the command was received. Confirmation is indirect, via `CHUTE:1` appearing in later
telemetry.

## Simulated flight profile

`MRC_FlightUnit_V7.ino` runs a fixed 78-second, 8-phase sequence, then halts:

| Phase | Duration | Altitude behaviour |
|---|---|---|
| `PRE_LAUNCH` | 8 s | 0 m |
| `BOOST` | 5 s | 0 → 80 m linear |
| `COAST` | 8 s | 80 → 150 m, easing |
| `APOGEE` | 3 s | 150 m |
| `DESCENT_FREE` | 12 s | 150 → 80 m |
| `DESCENT_CHUTE` | 30 s | 80 → 2 m |
| `LANDING` | 4 s | 2 → 0 m |
| `POST_LAND` | 8 s | 0 m |

Each phase also shapes accelerometer, gyro and speed values — e.g. `BOOST` sets `az≈6.5 g`,
`DESCENT_CHUTE` sets `az≈0.85 g`, `LANDING` applies a 4× impact spike on the first second.
Apogee is 150 m.
