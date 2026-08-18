# Ground Unit (GCS) — Hardware

Source: `14.GROUND_919MHz.ino`, `Pins_Assignment.md`, `MRC_DualDebug_Flow.json`

## Board

**Heltec WiFi LoRa 32 V3** — same SX1262 + OLED layout as the flight unit.
Firmware comment: "same SX1262 layout as CanSat".

Identifies itself on the OLED as **"MEeT GCS"**.

## Pin assignments

Identical LoRa pinout to the flight unit:

```
NSS = 8   SCK = 9   MOSI = 10   MISO = 11   RST = 12   BUSY = 13   DIO1 = 14
OLED      SDA = 17   SCL = 18   RST = 21
Vext = 36
```

## ⚠️ Conflict: Arduino Nano + RA-01

`Pins_Assignment.md` contains a **second** section describing an Arduino Nano wired to an
**RA-01 LoRa transceiver** (MOSI D11, MISO D12, SCK D13, NSS D10, DIO0 D2, RST D9).

No firmware in the source set targets this. Two problems:

1. The only ground station firmware present (`14.GROUND_919MHz.ino`) is written for the
   **Heltec V3 / SX1262**, not a Nano.
2. The RA-01 is an **SX1278**, a 433 MHz part. The link runs at **919 MHz**. An RA-01 cannot
   reach that band — 915 MHz needs an RA-01H / SX1276 variant.

**Open question:** is the Nano + RA-01 section obsolete, a different unit entirely, or a
planned change? Nothing in the source resolves it.

## PC connection

USB serial at **115200 baud**.

⚠️ **COM port assignments conflict between sources:**

| Source | COM5 | COM12 |
|---|---|---|
| `serial_to_mqtt_V3.py` | — | "new ground unit port" |
| `MRC_DualDebug_Flow.json` | Ground Unit | Flight Unit |

Both cannot be true. The dual-debug flow attaches both units to the PC simultaneously for
bench testing, which is the likely explanation for the second assignment — but it needs
confirming, and port numbers are machine-specific anyway.

## Serial output format

Two kinds of line come out of the ground unit and the PC side must tell them apart:

- **Telemetry** — CSV, no prefix. See `firmware/packet-format.md`.
- **Status/debug** — prefixed `[GCS]`, e.g. `[GCS] LoRa OK @ 919 MHz`,
  `[GCS] Timeout - no packet`, `[GCS] RX error code: N`.

`serial_to_mqtt_V3.py` discriminates on a leading `[`.

## OLED display

Shows `T`/`H`/`P`/`A` from the first four telemetry fields plus RSSI, SNR and a running
packet count. Falls back to a "Waiting for TX..." screen on receive timeout.
