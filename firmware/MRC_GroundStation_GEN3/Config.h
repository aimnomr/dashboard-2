/* ============================================================================
 *  MRC CanSat — Ground Station GEN3
 *  Configuration. Everything tunable lives here; nothing tunable lives elsewhere.
 *
 *  Board: Heltec WiFi LoRa 32 (ESP32-S3, on-board SX1262 + SSD1306)
 * ========================================================================= */

#pragma once

/* ---- RADIO ---------------------------------------------------------------
 * MUST match the flight unit exactly. Frequency, sync word and TEAM_ID may all
 * have to change at the launch site if channels are assigned or negotiated on
 * the day — see ISS-13. That is why they are here and not buried in Radio.ino.
 *
 * Keep SPREADING low. Shortest airtime is the smallest collision target, and
 * with other teams in the band that matters far more than range: link margin at
 * SF7 is ~70 dB at apogee.
 */
#define FREQ_MHZ          919.0
#define BANDWIDTH_KHZ     125.0
#define SPREADING         7
#define CODING_RATE       5
#define SYNC_WORD         0xAB
#define TX_POWER_DBM      17

/* Packet start marker. Identifies our traffic; anything else on the air is
 * another team's and is counted, not forwarded. */
#define TEAM_ID           "MRC"
#define PACKET_PREFIX     "$" TEAM_ID ","

/* ---- SERIAL --------------------------------------------------------------- */
#define SERIAL_BAUD       115200
/* Worst case is 144 for a GEN3.1 vehicle packet, plus ",-120.0,-20.0" of link
 * quality appended here = 158. 256 removes the question. */
#define PACKET_BUF        256
#define SERIAL_LINE_BUF   64

/* ---- UPLINK ---------------------------------------------------------------
 * The eject command is transmitted as a BURST, starting the moment the command
 * arrives over serial: EJECT_ATTEMPTS transmissions EJECT_RETRY_MS apart. The
 * burst is timed to cover a whole vehicle cycle rather than to hit a particular
 * moment in it, so it needs no estimate of where the vehicle is in its cycle.
 *
 * The previous scheme fired one shot per received telemetry packet, believing
 * that was when the vehicle opened its listen window. It is when the window has
 * just closed, and 45 transmissions across two hardware sessions were never
 * heard. See devlog 039, 043 and 044.
 *
 * TIMING CONSTRAINT, and it is load-bearing:
 *
 *   EJECT_RETRY_MS + airtime  <=  vehicle LISTEN_WINDOW_MS   (351 <= 400)
 *   (EJECT_ATTEMPTS-1) * spacing  >=  vehicle CYCLE_PERIOD_MS (1404 >= 1000)
 *
 * Both hold against the GEN3 flight unit. Shorten the vehicle's listen window
 * below ~351 ms, or reduce EJECT_ATTEMPTS below 4, and the guarantee degrades
 * to a probability without anything failing loudly.
 *
 * There is deliberately no cancel command. Once fired, the burst runs to
 * completion and cannot be stopped short of power-cycling this unit.
 */
#define CMD_EJECT         "CMD:EJECT"
#define EJECT_TOKEN       "EJECT"
#define EJECT_ATTEMPTS     5
#define EJECT_RETRY_MS   300

/* PING proves the uplink works WITHOUT firing the parachute.
 *
 * This ground station still cannot tell whether the vehicle heard it — there is
 * no acknowledgement on the uplink and there never has been. What changed on
 * 2026-08-20 is where the evidence appears: GEN3.1 carries `ul`, the vehicle's
 * count of commands received, in every telemetry packet. Press Ping and watch it
 * increment on the dashboard.
 *
 * That replaces "one person watches the sealed unit's OLED while another sends
 * this" — an instruction that could not be followed once the CanSat was closed,
 * and could not be followed at all on a set whose screen was dead. */
#define CMD_PING          "CMD:PING"
#define PING_TOKEN        "PING"
#define PING_BLIND_AFTER_MS 3000   /* if no packet arrives to time against, send anyway */

/* ---- DISPLAY -------------------------------------------------------------- */
#define ENABLE_OLED       1
#define OLED_MIN_INTERVAL_MS 250   /* an I2C buffer push is slow; do not do it per packet */

/* ---- PINS ----------------------------------------------------------------
 * LoRa is left on the default SPI bus, matching the GEN1 ground and flight
 * units. Do not move it to HSPI: on the flight unit HSPI belongs to the SD card,
 * and keeping both units identical avoids one more difference to reason about.
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

/* ---- TIMING --------------------------------------------------------------- */
#define LOOP_TICK_MS      5        /* never block longer than this */
