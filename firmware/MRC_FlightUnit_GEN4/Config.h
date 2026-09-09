/* ============================================================================
 *  MRC CanSat — Flight Unit GEN4
 *  Configuration. Everything tunable lives here; nothing tunable lives elsewhere.
 *
 *  GEN4 = GEN3.1 packet, unchanged, plus a configurable auto-eject trigger. The
 *  values below are BOOT DEFAULTS now, not fixed constants: the ground station can
 *  change them in flight over the uplink. See the AUTO-EJECT block.
 *
 *  Board: Heltec WiFi LoRa 32 (ESP32-S3, on-board SX1262 + SSD1306)
 *  Lineage: GEN1 sensors + GEN1 SPI layout + GEN2 receive window + GEN3 packet.
 * ========================================================================= */

#pragma once

/* ---- RADIO ---------------------------------------------------------------
 * MUST match the ground station exactly. Frequency, sync word and TEAM_ID may
 * all have to change at the launch site if channels are assigned or negotiated
 * on the day — see ISS-13.
 */
#define FREQ_MHZ          919.0
#define BANDWIDTH_KHZ     125.0
#define SPREADING         7
#define CODING_RATE       5
#define SYNC_WORD         0xAA
#define TX_POWER_DBM      17

#define TEAM_ID           "MRC"
#define PACKET_BUF        256      /* worst case is 144 at GEN3.1; 256 removes the question */

/* ---- CADENCE -------------------------------------------------------------
 * CYCLE_PERIOD_MS is a hard requirement: telemetry may not be slower than 1 Hz.
 * Faster is permitted; slower is not. A deadline scheduler holds this exactly
 * rather than letting each cycle take however long it takes and drift.
 *
 * Measured budget at SF7 with a worst-case packet:
 *     listen 400 + sensors 15 + transmit 231 = 646 ms, leaving 354 ms
 *     for the SD write, the OLED, and slack.
 */
#define CYCLE_PERIOD_MS   1000
#define LISTEN_WINDOW_MS  400
#define LISTEN_TICK_MS    5

/* ---- UPLINK ---------------------------------------------------------------
 * PING exists so the two-way link can be proven BEFORE launch without firing
 * the parachute. Without it the only evidence the vehicle ever hears the ground
 * station is the chute counter rising, which is a destructive test.
 *
 * The vehicle shows time-since-last-uplink on its OLED, so the pre-launch check
 * is: one person watches the sealed unit, another presses PING.
 */
#define ENABLE_UPLINK     1
#define EJECT_TOKEN       "EJECT"
#define PING_TOKEN        "PING"

/* GEN4 commands. EJECT, PING, RESET and RESET:CHUTE are matched EXACTLY; only
 * SET_PREFIX is matched as a prefix, because only it carries a value.
 *
 * That split is deliberate. An exact match is self-validating — a garbled token
 * simply fails to match and is dropped — and it is the reason the uplink has never
 * needed a checksum of its own. Prefix matching gives that up for one command, so
 * it is confined to one command, and the LoRa CRC (radioServiceUplink only acts on
 * RADIOLIB_ERR_NONE) is what covers corruption on the air.
 *
 * RESET and RESET:CHUTE are two commands on purpose — see Apogee.ino. */
#define SET_PREFIX          "SET:"
#define RESET_TOKEN         "RESET"
#define RESET_CHUTE_TOKEN   "RESET:CHUTE"

/* ---- CHUTE ---------------------------------------------------------------
 * The release mechanism is not yet chosen. Two paths:
 *
 *   CHUTE_USE_SERVO 0  -> drive CHUTE_PIN HIGH. Suits a relay, MOSFET or a
 *                         burn-wire. This is what GEN2 did.
 *   CHUTE_USE_SERVO 1  -> sweep a hobby servo from ARMED to RELEASE. Requires
 *                         the ESP32Servo library.
 *
 * NOTE: neither path can confirm the parachute opened. There is no feedback
 * sensor, so the chute counter means "commanded", never "deployed".
 */
#define CHUTE_USE_SERVO         1
#define CHUTE_PIN               3
#define CHUTE_SERVO_ARMED_DEG   90
#define CHUTE_SERVO_RELEASE_DEG 160
#define CHUTE_HOLD_MS           1000   /* dwell at RELEASE before the horn returns to ARMED */

