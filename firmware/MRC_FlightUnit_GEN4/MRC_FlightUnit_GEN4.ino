/* ============================================================================
 *  MRC CanSat — Flight Unit GEN4
 *
 *  GEN1's sensors and SPI layout + GEN2's receive window + the GEN3.1 packet.
 *
 *  GEN4 changes the UPLINK ONLY. The packet is GEN3.1 byte for byte — no new
 *  fields, no checksum change — so the ground station, parser, contract and
 *  dashboard read a GEN4 vehicle exactly as they read a GEN3 one. What is new is
 *  that the auto-eject trigger is CONFIGURABLE FROM THE GROUND:
 *
 *      SET:DROP:15.0   SET:CYCLES:2   SET:ARM:50.0   SET:AUTO:0
 *      RESET           trigger state only
 *      RESET:CHUTE     trigger state + the fire latch, for sealed bench testing
 *
 *  Because only the uplink grammar changed, a mismatched pair degrades safely in
 *  both directions: a GEN3 vehicle ignores SET as foreign traffic and never moves
 *  `ul`, so a GEN4 ground station's burst reports failure loudly rather than
 *  pretending. That is NOT true of the GEN3.0/GEN3.1 split — see status.md.
 *
 *  Each cycle:   listen -> read sensors -> transmit -> log -> display -> hold
 *
 *  The cycle is driven by a DEADLINE, not by however long the work takes. A slow
 *  SD write shortens the hold at the end of that cycle instead of pushing every
 *  later packet late, so 1 Hz is guaranteed rather than hoped for.
 *
 *  Files:  Config.h  Radio.ino  Sensors.ino  Packet.ino
 *          Storage.ino  Chute.ino  Apogee.ino  Display.ino
 * ========================================================================= */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include "Config.h"

/* ---- objects -------------------------------------------------------------- */
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
TwoWire        I2C_SENS = TwoWire(1);
Adafruit_BME280 bme;
TinyGPSPlus     gps;

/* The module's own verdict on its fix — GGA field 6: 0 invalid, 1 GPS, 2 DGPS.
 * TinyGPSPlus exposes no accessor for it, so it is read as a custom term.
 *
 * Declared here rather than in Sensors.ino on purpose: TinyGPSCustom's constructor
 * registers itself with `gps`, so `gps` must already be constructed. The Arduino
 * build concatenates the main sketch first, and keeping these adjacent to it makes
 * that dependency visible instead of load-bearing and invisible.
 *
 * Both talkers are registered because which one arrives is a property of the
 * hardware: a GPS-only NEO-6M emits $GPGGA, a multi-constellation module $GNGGA. */
TinyGPSCustom   ggaQualityGp(gps, "GPGGA", 6);
TinyGPSCustom   ggaQualityGn(gps, "GNGGA", 6);

/* HDOP — GGA field 8. The accuracy figure the satellite count only approximates. */
TinyGPSCustom   ggaHdopGp(gps, "GPGGA", 8);
TinyGPSCustom   ggaHdopGn(gps, "GNGGA", 8);
HardwareSerial  GPSSerial(1);
SPIClass        sdSPI(HSPI);

/* ---- shared state --------------------------------------------------------- */
Telemetry tm;                       /* this cycle's readings */

uint32_t seqNumber      = 0;        /* monotonic packet counter from boot */
uint32_t chuteCommands  = 0;        /* eject commands received. 0 = armed */
bool     loraReady      = false;
bool     sdReady        = false;

/* Uplink health — for the OLED, so the two-way link can be confirmed on the pad
 * without a laptop and without firing the chute. See PING in Config.h. */
uint32_t pingCount      = 0;
uint32_t lastUplinkMs   = 0;
bool     uplinkHeard    = false;    /* has the ground station EVER been heard? */

/* Every uplink command actually RECEIVED over the air — pings and ejects both.
 *
 * This is the `ul` field, and it used to be computed at the packet as
 * `pingCount + chuteCommands`. That identity held only while the uplink was the
 * one and only thing that could move the chute counter. Auto-eject breaks it: a
 * vehicle that released on its own would have reported ul = 1 having never heard
 * the ground station at all, which is the exact opposite of what this field is
 * for. Counted at the radio now, where the evidence actually is. */
