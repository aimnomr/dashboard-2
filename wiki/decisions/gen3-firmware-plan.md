# GEN3 Firmware Plan — Flight Unit and Ground Station

Proposed 2026-08-18. **Not yet agreed — supersedes `gen2-firmware-plan.md`.**

GEN3 = GEN1's proven sensor code + GEN2's two-way capability + the fixes both need.
Packet format is specified separately in `gen3-packet-format.md`.

---

## Agreed constraints

| | |
|---|---|
| Cadence | **≥ 1 Hz. Faster acceptable, slower is not.** Period ≤ 1000 ms |
| SD writes | open → append → close per write. **Data safety over speed** |
| SPI | GEN1's arrangement: LoRa on default SPI, SD on HSPI |
| Config | Everything tunable lives in one block at the top of each file |

## 1. Cycle budget

Housekeeping moves **inside** the listen window. The SX1262 latches a received packet and
holds `DIO1` high until read, so a blocking SD write during the window delays detection
but cannot lose the packet. This makes SD and OLED time effectively free.

```
listen 400ms  +  tx 231ms  +  sensors 15ms  =  646ms      margin 354ms
listen 600ms  +  tx 231ms  +  sensors 15ms  =  846ms      margin 154ms
```

`tx` is the GEN3 **worst-case** packet at SF7/BW125. **Recommend a 400 ms window** — the
timed uplink below removes any need for a long one, and 354 ms of margin protects the
1 Hz guarantee against SD cards that stall.

**Use a deadline scheduler, not a free-running loop.** Each cycle targets
`start + n × PERIOD`, so jitter in one cycle does not accumulate. GEN1's `delay(200)`
goes; it exists only to pace the old display loop.

## 2. Uplink: timed retry, not blind burst

**The ground station knows when the vehicle is listening**, because the vehicle transmits
immediately before opening its window. A received packet is the timing signal.

```
on telemetry received:
    forward to PC
    if eject_pending and not acknowledged:
        transmit "EJECT"          <- lands in the window that just opened
        report attempt to PC
```

| Strategy | Hit rate | Ground unit deaf | Telemetry lost |
|---|---|---|---|
| Blind single shot (400 ms window) | 40% | 41 ms | none |
| Blind 5 × 300 ms burst | 40% each | **1406 ms** | 1–2 packets |
| **Timed, fire after RX** | **~100%** | 41 ms | **none** |

**Stop condition:** the vehicle's `chute` counter goes above zero. `chute` and `ack` were
merged into one field — the chute is servo-driven with no feedback sensor, so "command
acknowledged" and "servo driven" are the same fact. See `gen3-packet-format.md`.

**Give up after `EJECT_MAX_ATTEMPTS`** (suggest 15 ≈ 15 s) and report it. Retrying
forever would occupy the channel indefinitely if the vehicle is gone.

**No cancel command.** Considered and rejected. The consequence, accepted knowingly: once
fired, the retry loop runs to `EJECT_MAX_ATTEMPTS` and cannot be stopped short of
power-cycling the ground unit. The dashboard's arm-then-fire guard makes an accidental
trigger unlikely, and a deploy command is rarely one you want to take back — but it is a
trade, not a free choice.

## 3. Command wire format, PC → ground station

```
CMD:EJECT\n     arm the retry loop
```

Exact match after trimming. The `CMD:` prefix costs nothing, states intent, and leaves
room for later commands without ambiguity. Requires a one-line change in the dashboard,
which currently sends bare `EJECT`.

## 4. Flight unit — start from GEN1

**Port in from GEN2, unchanged:**

- `CHUTE_PIN 47`, `chute_deployed` flag.
- `listenForEject()` — `startReceive()` → poll `DIO1` in a `millis()` loop → `readData()`
  → `standby()`. **Keep its shape.** The header records why: `radio.receive()` timeout
  behaviour varies between RadioLib versions and blocked forever. That is a fix.
- Listen-then-transmit ordering.

**Keep from GEN1, unchanged:** sensor init and reads, raw MPU6050 register access with
its matched scale factors, SD logging, boot calibration.

**Fix rather than carry forward:**

| Fault | Fix |
|---|---|
| GPS speed offset is captured inside `showGPSScreen()`, so it only runs once that OLED screen rotates into view | move into the GPS update path |
| Sensors read twice per cycle — once in `transmitData()`, again in each display function | read once, share the values |
| SD header row lists 14 columns | widen to the GEN3 field list |
| `delay(200)` paces the loop | deadline scheduler |
| OLED updated every iteration at the 100 kHz I²C default (~90 ms per full buffer) | update every Nth cycle, call `u8g2.setBusClock(400000)` |

**Add:** `seq` counter, `millis()` timestamp, `ack` counter, CRC16 over the payload.

**Resolve SPI first.** GEN1: `new Module(NSS, DIO1, RST, BUSY)` with no bus argument
(LoRa on default SPI) plus `SPIClass sdSPI(HSPI)`. GEN2: `SPIClass loraSPI(HSPI)` passed
to `Module`. Merged as written, both drivers claim HSPI. **Adopt GEN1's** — it is the only
build that has run LoRa and SD together on real hardware.

## 5. Ground station — GEN1 base, new capability

`14.GROUND_919MHz.ino` contains no `Serial.available()` and no `fireEject()`. It cannot
receive an uplink command under any circumstances (`ISS-02`).

**The blocking receive must go.** `radio.receive()` blocks until packet or timeout, so
the unit cannot poll serial while waiting. Restructure to the same non-blocking pattern
the flight unit uses.

```
setup:  radio.begin(...); startReceive()

loop (~5 ms tick, never blocking):
  DIO1 high?         -> readData(); append ",rssi,snr"; Serial.println()
                        if eject_pending && !acked: transmit "EJECT"; report
                        startReceive()
  serial line ready? -> CMD:EJECT  -> eject_pending = true
                        CMD:CANCEL -> eject_pending = false
```

