/* ============================================================================
 *  MRC CanSat — Flight Unit GEN3
 *  Configuration. Everything tunable lives here; nothing tunable lives elsewhere.
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
#define SYNC_WORD         0xAB
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
#define CHUTE_USE_SERVO         0
#define CHUTE_PIN               47
#define CHUTE_SERVO_ARMED_DEG   0
#define CHUTE_SERVO_RELEASE_DEG 90
#define CHUTE_HOLD_MS           1000   /* servo only: time to reach position */

/* ---- AUTO-EJECT ----------------------------------------------------------
 * Release on detected descent, without waiting for a command. A BACKUP to the
 * uplink, never a replacement: the ground station can still fire at any time,
 * and this can still fire if the ground station is never heard.
 *
 * The rule is a drop from the highest altitude seen so far:
 *
 *     apogee - alt >= AUTO_EJECT_DROP_M, for AUTO_EJECT_CONFIRM_N cycles
 *
 * All four numbers are meant to be changed. Tune them here and nowhere else —
 * Apogee.ino reads these and holds no constants of its own.
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
 * AUTO_EJECT_CONFIRM_N is measured in CYCLES, so at CYCLE_PERIOD_MS = 1000 it is
 * also seconds. Each cycle of confirmation costs real altitude — roughly 6 m
 * against the wiki's modelled descent, and 30 m or more in genuine freefall —
 * and buys immunity to a single anomalous pressure reading. 3 is the chosen
 * balance, not a floor: 1 is legitimate if the barometer proves quiet in flight.
 */
#define ENABLE_AUTO_EJECT       1
#define AUTO_EJECT_ARM_ALT_M    30.0f  /* must climb past this before it can fire  */
#define AUTO_EJECT_DROP_M       10.0f  /* apogee - alt that counts as descending   */
#define AUTO_EJECT_CONFIRM_N    3      /* consecutive qualifying cycles to fire    */

/* ---- SENSORS -------------------------------------------------------------- */
#define BME_ADDR          0x76
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

/* These two were the wrong way round until 2026-08-19 — the classic TX-to-TX wiring
 * fault, and the actual cause of `chars=0`. Entry 026 concluded the module was
 * unpowered; it was not, it was talking into a pin that was also transmitting.
 * GPS_RX is the ESP32's RX pin and connects to the module's TX. */
#define GPS_RX            19
#define GPS_TX            20

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