/* ---- CHUTE RE-ARM --------------------------------------------------------
 * The mechanism returns to its rest position CHUTE_HOLD_MS after it is driven,
 * and the fire latch clears CHUTE_REARM_MS after that same instant, so a release
 * can be commanded again without RESET:CHUTE.
 *
 * ⚠ CHUTE_REARM_MS must exceed the ground station's eject burst span, or the tail
 * of one operator EJECT re-fires the mechanism as if it were a second command:
 *
 *     burst span = (EJECT_ATTEMPTS - 1) * (EJECT_RETRY_MS + airtime)
 *                = 4 * ~351 ms = ~1404 ms
 *
 * 3000 ms leaves ~1.6 s of margin. Raise EJECT_ATTEMPTS or EJECT_RETRY_MS in the
 * ground station's Config.h and this must move with them.
 *
 * CHUTE_AUTO_REARM is the POWER-ON DEFAULT for the release mode, not the decision:
 *
 *     1  MULTI   the drive latch expires, repeat releases can be commanded
 *     0  SINGLE  one drive per boot; only RESET:CHUTE clears the latch (pre-061)
 *
 * SET:REPEAT:0|1 moves it at runtime, so a sealed unit can be switched either way
 * without opening it. A reboot forgets the override and returns to this value — the
 * same rule the apogee config follows, and the safe direction when the default is 0.
 *
 * The horn returns to ARMED after CHUTE_HOLD_MS in BOTH modes. The mode decides only
 * whether the latch expires, never whether the mechanism resets.
 *
 * ⚠ Governs the COMMANDED path only. Auto-eject is one-shot per boot in both modes.
 * ----------------------------------------------------------------------- */
#define CHUTE_AUTO_REARM        1
#define CHUTE_REARM_MS          3000