**Also:**

- Parse `chute` out of the received packet to decide when to stop retrying.
- Raise `char output[160]` to **256**.
- **Count and report foreign packets.** A received packet whose start marker is not
  `$TEAM_ID` belongs to another team. Do not forward it as telemetry; count it and emit
  `[GCS] foreign packet, N so far`. This single line separates "the band is busy" from
  "my vehicle is silent" — otherwise indistinguishable, and they look identical at
  exactly the wrong moment. See `ISS-13`.
- Keep forwarding malformed packets verbatim — the dashboard shows corruption; the ground
  unit must not hide it.
- Keep `[GCS]`-prefixed status lines. The dashboard already treats `[`-prefixed lines as
  status and displays them.

## 6. Status lines → three-level confirmation

| Level | Evidence | Meaning |
|---|---|---|
| 1 · Sent | dashboard wrote to the serial port | the PC did its part |
| 2 · **Transmitted** | `[GCS] EJECT attempt n` | ground unit heard the PC, keyed the radio |
| 3 · **Commanded** | `chute ≥ 1` in telemetry | **the vehicle heard it and drove the servo** |

Level 2 is the new one, and it splits "the dashboard is broken" from "the vehicle never
heard us" — currently indistinguishable.

⚠️ **There is no level 4.** The servo has no feedback sensor, so nothing on this vehicle
can confirm the parachute actually opened. Level 3 is the end of the evidence chain, and
the UI must not imply otherwise.

Suggested lines: `[GCS] EJECT armed`, `[GCS] EJECT attempt 3`, `[GCS] EJECT confirmed`,
`[GCS] EJECT gave up after 15`.

## 7. Configuration blocks

Everything tunable at the top of each file, nothing buried.

**Flight unit**

```c
// ---- RADIO ----
// Frequency, sync word and TEAM_ID may all have to change at the launch site if
// channels are assigned or negotiated on the day. See ISS-13. Keep them here.
#define FREQ_MHZ        919.0
#define BANDWIDTH_KHZ   125.0
#define SPREADING       7      // keep low: shortest airtime = smallest collision target
#define CODING_RATE     5
#define SYNC_WORD       0xAB
#define TX_POWER_DBM    17
#define TEAM_ID         "MRC"  // packet start marker, "$MRC"
#define PACKET_BUF      256

// ---- CADENCE ----  period must stay <= 1000 ms
#define CYCLE_PERIOD_MS 1000
#define LISTEN_WINDOW_MS 400
#define LISTEN_TICK_MS    5

// ---- FEATURES ----
#define ENABLE_SD        1
#define ENABLE_OLED      1
#define OLED_EVERY_N     3
#define ENABLE_UPLINK    1

// ---- CALIBRATION ----
#define MPU_CAL_SAMPLES  500
#define SEA_LEVEL_HPA    1013.25
#define ACCEL_RANGE_REG  0x10   // +/-8 g
#define GYRO_RANGE_REG   0x08   // +/-500 dps

// ---- PINS ----  (unchanged from GEN1)
```

**Ground station**

```c
// ---- RADIO ----  must match the flight unit exactly
// ---- SERIAL ----
#define SERIAL_BAUD          115200
#define OUTPUT_BUF           256
// ---- UPLINK ----
#define EJECT_TOKEN          "EJECT"
#define CMD_EJECT            "CMD:EJECT"
#define CMD_CANCEL           "CMD:CANCEL"
#define EJECT_MAX_ATTEMPTS   15
// ---- FEATURES ----
#define ENABLE_OLED          1
```

## 7b. File layout — split, not one long sketch

Arduino concatenates every `.ino` in a sketch folder, main file first, then the rest
alphabetically. Globals are therefore shared across tabs — which works, but makes
declaration order matter. **Put every shared declaration and all tunables in `Config.h`**
so ordering is explicit rather than accidental.

```
MRC_FlightUnit_GEN3/
├── MRC_FlightUnit_GEN3.ino   setup(), loop(), the cycle scheduler — and nothing else
├── Config.h                  all tunables + shared declarations
├── Radio.ino                 LoRa init, transmit, listen window
├── Sensors.ino               BME280, MPU6050, GPS, calibration
├── Packet.ino                field formatting + CRC16
├── Storage.ino               SD card
└── Display.ino               OLED

MRC_GroundStation_GEN3/
├── MRC_GroundStation_GEN3.ino  setup(), loop()
├── Config.h
├── Radio.ino                   receive, forward, transmit uplink
├── Uplink.ino                  command parsing, retry state machine
└── Display.ino                 OLED
```

The main `.ino` should read as the flight sequence and nothing more — anyone opening it
sees the cycle, not 400 lines of sensor registers. Naming convention to be settled before
the draft is written.

## 8. Order of work

1. **Ground station first.** It is the missing piece, it is smaller, and it can be tested
   against the **existing GEN1 flight unit** — telemetry keeps flowing while the uplink is
   built, so nothing waits on both units being rewritten at once.
2. Flight unit: SPI, then the GEN2 ports, then the fixes, then the GEN3 packet fields.
3. **Measure the real cycle on hardware** before trusting the budget above. SD stall time
   is the least predictable term.
4. Bench test both units with the dashboard; confirm confirmation reaches level 3.
5. Dashboard: add the GEN3 parser branch, then real packet-loss display from `seq`.

## Open question for the firmware member

**Should `ack` reset?** Suggested: never — it is a monotonic count from boot, so a late
telemetry packet cannot make an acknowledged command look unacknowledged. The ground
station stops on `ack > 0`, and a reset would restart the retry loop.
