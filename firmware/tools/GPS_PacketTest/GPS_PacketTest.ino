/* ============================================================================
 *  GPS PACKET TEST.  Diagnostic sketch, NOT flight code.
 *
 *  Raw GPS, wrapped in a real GEN3.1 packet, so the ordinary dashboard shows it.
 *
 *  Put this on the flight unit and take it outside. The EXISTING GEN4 ground
 *  station forwards it with no reflash, `python -m dashboard --port COMxx` parses
 *  it, and the GPS and Ground Track panels come alive. No second ground sketch, no
 *  serial monitor, no separate tooling.
 *
 *  That is the difference from GPS_Relay_Flight, which needs its own paired ground
 *  sketch, emits a private `$GPSD` digest the dashboard cannot read, and is still on
 *  SYNC_WORD 0xAB — stale since devlog 060 moved GEN4 to 0xAA, so it cannot reach the
 *  current ground station at all. Use that one for raw NMEA sentences; use this one to
 *  watch a fix arrive on the dashboard you will actually fly with.
 *
 *  ---------------------------------------------------------------------------
 *  ⚠ WHAT THIS PACKET IS AND IS NOT
 *
 *  Only the GPS fields are measured. This sketch never touches the BME280 or the
 *  MPU6050, and there is no uplink listener and no chute.
 *
 *      MEASURED      seq  ms  lat  lng  spd  sat  hdop  fixq
 *      NOT MEASURED  temp  hum  pres  alt  ax ay az  gx gy gz     -> -999
 *      ALWAYS ZERO   chute  ul        (nothing commanded; no uplink to hear it)
 *
 *  The unmeasured fields carry -999 rather than a plausible-looking zero, and that
 *  is deliberate. Two reasons:
 *
 *  1. The parser REJECTS a whole frame containing a non-finite or non-numeric field
 *     (`parser.py::_convert`), so blanks and NaN are not available — something
 *     numeric has to go there or nothing reaches the dashboard.
 *
 *  2. -999 falls outside every plausible range in `parser.py::_PLAUSIBLE`, so each
 *     one is flagged as a warning on arrival. Range violations are warnings and never
 *     rejections, so the frame still parses and the GPS still displays — but nothing
 *     invented here can be mistaken for a measurement. Zeros would have passed
 *     silently: 0 degC, 0 %RH and 0 m are all inside their plausible bands, and a
 *     level 0 g attitude would have rendered as a confident pose.
 *
 *  The dashboard will therefore show nonsense in Environment, Altitude and Attitude
 *  while this sketch is running. That is the intended and honest outcome.
 *
 *  ⚠ Logs written from this sketch are byte-identical in FORM to a real flight —
 *  same `$MRC` prefix, same 20 fields, same CRC — and `logs/raw/` names them
 *  `-serial.log` like any other. The -999 columns are what tells them apart. Say so
 *  if you keep one.
 * ========================================================================= */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

/* ---- must match the GEN4 pair --------------------------------------------
 * SYNC_WORD 0xAA since devlog 060. If this and the ground station disagree the
 * link is not merely lossy, it is silent — a mismatched sync word never raises
 * DIO1, so there is no error to see. */
#define FREQ_MHZ      919.0
#define BANDWIDTH_KHZ 125.0
#define SPREADING     7
#define CODING_RATE   5
#define SYNC_WORD     0xAA
#define TX_POWER_DBM  17

#define TEAM_ID       "MRC"

#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_PIN   36

/* GPS_RX is the ESP32's RX pin and connects to the module's TX. Mirrors GPS_RX /
 * GPS_TX in MRC_FlightUnit_GEN4/Config.h — 20/19 for the new PCB since devlog 062,
 * and confirmed on hardware by the sat=4 window in ISS-14. */
#define GPS_RX_PIN 20        /* ESP32 receives here  <- module TX */
#define GPS_TX_PIN 19        /* ESP32 transmits here -> module RX */
#define GPS_BAUD   9600

#define PERIOD_MS  1000

/* Mirrors GPS_FIX_MAX_AGE_MS in the flight firmware. TinyGPSPlus's isValid()
 * latches true forever, so a module that stops talking keeps vouching for a fix it
 * can no longer see — every read below is age-checked for that reason. Devlog 046. */
