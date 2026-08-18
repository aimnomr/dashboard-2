/* ============================================================================
 *  MRC CanSat — Flight Unit GEN3
 *
 *  GEN1's sensors and SPI layout + GEN2's receive window + the GEN3 packet.
 *
 *  Each cycle:   listen -> read sensors -> transmit -> log -> display -> hold
 *
 *  The cycle is driven by a DEADLINE, not by however long the work takes. A slow
 *  SD write shortens the hold at the end of that cycle instead of pushing every
 *  later packet late, so 1 Hz is guaranteed rather than hoped for.
 *
 *  Files:  Config.h  Radio.ino  Sensors.ino  Packet.ino
 *          Storage.ino  Chute.ino  Display.ino
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

/* Calibration quality, surfaced on the OLED. A large residual gyro bias means
 * the unit moved during calibration; 4 deg/s integrates to a full turn over a
 * flight, and that was seen on real hardware. */
float    gyroBiasWorst  = 0;

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

  /* ---- 3. TRANSMIT ------------------------------------------------------- */
  seqNumber++;
  packetBuild(packetBuf, sizeof(packetBuf), tm, seqNumber, millis(), chuteCommands);

  int txState = radio.transmit(packetBuf);

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

  /* ---- 6. HOLD ----------------------------------------------------------- */
  holdUntil(nextCycleAt);
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
