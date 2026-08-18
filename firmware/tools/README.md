# firmware/tools — diagnostics, NOT flight code

Nothing here flies. These sketches exist to answer specific questions faster than
reflashing the real firmware would.

Radio settings match the GEN3 firmware exactly, so the relay pair works with no
reconfiguration.

## GPS_Relay_Flight + GPS_Relay_Ground — use this pair

**The GPS test you actually want.** Flash one to each unit. The CanSat goes outside under
clear sky; the ground unit stays on the laptop and prints everything over USB at 115200.

Once per second the CanSat sends:

```
$GPSD,12,chars=5820,ok=61,bad=0,inview=7,used=0,fix=0,hdop=-   [-64 dBm  SNR 9.2]
$GPGGA,,,,,,0,00,99.99,,,,,,*48                                [-64 dBm  SNR 9.2]
```

A digest, plus **one full raw NMEA sentence**, rotating GGA → GSV → RMC → GSA so all of
it is visible across four seconds. The CanSat's own OLED shows the same summary, so it can
also be read while standing next to it.

### Why not relay all the NMEA

A NEO-6M emits roughly 400–600 bytes every second. At SF7/BW125 a 100-byte packet is about
180 ms of airtime, so the channel tops out near 500 bytes/second at **100% duty cycle**.
Relaying everything is not possible, and attempting it would jam the band.

### Reading the result

| Symptom | Diagnosis | What to do |
|---|---|---|
| `chars=0` | The module is not reaching the ESP32 | TX/RX not crossed, wrong baud, or no power. Nothing else matters until this is fixed |
| `chars>0`, `bad>0` | Data arriving corrupted | Baud mismatch, or missing common ground |
| `chars>0`, `inview=0` | Wiring fine, antenna sees nothing | Check the antenna connector; or you are still effectively indoors |
| `inview>0`, `used=0` | **Antenna works, still acquiring** | Wait. A cold start can take 15 minutes. This is the encouraging case |
| `used>=4`, `fix=1` | Fix acquired | Done |

The `inview` vs `used` split is the useful part: satellites *in view* prove the antenna and
sky are fine even when there is no fix yet, which is exactly the distinction the flight
firmware's `sat` field cannot make.

### Wiring reminder

```
GPS module TX  ->  ESP32 pin 20     (the ESP32 RECEIVES here)
GPS module RX  ->  ESP32 pin 19     (the ESP32 TRANSMITS here)
GPS VCC -> 3V3      GPS GND -> GND
```

Note the crossover. `GPS_RX 20` in `Config.h` means *the ESP32's RX pin*, which connects to
the module's **TX**. Wiring TX to TX is the classic failure and produces `chars=0`.

## UART_PinTest — run this when the relay reports `chars=0`

`chars=0` means **zero bytes ever arrived**, which is different from garbage. A wrong baud
rate still produces garbage, because the UART samples edges and emits nonsense. Zero rules
out sky, antenna, cold start and baud in one reading — nothing is reaching the pin.

**On the ESP32-S3, GPIO19 and GPIO20 are the native USB D− and D+ lines**, and the GPS is
wired to exactly those. With *USB CDC On Boot* enabled, the USB peripheral claims them and
they silently stop working as a UART — indistinguishable from a dead module.

This sketch settles it without the GPS being involved at all:

1. Disconnect the GPS from those pins.
2. Jumper pin 19 directly to pin 20.
3. Flash, open Serial Monitor at 115200.

| Result | Meaning |
|---|---|
| `LOOPBACK OK` | The pins work. The fault is the module, its power, or the wiring |
| `LOOPBACK FAIL` | The pins are not usable as a UART. Try **Tools → USB CDC On Boot → Disabled**, reflash, retest. If it still fails, move the GPS to another pin pair |

Change `TEST_RX_PIN` / `TEST_TX_PIN` to try a candidate pair *before* committing to
rewiring. See `ISS-14`.

## GPS_Passthrough — desk test only

Dumps raw GPS output straight to USB and sweeps five baud rates. Useful when the unit is
already on the bench and you only want to know whether the module is alive at all. For
anything involving sky, use the relay pair instead — this one requires the CanSat to be
tethered to the laptop, which is the wrong place for it.