#define FIX_MAX_AGE_MS 3000

/* The value written into every field this sketch does not measure. Outside all of
 * parser.py's plausible ranges by construction — see the header. */
#define NOT_MEASURED (-999.0f)

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
HardwareSerial GPSSerial(1);
TinyGPSPlus    gps;

/* GGA fields 6 and 8. Both prefixes, because a GPS-only module talks GP and a
 * multi-constellation one talks GN — same as the flight firmware. */
TinyGPSCustom ggaQualityGp(gps, "GPGGA", 6);
TinyGPSCustom ggaQualityGn(gps, "GNGGA", 6);
TinyGPSCustom ggaHdopGp(gps, "GPGGA", 8);
TinyGPSCustom ggaHdopGn(gps, "GNGGA", 8);

uint32_t seq    = 0;
uint32_t nextAt = 0;

/* CRC16/CCITT-FALSE — poly 0x1021, init 0xFFFF, no reflection, no final xor.
 * Byte-for-byte identical to MRC_FlightUnit_GEN4/Packet.ino. If this drifts the
 * ground station forwards the packet and the dashboard rejects it as corrupt.
 * firmware/tests/verify_gen3.py is the reference. */
uint16_t crc16Ccitt(const char *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* 0.0 when the module never reported it. A real HDOP is never zero, so the sentinel
 * cannot be read as a good value. Matches gpsHdop() in Sensors.ino. */
float gpsHdop() {
  TinyGPSCustom *terms[2] = { &ggaHdopGp, &ggaHdopGn };
  for (uint8_t i = 0; i < 2; i++) {
    if (terms[i]->isValid() && terms[i]->age() < FIX_MAX_AGE_MS) {
      float v = atof(terms[i]->value());
      if (v > 0.0f) return v;
    }
  }
  return 0.0f;
}

/* -1 never reported · 0 receiver says INVALID · 1 GPS · 2 DGPS. -1 rather than 0 for
 * "never reported" so a module that does not send GGA cannot veto a position.
 * Matches gpsFixQuality() in Sensors.ino. */
int gpsFixQuality() {
  TinyGPSCustom *terms[2] = { &ggaQualityGp, &ggaQualityGn };
  int best = -1;
  for (uint8_t i = 0; i < 2; i++) {
    if (terms[i]->isValid() && terms[i]->age() < FIX_MAX_AGE_MS) {
      int v = atoi(terms[i]->value());
      if (v > best) best = v;
    }
  }
  return best;
}

void setup() {
  Serial.begin(115200);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(150);

  oled.begin();
  oled.setBusClock(400000);
  oled.setFont(u8g2_font_6x10_tf);

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  int state = radio.begin(FREQ_MHZ, BANDWIDTH_KHZ, SPREADING,
                          CODING_RATE, SYNC_WORD, TX_POWER_DBM);
  if (state != RADIOLIB_ERR_NONE) {
    oled.clearBuffer();
    oled.drawStr(0, 20, "LoRa FAILED");
    oled.sendBuffer();
    Serial.print("[GPSPKT] LoRa init failed, code ");
    Serial.println(state);
    while (true) delay(1000);
  }

  /* Stamped so a log can be traced to the build that wrote it — the ground station
   * has carried one since devlog 059 and it has already earned its place twice. */
  Serial.println("[GPSPKT] GPS packet test - GEN3.1 frames, GPS fields only");
  Serial.print("[GPSPKT] build ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  Serial.println("[GPSPKT] temp/hum/pres/alt and the IMU are -999: NOT MEASURED");

  nextAt = millis();
}

/* Drain the UART into the parser. Must be called often — at 9600 baud the FIFO
 * fills in roughly 130 ms. */
void pumpGps() {
  while (GPSSerial.available() > 0) gps.encode((char)GPSSerial.read());
}

void loop() {
  pumpGps();

  if ((int32_t)(nextAt - millis()) > 0) {
    delay(2);
    return;
  }
  nextAt += PERIOD_MS;
  seq++;

  /* Age-checked, not just isValid(): see FIX_MAX_AGE_MS. 0.0 for an absent position
   * is what the flight firmware sends and what the dashboard reads as "no fix" —
   * it is deliberately not a coordinate. */
  bool  havePos = gps.location.isValid() && gps.location.age() < FIX_MAX_AGE_MS;
  float lat     = havePos ? (float)gps.location.lat() : 0.0f;
  float lng     = havePos ? (float)gps.location.lng() : 0.0f;
  float spd     = (gps.speed.isValid() && gps.speed.age() < FIX_MAX_AGE_MS)
                    ? (float)gps.speed.kmph() : 0.0f;
  int   sat     = (gps.satellites.isValid() && gps.satellites.age() < FIX_MAX_AGE_MS)
                    ? (int)gps.satellites.value() : 0;
  float hdop    = gpsHdop();
  int   fixq    = gpsFixQuality();

  /* Field order, widths and separators are copied from packetBuild() in
   * MRC_FlightUnit_GEN4/Packet.ino and must stay identical. The CRC covers the body
   * WITHOUT the leading '$', and the ground station appends ",rssi,snr" after the
   * checksum — so this covers exactly what left the vehicle. */
  char body[192];
  snprintf(body, sizeof(body),
    "%s,%lu,%lu,"                 /* team, seq, ms                */
    "%.2f,%.1f,%.2f,%.1f,"        /* temp, hum, pres, alt         */
    "%.3f,%.3f,%.3f,"             /* ax, ay, az                   */
    "%.2f,%.2f,%.2f,"             /* gx, gy, gz                   */
    "%.5f,%.5f,%.1f,%d,"          /* lat, lng, spd, sat           */
    "%lu,"                        /* chute: no mechanism here     */
    "%lu,%.1f,%d",                /* ul, hdop, fixq               */
    TEAM_ID,
    (unsigned long)seq,
    (unsigned long)millis(),
    NOT_MEASURED, NOT_MEASURED, NOT_MEASURED, NOT_MEASURED,
    NOT_MEASURED, NOT_MEASURED, NOT_MEASURED,
    NOT_MEASURED, NOT_MEASURED, NOT_MEASURED,
    lat, lng, spd, sat,
    0UL,
    0UL, hdop, fixq);

  char packet[208];
  snprintf(packet, sizeof(packet), "$%s*%04X", body, crc16Ccitt(body, strlen(body)));

  radio.transmit(packet);

  /* The raw-GPS half, on USB serial. "No characters" and "characters but no fix" are
   * completely different faults and must never look the same:
   *
   *   chars == 0            wiring or baud. The module is not reaching us.
   *   chars > 0, fix == 0   wiring is fine. Antenna, sky view, or cold start.
   *   bad checksums high    data arriving corrupted: baud mismatch or a bad ground. */
  Serial.print("[GPSPKT] chars=");    Serial.print(gps.charsProcessed());
  Serial.print(" bad=");              Serial.print(gps.failedChecksum());
  Serial.print(" fixsent=");          Serial.print(gps.sentencesWithFix());
  Serial.print(" sat=");              Serial.print(sat);
  Serial.print(" hdop=");             Serial.print(hdop, 1);
  Serial.print(" fixq=");             Serial.println(fixq);
  Serial.println(packet);

  /* The OLED is the whole point outdoors, where there is no laptop. Everything
   * needed to decide "wait longer" or "go back and fix the antenna" is here. */
  char l1[24], l2[24], l3[24], l4[24];
  snprintf(l1, sizeof(l1), "GPS TEST  #%lu", (unsigned long)seq);
  snprintf(l2, sizeof(l2), "sat %d  hdop %.1f", sat, hdop);
  snprintf(l3, sizeof(l3), "fixq %d  chars %lu", fixq, (unsigned long)gps.charsProcessed());
  if (havePos) snprintf(l4, sizeof(l4), "%.5f", lat);
  else         snprintf(l4, sizeof(l4), "NO FIX");

  oled.clearBuffer();
  oled.drawStr(0, 12, l1);
  oled.drawStr(0, 26, l2);
  oled.drawStr(0, 40, l3);
  oled.drawStr(0, 54, l4);
  oled.sendBuffer();
}