/* ---- AUTO-EJECT ----------------------------------------------------------
 * Release on detected descent, without waiting for a command. A BACKUP to the
 * uplink, never a replacement: the ground station can still fire at any time,
 * and this can still fire if the ground station is never heard.
 *
 * The rule is a drop from the highest altitude seen so far:
 *
 *     apogee - alt >= AUTO_EJECT_DROP_M, for AUTO_EJECT_CONFIRM_N cycles
 *
 * ⚠ GEN4: these four are BOOT DEFAULTS, not constants. Apogee.ino copies them into
 * runtime variables at startup, and the ground station can change any of them in
 * flight with SET:DROP / SET:CYCLES / SET:ARM / SET:AUTO. A reset returns the
 * vehicle to exactly these values — there is no NVS, and that is deliberate: what
 * is compiled here is what it will fly with unless someone actively says otherwise.
 *
 * AUTO_EJECT_ARM_ALT_M is the safety interlock and the reason this cannot fire
 * on the pad. Altitude is relative to boot (Sensors.ino zeroes it at the end of
 * calibration), so a unit sitting on the ground reads ~0 and drifts by tens of
 * centimetres. Without an arming floor, that drift sets an "apogee" of a few
 * centimetres and any dip below it is a live trigger a metre off the ground.
 * With it, the vehicle must genuinely FLY before the rule is allowed to act.
 *
 * The failure direction is deliberate: a flight that never reaches the arming
 * altitude never arms, and the uplink remains the only path. Never arming is a
 * recoverable disappointment; arming on the pad is not.
 *
 * AUTO_EJECT_CONFIRM_N is measured in SAMPLES, and since devlog 071 a sample is
 * AUTO_EJECT_SAMPLE_MS rather than a whole telemetry cycle. It used to be counted in
 * 1000 ms cycles, which made three confirmations cost 2 s and 30 m or more in genuine
 * freefall. At 125 ms the same three cost 250 ms, and buy the same thing they always
 * did: immunity to a single anomalous pressure reading.
 *
 * ⚠ Raising the sample rate SHORTENS the window three confirmations span, so it also
 * shortens the excursion this can filter. Three samples at 8 Hz reject a glitch up to
 * ~250 ms; three at 1 Hz rejected one up to ~2 s. That is the trade, and it is the
 * right way round for a trigger whose cost is measured in metres of altitude — but if
 * the barometer proves noisy in flight, raise CONFIRM_N rather than lowering the rate.
 * At 8 Hz the ceiling of 10 is still only 1.25 s.
 *
 * AUTO_EJECT_DROP_M was 10.0 until 2026-09-10 and is now at its floor, 2.0 (devlog 072).
 *
 * ⚠ It could not usefully be lowered before 071. At one sample per second and a 20 m/s
 * descent the first post-apogee sample is already 20 m down, so every threshold under
 * ~20 m behaved identically — DROP:10 and DROP:2 fired on the same sample. At 125 ms a
 * sample is well under a metre of travel early in the fall, so the threshold resolves and
 * the number finally means what it says.
 *
 * ⚠ The two changes COMPOUND, and not in the safe direction. Against the pre-071 vehicle
 * this trigger now needs a 5x smaller drop held for an 8x shorter window — 2 m over
 * 250 ms, where it used to be 10 m over 2 s. Sensor noise alone will not do that (~0.11 m
 * RMS at 16x pressure oversampling, so 2 m is ~18 sigma), but a real pressure disturbance
 * lasting 375 ms will: a gust, slipstream, a venting payload bay. The rule cannot fire
 * below AUTO_EJECT_ARM_ALT_M, so the pad is still safe — the exposure is a transient
 * during ascent, above 30 m, faking a 2 m dip below the highest altitude seen.
 *
 * If the bench or the first flight shows that, RAISE CONFIRM_N rather than the threshold:
 * SET:CYCLES is settable over the uplink at the pad and costs no reflash, and at 8 Hz the
 * ceiling of 10 is still only 1.125 s. See the trade table in COMMANDS.md.
 *
 * FLIGHT CONFIGURATION, restored 2026-09-10 after the bench run (devlog 076).
 *
 *     ARM      30.0 m   pad interlock, never moved
 *     DROP      2.0 m   argued for in 071/072: ~4-5 m of altitude lost in free fall
 *     CYCLES    3       samples, so 250 ms of confirmation at AUTO_EJECT_SAMPLE_MS
 *
 * The BOUNDS below are back at 2.0 / 5.0 as well (devlog 078). 073-075 walked them down
 * to 0.5 so the chain could be armed and fired by hand on a desk; with that done, the
 * full envelope is restored and a mistyped SET:DROP is refused again.
 *
 * ⚠ THAT MEANS THE DESK TEST IS NO LONGER REACHABLE OVER THE UPLINK. SET:ARM:0.5 and
 * SET:DROP below 2.0 are both rejected now, at the ground station before transmission
 * and at the vehicle again. Another bench session needs these three mirrored files
 * edited, not three SET commands — deliberately, because the bounds are the last thing
 * standing between a typo and a threshold the barometer cannot support.
 *
 * For reference when that day comes: 0.5 m is the lowest threshold that can tell a
 * descent from the barometer's noise at all — the running maximum drifts ~0.30 m upward
 * in 5 s from noise alone, and below that the rule fires on a stationary unit. Derived
 * in 074 and 075; do not go under it.
 */

/* What a FLIGHT configuration looks like, so the boot banner can recognise one.
 *
 * Deliberately not tied to AUTO_EJECT_DROP_MIN_M: that is the lowest value SET will
 * ACCEPT, and it has been lowered twice in one day to reach bench values. A banner
 * anchored to it would go quiet the moment the floor moved again, which is precisely
 * when it is most needed. These two are the intent, and they do not move for testing. */
#define AUTO_EJECT_FLIGHT_DROP_M     2.0f
#define AUTO_EJECT_FLIGHT_CONFIRM_N  3
#define ENABLE_AUTO_EJECT       1
#define AUTO_EJECT_ARM_ALT_M    30.0f  /* must climb past this before it can fire  */
#define AUTO_EJECT_DROP_M        2.0f  /* FLIGHT value - see devlogs 071, 072, 076 */
#define AUTO_EJECT_CONFIRM_N    3      /* restored to flight value (074)           */

