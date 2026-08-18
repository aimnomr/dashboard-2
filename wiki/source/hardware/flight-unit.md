# Flight Unit (CanSat) — Hardware

Source: `13.CANSAT_919MHZ.ino`, `Pins_Assignment.md`, `MRC_FlightUnit_V7.ino`

## Board

**Heltec WiFi LoRa 32** — ESP32-S3 with on-board SX1262 LoRa and SSD1306 OLED.

⚠️ **Board revision is inconsistent across sources.** `Pins_Assignment.md` and
`13.CANSAT_919MHZ.ino` describe a **V3**; `MRC_FlightUnit_V7.ino` header says **V4**
and notes "If you're on V4, double-check your pinout diagram". Needs confirming.

## Sensors

| Component | Interface | Address / Port | Config |
|---|---|---|---|
| BME280 | I²C bus 1 (`TwoWire(1)`) | `0x76` | temp, humidity, pressure, altitude |
| MPU6050 IMU | same I²C bus | `0x68` (ADD→GND) | accel ±8 g (`0x1C`=`0x10`), gyro ±500 °/s (`0x1B`=`0x08`) |
| NEO-6M GPS | `HardwareSerial(1)` | 9600 baud | parsed with TinyGPSPlus |
| microSD | HSPI (separate SPI bus) | — | avoids conflict with LoRa SPI |
| SSD1306 OLED 128×64 | I²C (hardware) | — | 3 rotating screens, 5 s interval |

## Pin assignments

**Sensor / peripheral pins** (from `Pins_Assignment.md`, confirmed against firmware `#define`s):

```
I2C (BME280 + MPU6050)    SDA = 1    SCL = 2
GPS (NEO-6M)              RX  = 20   TX  = 19
SD card (HSPI)            CS  = 4    SCK = 5    MOSI = 6    MISO = 7
OLED                      SDA = 17   SCL = 18   RST  = 21
Vext (peripheral power)   36         driven LOW = ON
```

**LoRa SX1262** (on-board, identical on flight and ground units):

```
NSS = 8   SCK = 9   MOSI = 10   MISO = 11   RST = 12   BUSY = 13   DIO1 = 14
```

**Simulator build only** (`MRC_FlightUnit_V7.ino`):

```
CHUTE_PIN = 47    driven HIGH on EJECT → servo/relay
```

## Power notes

- SD card module is fed **5 V from an external supply**, not the board 3.3 V rail
  (`Pins_Assignment.md`). Everything else is 3.3 V.
- `Vext` must be pulled LOW at boot before the OLED and peripherals will power up.

## On-board logging

The flight unit writes the same CSV payload it transmits to the SD card:

- Auto-incrementing session files `/FLIGHT01.CSV` … `/FLIGHT99.CSV`, first unused name wins.
- Header row: `temp,hum,pres,alt,ax,ay,az,gx,gy,gz,lat,lng,spd,sat`
- Opened and closed on **every single write** (once per second).
- If SD init fails the unit continues flying without logging — it does not halt.

## Boot-time calibration

1. **MPU6050** — 500 samples averaged as zero offsets. Requires the unit to be **flat and still**.
   Offsets are applied to `ax`, `ay`, `gx`, `gy`, `gz`. Note `az` is *not* offset-corrected.
2. **Altitude** — the first BME280 reading becomes `baseAltitude`; all reported altitude is
   relative to it. Sea-level pressure is hardcoded to `1013.25` hPa.
3. **GPS speed** — the first valid speed reading becomes an offset subtracted from all later
   readings, floored at 0. This calibration happens inside the *display* function
   (`showGPSScreen`), so it only runs once the GPS screen has been shown.
