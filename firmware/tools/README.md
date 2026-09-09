# firmware/tools — diagnostics, NOT flight code

Nothing here flies. These sketches exist to answer specific questions faster than
reflashing the real firmware would.

Radio settings match the GEN3 firmware exactly, so the relay pair works with no
reconfiguration.

## ServoEjectTest — bench the release mechanism

Throws the release servo from a button instead of a radio, so the mechanism can be
tested without flashing flight firmware or standing up a ground station.

**Runs on the bench board, not the flight unit.** `buttonPin = 25` could not exist on
the Heltec V3: the ESP32-S3 has no GPIO 22-25, its map runs 0-21 then 26-48. The pin
numbers in this sketch are for a classic ESP32 and must not be copied into `Config.h`.

The angles are the measured ones and are the same pair compiled into both flight
generations:

| | Bench | `Config.h` (GEN3 and GEN4) |
|---|---|---|
| Armed | `ARMED_DEG 90` | `CHUTE_SERVO_ARMED_DEG 90` |
| Release | `RELEASE_DEG 160` | `CHUTE_SERVO_RELEASE_DEG 160` |
| Travel time | `HOLD_MS 1000` | `CHUTE_HOLD_MS 1000` |

**Retune in all three places or the bench stops predicting the vehicle.** This is the
same three-copy problem as the auto-eject bounds, and it fails the same silent way.

The bench returns to armed after the throw; **flight never does.** `chuteFire()` is a
one-shot latch, and only `RESET:CHUTE` over the GEN4 uplink clears it.

⚠ Reaching `RELEASE_DEG` proves the horn moved. It does not prove the parachute opened,
here or in flight — there is no feedback sensor anywhere in this system.

## GPS_PacketTest — raw GPS, on the real dashboard

**Start here.** Flash it to the flight unit and take it outside. The **existing GEN4 ground
station needs no reflash**: this sketch transmits genuine GEN3.1 `$MRC` packets on
`SYNC_WORD 0xAA`, so `python -m dashboard --port COMxx` parses them and the GPS and Ground
Track panels come alive. One sketch, one unit, no second ground build.

Every second it also prints the raw-GPS digest over USB at 115200, and puts `sat`, `hdop`,
`fixq` and `chars` on the OLED — which is the half that matters when there is no laptop.

```
[GPSPKT] chars=5820 bad=0 fixsent=61 sat=4 hdop=11.2 fixq=0
$MRC,12,12000,-999.00,-999.0,...,0.00000,0.00000,0.0,4,0,0,11.2,0*D9F7
```

⚠ **Only the GPS fields are measured.** `temp`, `hum`, `pres`, `alt` and all six IMU axes
carry **-999**, so the dashboard shows nonsense in Environment, Altitude and Attitude while
this runs. That is deliberate and it is the honest option: the parser rejects any frame
containing a blank or NaN field, so *something* numeric has to go there, and -999 falls
outside every plausible range in `parser.py` — each one arrives flagged as a warning rather
than passing silently as a reading. Zeros would not have: 0 °C, 0 %RH and 0 m are all
plausible, and a 0 g attitude renders as a confident level pose.

`chute` and `ul` are always 0, honestly — there is no mechanism and no uplink listener here.

⚠ Logs from this sketch are identical in **form** to a real flight — same prefix, same 20
fields, same CRC, same `-serial.log` name. The -999 columns are the only thing telling them
apart. Label one if you keep it.

**What to watch for:** `sat` past 6 with `hdop` under 3 gives a fix. Four satellites at
HDOP 11 outdoors means the antenna, not the wiring — see `ISS-14`.

## GPS_Relay_Flight + GPS_Relay_Ground — raw NMEA sentences

Use this pair when you need the **actual sentences** rather than a parsed position.
`GPS_PacketTest` above is the better first move for "is there a fix yet".

⚠ **Both halves are still on `SYNC_WORD 0xAB`**, stale since devlog 060 moved GEN4 to
`0xAA`. They talk to each other, so the pair works — but neither can reach the GEN4 ground
station, and a sync-word mismatch presents as **silence**, not as an error. Flashing only
one of the two units is the trap.

Flash one to each unit. The CanSat goes outside under
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
the module's **TX**. Wiring TX to TX is the classic failure and produces `chars=0` — and it
is what was actually wrong on 2026-08-19; see devlog 042, which supersedes entry 026's power
diagnosis. The numbers moved again on 2026-09-07, when the new PCB routed the pair the other
way and sent them back to 20/19 (devlog 062).

**The crossover is the invariant; the numbers are not.** Three devlog entries argue about
this pair and it has been wired both ways on real boards, so check the trace before trusting
any number written down here — including these.

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