uint32_t uplinkCount    = 0;

/* Calibration quality, surfaced on the OLED. A large residual gyro bias means
 * the unit moved during calibration; 4 deg/s integrates to a full turn over a
 * flight, and that was seen on real hardware. */
float    gyroBiasWorst  = 0;

/* Cycles that failed to finish inside CYCLE_PERIOD_MS. Should stay at 0. */
uint32_t cycleOverruns  = 0;

char     packetBuf[PACKET_BUF];

static uint32_t nextCycleAt = 0;

/* ========================================================================== */

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);
  Serial.println();
  Serial.println("[FLT] MRC Flight Unit GEN3 booting");

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);       /* peripherals on */
  delay(100);

  displayBegin();
  displayMessage("MRC FLIGHT GEN3", "Initialising...", "", "");

  chuteBegin();                      /* before anything can fail and halt */

  sensorsBegin();                    /* halts on BME280 or MPU6050 failure */
  loraReady = radioBegin();
  if (!loraReady) {
    Serial.println("[FLT] LoRa init FAILED - halted");
    displayMessage("LoRa FAILED", "Check wiring", "HALTED", "");
    while (true) delay(1000);
  }

  sdReady = storageBegin();          /* non-fatal: fly without the card */

  sensorsCalibrate();                /* MPU offsets, then altitude zero */
  apogeeBegin();                     /* AFTER the altitude zero, never before */

  Serial.print("[FLT] ready  ");
  Serial.print(FREQ_MHZ, 1);
  Serial.print(" MHz SF");
  Serial.print(SPREADING);
  Serial.print("  team ");
  Serial.print(TEAM_ID);
  Serial.print("  SD ");
  Serial.println(sdReady ? "OK" : "ABSENT");

  displayMessage("MRC FLIGHT GEN3",
                 sdReady ? "SD: OK" : "SD: NOT FOUND",
                 "Ready", "");
  delay(1200);

  nextCycleAt = millis();
}

