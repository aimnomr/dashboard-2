# GEN1 vs GEN2 — Structural Comparison

Written 2026-08-18 to support rewriting GEN2 as flight firmware.

⚠️ **Reconstructed from notes, not from source.** The `.ino` files were removed before
this was written (`ISS-09`). Everything below comes from the wiki extracts and a reading
of the files earlier that day. It is reliable at the structural level — what each build
does, which peripherals it touches, which pins it uses — but it is **not** a substitute
for the source when transcribing sensor code. Register sequences and calibration loops
must be copied from the real file, not retyped from prose.

The simulation is factored out throughout: GEN2's synthetic flight profile is a test
harness, not part of the design.

---

## Identical in both

Nothing here needs deciding — it already agrees.

| | |
|---|---|
| Board | Heltec WiFi LoRa 32 (ESP32-S3, on-board SX1262 + SSD1306) |
| Libraries | RadioLib, U8g2, Wire, SPI |
| LoRa pins | NSS 8 · SCK 9 · MOSI 10 · MISO 11 · RST 12 · BUSY 13 · DIO1 14 |
| OLED pins | SDA 17 · SCL 18 · RST 21 · Vext 36 (LOW = on) |
| Radio | 919.0 MHz · BW 125 kHz · SF7 · CR 4/5 · sync `0xAB` · 17 dBm |
| Cadence | 1 Hz |

## GEN1 only — the sensor half GEN2 lacks

This is the code to carry forward. GEN2 has **none** of it.

**Peripherals and pins**

```
I2C (TwoWire(1))   SDA 1   SCL 2      BME280 @0x76, MPU6050 @0x68
GPS  HardwareSerial(1)  9600  RX 20  TX 19     TinyGPSPlus
SD   HSPI          CS 4  SCK 5  MOSI 6  MISO 7
```

**MPU6050 is driven by raw register access**, not a library — wake `0x6B`, accel range
`0x1C`, gyro range `0x1B`, then a 14-byte burst read from `0x3B`. Scale factors are
hardcoded to the configured ranges (±8 g, ±500 °/s). Copy this verbatim; the constants
only make sense together.

**Boot calibration**

1. MPU: 500 samples averaged into gyro X/Y/Z offsets and accel X/Y offsets.
   **`az` is deliberately not offset-corrected.**
2. Altitude: first BME280 reading becomes `baseAltitude`; everything reported is relative
   to it. Sea-level pressure hardcoded `1013.25`.
3. GPS speed: first valid reading becomes an offset. **This runs inside the GPS display
   function**, so it only happens once that screen has been shown — a latent bug worth
   fixing during the rewrite rather than carrying over.

**SD logging** — `/FLIGHT01.CSV` … `/FLIGHT99.CSV`, first unused name wins, header row
written at creation, file opened and closed on every write.

**OLED** — three rotating screens (BME / GPS / MPU) on a 5 s timer. The display functions
re-read the sensors independently of the transmit path.

**Loop** — feed GPS parser, transmit every 1000 ms, rotate screens, `delay(200)`.

**Radio** — `radio.transmit()` only. No receive path, no chute output.

## GEN2 only — the two-way half GEN1 lacks

**Chute output** — `CHUTE_PIN 47`, driven HIGH on command.

**Receive window** — `listenForEject()`: `startReceive()` → poll `DIO1` for 800 ms in 5 ms
ticks → `readData()` → `standby()`. The header records why this shape was chosen:

> Dropped `radio.receive()` entirely — timeout param behavior varies between RadioLib
> versions and was blocking forever.

Keep that. It is a fix, not a style choice.

**Cycle** — listen 800 ms → transmit → repeat, ≈1 s total. After deployment it still
`delay(800)` in place of listening, so cadence stays constant.

**Packet** — 15 fields, higher precision than GEN1, with `CHUTE:0`/`CHUTE:1` appended.

## ⚠️ The collision to resolve first: both builds want HSPI

This is the one place where merging naively produces a bug rather than a merge conflict.

| | LoRa | SD card |
|---|---|---|
| **GEN1** | default SPI (`new Module(...)`, no bus argument) | **HSPI** (`SPIClass sdSPI(HSPI)`) |
| **GEN2** | **HSPI** (`SPIClass loraSPI(HSPI)`, passed to `Module`) | none |

GEN1 deliberately put the SD card on HSPI to keep it off the LoRa bus. GEN2, having no SD
card, took HSPI for LoRa instead. **Combine them as written and both drivers initialise
the same peripheral.**

Resolve by deciding which device owns HSPI before writing anything else — the pin
definitions are unaffected either way, so this is a one-line choice made early or a
mystifying bus fault found late.

## Suggested merge order

1. **Start from GEN1**, not GEN2. GEN1 is real flight code; GEN2 is a test harness with a
   good receive routine in it. Taking the smaller, better-understood addition into the
   working article is lower risk than the reverse.
2. **Resolve the HSPI ownership** above.
3. Add `CHUTE_PIN` and the `chute_deployed` flag.
4. Port `listenForEject()` in as-is.
5. Restructure the loop to listen-then-transmit, keeping the ~1 s cadence.
6. Widen the packet format string to GEN2 precision and append `CHUTE:%d`.
7. Match the SD header row to the new field list — GEN1's header has 14 columns.
8. Delete nothing from GEN1's calibration; fix the GPS-speed-offset placement noted above.

## Dashboard impact

None. GEN2's 17-field form at the PC is already the canonical contract, is implemented,
and is tested. GEN1 stays tolerated so bench firmware and existing SD logs keep parsing.
See `packet-format.md` and `ISS-01`.

The remaining firmware gap is `ISS-02` — the **ground unit** side of the uplink, which
exists in no source file that has been seen. Without it the vehicle can listen for
`EJECT` but nothing will ever send one.