/* How often the trigger takes its own altitude sample, independent of the 1 Hz
 * telemetry cadence. See devlog 071.
 *
 * ⚠ THE BAROMETER IS THE RATE LIMIT, NOT THIS NUMBER. Sensors.ino calls bme.begin()
 * without setSampling(), so the Adafruit default applies: MODE_NORMAL with 16x
 * oversampling on temperature, pressure and humidity, FILTER_OFF, 0.5 ms standby.
 * A conversion at 16x takes ~98 ms typical and ~113 ms worst case, and in normal mode
 * the data registers only update at that rate — so reading faster than ~10 Hz returns
 * the SAME conversion twice and counts one physical measurement as two confirmations.
 *
 * 125 ms sits just above the worst-case conversion, so every sample is a fresh one.
 * Going faster means dropping oversampling, which roughly doubles pressure noise for
 * every halving — and a noisier `drop` against a fixed threshold is more of exactly
 * the glitch CONFIRM_N is spending time to reject. Not a good trade.
 *
 * ⚠ Sampling is NOT uniform. The main loop blocks inside radio.transmit() for ~231 ms,
 * plus the SD write and the OLED, so roughly 700 ms of every 1000 has a poll loop
 * running. Expect ~5-6 samples a second with a gap after the transmit. CONFIRM_N counts
 * samples rather than time, so a gap DELAYS the decision; it never resets it. */
#define AUTO_EJECT_SAMPLE_MS    125

/* Bounds on what SET will accept. An out-of-range value is REJECTED, never clamped.
 *
 * Clamping would be the friendlier behaviour and the wrong one: send SET:DROP:1 to a
 * clamping vehicle and it reports success while flying a 2 m threshold you did not
 * choose. Refusing means the operator finds out immediately, at the pad, from the
 * ground station — which pre-validates against these same numbers so a bad value
 * never reaches the air at all. The check here is defence in depth, not the only
 * line: keep the two in step.
 *
 * ⚠ Mirrored in MRC_GroundStation_GEN4/Config.h. Change both together. */
#define AUTO_EJECT_DROP_MIN_M    2.0f
#define AUTO_EJECT_DROP_MAX_M  100.0f
#define AUTO_EJECT_ARM_MIN_M     5.0f
#define AUTO_EJECT_ARM_MAX_M   200.0f
#define AUTO_EJECT_CYCLES_MIN    1
#define AUTO_EJECT_CYCLES_MAX   10

/* ---- SENSORS -------------------------------------------------------------- */
#define BME_ADDR          0x76

/* Plausibility band on the altitude baseline captured at the end of calibration.
 * See sensorsCalibrate() and devlog 071 — a baseline captured during a BME280 failure
 * poisons every altitude for the whole flight, which is devlog 065 fault 3.
 *
 * Deliberately wide: this is a sanity check, not a site survey. The lowest dry land on
 * earth is about -430 m and the highest plausible launch site is a few thousand; the
 * failure this rejects read about -1740 m alongside a pressure of -164 hPa, so the band
 * does not need to be tight to catch it. Narrow it and it starts rejecting real sites.
 *
 * ⚠ Not a runtime setting and not mirrored anywhere. If it ever becomes either, it joins
 * the list of constants this project has watched drift. */
#define ALT_ZERO_MIN_M    -500.0f
#define ALT_ZERO_MAX_M    5000.0f
#define ALT_ZERO_ATTEMPTS       5
#define ALT_ZERO_RETRY_MS     200
#define MPU_ADDR          0x68
#define MPU_ACCEL_RANGE   0x10     /* register 0x1C: +/-8 g   -> 4096 LSB/g   */
#define MPU_GYRO_RANGE    0x08     /* register 0x1B: +/-500 dps -> 65.5 LSB/dps */
#define MPU_ACCEL_SCALE   4096.0f
#define MPU_GYRO_SCALE    65.5f

#define MPU_CAL_SAMPLES   500
#define MPU_CAL_DELAY_MS  5
#define SEA_LEVEL_HPA     1013.25f

/* GPS ground speed carries a standing offset when stationary. GEN1 captured it
 * on the first valid reading, which could happen with a one-satellite fix.
 * Requiring a usable fix first makes the offset far less likely to be garbage.
 * Set ENABLE_GPS_SPEED_CAL to 0 to report raw speed instead. */
#define ENABLE_GPS_SPEED_CAL 1
#define GPS_CAL_MIN_SATS     5
#define GPS_BAUD             9600

