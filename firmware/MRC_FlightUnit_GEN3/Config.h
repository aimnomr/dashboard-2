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
#define PACKET_BUF        256      /* worst case is 133; 256 removes the question */

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
};
