# GEN3 Packet Format — Proposal

Proposed 2026-08-18, revised the same day after review. **Not yet agreed.**

Retains GEN1's sensor field set and GEN2's precision where the sensors justify it, and
closes `ISS-07`, `ISS-08` and `ISS-10`.

---

## Vehicle transmits

```
$MRC,<seq>,<ms>,<temp>,<hum>,<pres>,<alt>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<lat>,<lng>,<spd>,<sat>,<chute>*<CRC16>
```

| # | Field | Fmt | Unit | Notes |
|---|---|---|---|---|
| — | `$MRC` | — | — | start marker. A truncated packet cannot corrupt the next |
| 1 | `seq` | `%lu` | — | **NEW** monotonic packet counter from boot |
| 2 | `ms` | `%lu` | ms | **NEW** `millis()` since boot — true onboard time |
| 3 | `temp` | `%.2f` | °C | |
| 4 | `hum` | `%.1f` | % | |
| 5 | `pres` | `%.2f` | hPa | |
| 6 | `alt` | `%.1f` | m | relative to boot |
| 7–9 | `ax ay az` | `%.3f` | g | |
| 10–12 | `gx gy gz` | `%.2f` | °/s | |
| 13 | `lat` | `%.5f` | deg | ≈1.1 m |
| 14 | `lng` | `%.5f` | deg | |
| 15 | `spd` | `%.1f` | km/h | |
| 16 | `sat` | `%d` | — | |
| 17 | `chute` | `%d` | — | **0 = armed · N ≥ 1 = commanded, N = commands received** |
| — | `*CRC16` | `%04X` | — | **NEW** CCITT over everything between `$` and `*` |

Ground station appends after the checksum:

```
$MRC,…,<chute>*A1B2,<rssi>,<snr>
```

`rssi` `%.1f` dBm, `snr` `%.1f` dB. **19 fields at the PC.**

## Checksum definition

**CRC16/CCITT-FALSE** — polynomial `0x1021`, initial value `0xFFFF`, no input or
output reflection, no final XOR. Computed over **every byte between `$` and `*`,
exclusive of both**. Emitted as four uppercase hex digits.

All three implementations must agree exactly: the vehicle computes it, the ground station
recomputes it to decide whether `chute` can be trusted, and the dashboard verifies it.
`firmware/tests/verify_gen3.py` is the reference and cross-checks the ground station's C
implementation.

## Worked examples

Real packets with real checksums. Armed, then after three commands got through:

```
$MRC,412,412340,31.52,70.4,1010.02,148.3,0.012,-0.008,0.998,0.31,-0.22,0.10,3.07830,101.71220,8.2,9,0*DA98
$MRC,689,689115,29.84,74.1,1013.55,2.1,0.921,0.383,-0.052,-0.40,-0.30,4.10,3.07902,101.71188,1.4,11,3*AEAF
```

As the PC sees them, with link quality appended by the ground station:

```
$MRC,412,412340,…,9,0*DA98,-55.6,9.3
$MRC,689,689115,…,11,3*AEAF,-61.2,7.8
```

106 bytes on the air, 116 at the PC. Worst case is **133 / 146** against 256-byte
buffers — 109 bytes of headroom.

**RSSI and SNR go after the `*CRC`, deliberately.** The checksum must cover exactly what
the vehicle sent, so corruption across the RF hop stays detectable end to end. Appending
before it would force the ground station to recompute the checksum, which would then only
prove the USB hop — not the hop that actually corrupts packets.

## One deployment field, not two

An earlier draft had separate `chute` and `ack` fields. **Merged, on the correct
observation that the chute is driven by a servo with no feedback sensor** — so there is no
way to know the parachute opened. "Command acknowledged" and "servo driven" are the same
fact, and one field carries it.

Counting rather than flagging costs the same one character and still reports uplink
quality: `chute` is how many eject commands reached the vehicle. The ground station stops
retrying at `chute ≥ 1`.

⚠️ **`chute ≥ 1` does not mean the parachute opened.** It means the servo was commanded.
Nothing on this vehicle can confirm deployment. The dashboard must label this
**"Commanded"**, not "Deployed" — the distinction matters at exactly the moment someone
is deciding whether to trust it.

## Load compared with GEN1 and GEN2

Measured, SF7 / BW125 / CR4-5, 8-symbol preamble:

| | bytes | fields | B/field | airtime | channel at 1 Hz |
|---|---|---|---|---|---|
| GEN1 | 75 | 14 | 5.4 | 133 ms | 13.3% |
| GEN2 | 96 | 15 | 6.4 | 164 ms | 16.4% |
| **GEN3** | **107** | **17** | **6.3** | **185 ms** | **18.5%** |
| binary equivalent | 39 | 17 | 2.2 | 82 ms | 8.2% |

GEN3 carries three more fields than GEN2 for +11 bytes and +21 ms, and is *more*
efficient per field. **Better in information carried, at a bandwidth cost that is
negligible at SF7.**

### Where the cost actually bites: range

Spreading factor is the range lever, and airtime roughly doubles per step.

| | SF7 | SF9 | SF10 |
|---|---|---|---|
| GEN3 | 185 ms | 574 ms | **1067 ms** ✗ |
| binary | 82 ms | 267 ms | 494 ms ✓ |

**CSV caps the link at SF9** — reachable by shrinking the listen window to ~300 ms. SF10
breaks the 1 Hz guarantee outright.