/* Longest a fix may go unrefreshed and still be transmitted as a position.
 *
 * TinyGPSPlus's isValid() never goes false once set, so without an age check the
 * vehicle reports its last known fix forever after losing signal — measured at 14
 * consecutive packets on 2026-08-19, frozen to the digit. See devlog 046.
 *
 * Three cycles. The GPS updates at 1 Hz and the vehicle samples at 1 Hz with no
 * phase relationship between them, so age at sample time is routinely several
 * hundred ms on a perfectly healthy fix; anything near 1000 would blank the
 * position at random. Three periods is comfortably clear of that and still
 * catches a real loss within a few seconds. */
#define GPS_FIX_MAX_AGE_MS   3000

/* ---- STORAGE -------------------------------------------------------------
 * Open, append, close on every write. Slower than holding the file open, and
 * chosen deliberately: a power loss at any instant costs at most the line in
 * flight. The downlink is the primary record; the card is the backup that has
 * to survive the landing.
 */
#define ENABLE_SD         1
#define SD_MAX_FILES      99

/* ---- DISPLAY -------------------------------------------------------------- */
#define ENABLE_OLED       1
#define OLED_EVERY_N      3        /* cycles between refreshes */

/* ---- SERIAL --------------------------------------------------------------- */
#define SERIAL_BAUD       115200
#define ENABLE_SERIAL_ECHO 1       /* print each packet over USB for bench testing */

/* ---- PINS ----------------------------------------------------------------
 * LoRa stays on the DEFAULT SPI bus and the SD card owns HSPI — GEN1's layout.
 * GEN2 put LoRa on HSPI because it had no SD card; combining the two as written
 * would have had both drivers claim the same peripheral.
 */
#define LORA_NSS          8
#define LORA_SCK          9
#define LORA_MOSI         10
#define LORA_MISO         11
#define LORA_RST          12
#define LORA_BUSY         13
#define LORA_DIO1         14

#define OLED_SDA          17
#define OLED_SCL          18
#define OLED_RST          21
#define VEXT_PIN          36       /* drive LOW to power the peripherals */

#define I2C_SDA           1        /* BME280 + MPU6050 share TwoWire(1) */
#define I2C_SCL           2

/* GPS_RX is the ESP32's RX pin and connects to the module's TX. That MEANING has
 * never changed; only the pin numbers have, and they have now changed twice.
 *
 *   until 2026-08-19   RX 20 / TX 19   wrong for the board of the day — the classic
 *                                      TX-to-TX fault, and the real cause of chars=0
 *   2026-08-19 (042)   RX 19 / TX 20   correct for that board
 *   2026-09-07 (062)   RX 20 / TX 19   the new PCB routes the pair the other way
 *
 * ⚠ The numbers below are back at their pre-042 values, and they are NOT a revert.
 * Devlog 042 was right about the board it was written for. Reading it now, out of
 * order, looks like this change undoes it — see 062 before "fixing" this again. */
#define GPS_RX            20
#define GPS_TX            19

#define SD_CS             4        /* HSPI */
#define SD_SCK            5
#define SD_MOSI           6
#define SD_MISO           7

/* ---- shared telemetry snapshot -------------------------------------------
 * Read once per cycle and shared. GEN1 read the sensors twice — once to build
 * the packet and again inside each display function.
 */
struct Telemetry {
  float  temp, hum, pres, alt;
  float  ax, ay, az;
  float  gx, gy, gz;
  double lat, lng;
  float  spd;
  int    sat;

  /* Added in GEN3.1, 2026-08-20 — see devlog 048.
   *
   * hdop  horizontal dilution of precision, GGA field 8. 0.0 = not reported.
   *       Satellite COUNT is a proxy for accuracy; this is the measurement. Ten
   *       satellites bunched in one patch of sky are worse than five well spread,
   *       and only this number says which you have.
   * fixq  the receiver's own verdict, GGA field 6. -1 not reported, 0 invalid,
   *       1 GPS, 2 DGPS. Sent as well as acted on locally, so the ground can tell
   *       "no fix" apart from "no GPS data at all" without a second cable. */
  float  hdop;
  int    fixq;
};
