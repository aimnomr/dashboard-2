# Ingest Pipeline

Decided 2026-08-18. The shape of the path from ground unit to screen.

```
CanSat ──LoRa 919MHz 1Hz──> Ground unit ──USB serial 115200──> PC
                                                                │
                            ┌───────────────────────────────────┤
                            ▼                                   ▼
                     RAW LOG (bytes)                    parser → validated frame
                     written FIRST,                             │
                     unconditionally                   ┌────────┴────────┐
                                                       ▼                 ▼
                                                 parsed log         live UI
```

## Principles

### 1. Raw bytes hit disk before parsing, always

Every byte off the serial port is appended to a raw log **before** any attempt to interpret it,
with no filtering and no validation gate. If the parser has a bug, the packet format drifts, or
an unknown line type appears, the flight is still fully recoverable afterwards.

Competition flights do not get a second attempt. This is the non-negotiable one.

**This exact failure existed in v1.** `serial_to_mqtt_V3.py` wrote nothing to disk — packets
went straight to MQTT. If the broker was down, Node-RED wasn't running, or the script threw,
that data was gone permanently. Malformed lines were printed and discarded.
See `../source/previous-system/serial-to-mqtt-bridge.md`.

### 2. The ground unit is a dumb pipe

Receive LoRa, append RSSI/SNR, forward over USB. No parsing, no filtering, no buffering
decisions on the ESP32 — including malformed packets, so corruption is visible rather than
silently swallowed.

This is what `14.GROUND_919MHz.ino` already does.

### 3. Source is a seam

Live and replay differ only in where bytes come from — a serial port or a file. Parser,
validation, storage and UI are identical downstream.

The seam is designed in from the start because retrofitting it means rewriting the ingest layer.
**Replay itself is not being built yet** — live only, features taken independently.

## Consequences for the parser

Driven by what the source material showed:

- Must tolerate **both 16-field (GEN1) and 17-field (GEN2)** lines — see `ISS-01`.
- Must strip the `CHUTE:` prefix before numeric parsing — see `ISS-07`.
- Must filter `[GCS]`-prefixed status lines rather than parse them.
- Must not assume GEN1 precision when reading GEN2 — GEN2 carries extra decimals, notably on
  lat/lng.
- Must survive a malformed line without dropping the connection, and must record that a line
  was malformed rather than silently discarding it.
- Must not hardcode a COM port — enumerate and select. See `ISS-05`.

## Not yet decided

- Stack and language — deferred.
- Storage format for the parsed log, and whether logs are committed to the repo.
- Whether link-health state (packet gaps, time since last packet) is computed at ingest or in
  the UI. Note that gap detection is impossible until `ISS-08` adds a packet counter.