void loop() {
  nextCycleAt += CYCLE_PERIOD_MS;

  /* ---- 1. LISTEN ---------------------------------------------------------
   * GPS is fed on every tick inside the window. At 9600 baud the UART FIFO
   * fills in about 130 ms, so leaving it unread for a 400 ms window would drop
   * NMEA sentences. */
  if (ENABLE_UPLINK) {
    if (radioListenForEject(LISTEN_WINDOW_MS)) {
      chuteCommands++;
      chuteFire();                    /* idempotent - safe on every repeat */
      Serial.print("[FLT] EJECT received, count ");
      Serial.println(chuteCommands);
    }
  } else {
    holdUntil(millis() + LISTEN_WINDOW_MS);
  }

  /* ---- 2. SENSORS -------------------------------------------------------- */
  sensorsRead(tm);

  /* ---- 2b. AUTO-EJECT ----------------------------------------------------
   * Placed between the sensor read and the packet build deliberately: a release
   * decided here is visible in THIS cycle's `chute`, not the next one. A second's
   * delay would be invisible on the ground and is free to avoid.
   *
   * Counted into chuteCommands exactly like an uplink command, because the field
   * means "release commanded" and this is a release commanded. chuteFire() is
   * idempotent, so a ground EJECT arriving afterwards drives nothing — see the
   * one-shot latch in Chute.ino. */
  if (apogeeUpdate(tm.alt)) {
    chuteCommands++;
    chuteFire();
    Serial.print("[FLT] chute released by AUTO-EJECT, count ");
    Serial.println(chuteCommands);
  }

  /* ---- 3. TRANSMIT ------------------------------------------------------- */
  seqNumber++;
  /* `ul` is TOTAL uplink commands received — pings plus ejects. One number above
   * zero is proof the two-way link has ever worked, which is the question that
   * matters. Until now this lived only on the OLED, invisible once the unit is
   * sealed. See devlog 037 and 048.
   *
   * `pings alone are recoverable as ul - chute` was true when the uplink was the
   * only thing that could move `chute`, and is not true now that the vehicle can
   * release itself. Read ul as what it is — proof of reception — and nothing more. */
  packetBuild(packetBuf, sizeof(packetBuf), tm, seqNumber, millis(),
              chuteCommands, uplinkCount);

  int txState = radio.transmit(packetBuf);

  /* ---- 3b. RE-ARM THE RECEIVER — before the SD write, not after -----------
   * The ground station transmits about 10-15 ms after this packet lands (its
   * readData, RSSI/SNR, a ~100 byte serial println at 115200, then the CRC).
   * Arming here puts the radio in receive at roughly t=647 ms, well before the
   * uplink preamble arrives.
   *
   * Order is the whole point. The SX1262 receives autonomously once armed and
   * holds the packet until it is read, so the SD write costs nothing — but only
   * if the radio is already listening when the preamble starts. Arming after
   * storageWrite() would put the card's open/append/close in front of exactly
   * the window the ground station uses, which is the same bug in a new place.
   *
   * The two are on separate SPI buses (LoRa on the default, SD on HSPI via
   * sdSPI), so nothing is contended by having receive live across the write. */
  if (ENABLE_UPLINK) radioArmReceive();

#if ENABLE_SERIAL_ECHO
  Serial.println(packetBuf);
  if (txState != RADIOLIB_ERR_NONE) {
    Serial.print("[FLT] TX failed, code ");
    Serial.println(txState);
  }
#endif

  /* ---- 4. LOG ------------------------------------------------------------
   * After the transmission, never before: the downlink is the primary record
   * and must not wait behind a card that decided to stall. */
  storageWrite(packetBuf);

  /* ---- 5. DISPLAY -------------------------------------------------------- */
  if (seqNumber % OLED_EVERY_N == 0) displayTelemetry(tm);

  /* ---- 6. HOLD -----------------------------------------------------------
   * Report overruns rather than absorbing them silently: the budget says this
   * cycle should finish in ~650 ms of 1000, and the SD write is the term least
   * possible to predict from a datasheet. If this line appears on the bench,
   * the margin is not what the plan assumed.
   */
  int32_t remaining = (int32_t)(nextCycleAt - millis());
  if (remaining < 0) {
    cycleOverruns++;
    Serial.print("[FLT] cycle overran by ");
    Serial.print(-remaining);
    Serial.print(" ms (");
    Serial.print(cycleOverruns);
    Serial.println(" total)");

    /* Fallen more than a whole period behind: resync instead of running cycles
     * back to back to catch up. A burst of packets is worse than a late one —
     * it breaks cadence in the other direction and floods the channel. */
    if (remaining < -(int32_t)CYCLE_PERIOD_MS) {
      nextCycleAt = millis();
      Serial.println("[FLT] cadence resynchronised");
    }
  }

  /* ---- 7. HOLD, LISTENING ------------------------------------------------
   * This used to be a blind delay, and it was the largest deaf stretch in the
   * cycle: 354 ms of every 1000, immediately after the transmit — which is
   * precisely when the ground station sends. See devlog 044. */
  holdUntilListening(nextCycleAt);
}

/* Wait for a deadline while keeping the GPS parser fed. Returns immediately if
 * the deadline has already passed — an overrunning cycle must not compound by
 * waiting a whole extra period. */
void holdUntil(uint32_t deadlineMs) {
  while ((int32_t)(deadlineMs - millis()) > 0) {
    gpsFeed();
    delay(1);
  }
}

/* Hold to a deadline while both feeding the GPS and servicing the uplink.
 *
 * Commands can now arrive anywhere in the back half of the cycle, so they are
 * dispatched here as well as in the front window — through the same
 * radioServiceUplink() in both places, never a second copy of the token
 * matching. chuteFire() is idempotent, so a command landing here needs no
 * special handling relative to one landing in the window. */
void holdUntilListening(uint32_t deadlineMs) {
  while ((int32_t)(deadlineMs - millis()) > 0) {
    gpsFeed();

    if (ENABLE_UPLINK && radioServiceUplink()) {
      chuteCommands++;
      chuteFire();
      Serial.print("[FLT] EJECT received, count ");
      Serial.println(chuteCommands);
    }

    delay(LISTEN_TICK_MS);
  }
}