**Decision: stay CSV.** There is 354 ms of margin at SF7, and readable packets have
already paid for themselves here — the raw feed panel, visible malformed lines, and a
bench packet that could be read directly by eye. Going binary now spends debuggability to
solve a problem that does not exist.

**But know where the wall is.** If range testing later demands SF10, the packet format is
what changes — not the cadence.

### Why SF7 is right, and why higher is not "better"

| SF | Sensitivity | Free-space range* | CSV airtime | Fits 1 Hz |
|---|---|---|---|---|
| **7** | −124 dBm | 46 km | 185 ms | ✅ |
| 9 | −130 dBm | 92 km | 595 ms | ✅ |
| 10 | −133 dBm | 130 km | 1067 ms | ❌ |
| 12 | −137 dBm | 206 km | 4268 ms | ❌ |

\* 17 dBm TX, 2/2 dBi antennas, 20 dB fade margin.

Each step adds ~3 dB of sensitivity and roughly doubles airtime. **The range is not
needed.** Measured link margin at SF7: **70 dB at 150 m apogee**, 53 dB at 1 km, 39 dB at
5 km. Raising SF spends the 1 Hz budget to buy margin that is already enormous.

In a band shared with other teams, low SF is actively **protective** — see `ISS-13`.
Shortest airtime is the smallest collision target.

### The one case that would justify binary

`ISS-13`: if the launch site forces a shared frequency, airtime becomes collision
exposure. At SF7 with one other team on the same channel, CSV loses **37%** of packets
and binary loses **16%**. Resolve the frequency question first; if a clean channel is
available, stay CSV.

## Precision checked against sensor resolution

Formats were trimmed where the sensor cannot support the digits:

| Field | Sensor resolves | Format |
|---|---|---|
| temp, pres | BME280 0.01 °C / 0.0018 hPa | `%.2f` justified |
| hum | BME280 0.008 % | `%.1f` justified |
| accel | ±8 g, 0.000244 g/LSB | `%.3f` justified |
| gyro | ±500 dps, 0.0153 dps/LSB | `%.2f` justified |
| **alt** | derived, ~0.1 m of noise | `%.2f` → **`%.1f`** |
| **lat, lng** | NEO-6M ~2.5 m CEP | `%.6f` → **`%.5f`** (1.1 m) |
| **spd** | GPS ~0.36 km/h | `%.2f` → **`%.1f`** |
| **snr** | SX1262 0.25 dB steps | `%.2f` → **`%.1f`** |

Six bytes saved with **no information lost**.

> Correction to earlier advice: GEN2's `%.6f` on latitude and longitude was previously
> described here as meaningfully better than `%.5f`. That overvalued it. At ~2.5 m
> receiver accuracy, 1.1 m quantisation is already well below the noise floor.

## SD card log

Same as GEN1: auto-incrementing `/FLIGHT01.CSV` … `/FLIGHT99.CSV`, first unused name
wins, opened and closed on every write.

**Contents differ from GEN1.** The card holds complete framed packets — `$MRC` marker and
`*CRC16` included — rather than bare CSV payloads, so it is a byte-faithful record of what
was transmitted and replays through the same parser as the downlink. Two `#` comment lines
head the file instead of a CSV header row.

The `.CSV` extension is kept for spreadsheet and pandas compatibility. The cost is that
the first column reads `$MRC` and the chute column carries the checksum glued to it
(`0*3E1A`) — a small cleaning step, accepted deliberately.

Verified recipe:

```python
COLS = ["team","seq","ms","temp","hum","pres","alt","ax","ay","az",
        "gx","gy","gz","lat","lng","spd","sat","chute"]

df = pd.read_csv("FLIGHT01.CSV", comment="#", names=COLS, header=None)
df["team"] = df["team"].str.lstrip("$")
df[["chute","crc"]] = df["chute"].astype(str).str.split("*", expand=True)
df["chute"] = df["chute"].astype(int)
```

Excel: open directly, then split the last column on `*`.

## Buffers — 256 bytes on both units

Worst case is 131 characters on the vehicle and 145 at the ground station. **256** is the
next power of two with real headroom, costs nothing on an ESP32-S3 with 512 KB of SRAM,
and closes `ISS-10` by construction rather than by measurement.

```c
#define PACKET_BUF   256   // both flight and ground
```

## What this closes

| Issue | How |
|---|---|
| `ISS-07` | `chute` is a bare integer. No prefix, no parser special case |
| `ISS-08` | `seq` → real packet-loss detection · `ms` → true onboard timing · CRC16 → covers the RF hop |
| `ISS-10` | 256-byte buffers both ends |

## Dashboard impact — real work, not free

GEN3 is a different **shape**, not another field count: start marker, checksum, 19 fields
at the PC. The parser needs a third branch alongside GEN1 and GEN2.

Worth it, because it enables three things the dashboard currently has to refuse:

- **Genuine packet loss.** `rx_index` counts arrivals and cannot detect loss. `seq` can.
- **Plotting against vehicle time.** Every time-derived value currently inherits PC
  arrival jitter.
- **Rejecting corrupted packets outright** instead of range-checking and hoping.

Also required: relabel the deployment state from "Deployed" to "Commanded".

Sequencing: agree the format → write the firmware → update the parser against **real
GEN3 output**, not against an assumption.

GEN1 and GEN2 tolerance stays. Bench firmware, the CanSat SD logs and `CANSAT_DATA` are
all GEN1.
