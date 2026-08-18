/* ============================================================================
 *  GPS RELAY — FLIGHT SIDE.  Diagnostic sketch, NOT flight code.
 *
 *  Pairs with GPS_Relay_Ground. Put this on the CanSat, take it outside under
 *  clear sky, and leave the laptop indoors with the ground unit.
 *
 *  Radio settings are identical to the GEN3 flight firmware, so the pair works
 *  with no reconfiguration.
 *
 *  ---------------------------------------------------------------------------
 *  Why not relay ALL the NMEA?
 *
 *  A NEO-6M emits roughly 400-600 bytes every second (GGA, GLL, GSA, GSV, RMC,
 *  VTG). At SF7/BW125 a 100-byte packet is ~180 ms of airtime, so the channel
 *  tops out near 500 bytes/second at 100% duty cycle. Forwarding everything is
 *  not possible and would jam the band while trying.
 *
 *  So each second sends two things:
 *    1. A DIGEST, which answers the diagnostic questions directly.
 *    2. ONE raw sentence, rotating GGA -> GSV -> RMC -> GSA, so all of it is
 *       visible across four seconds.
 *  ---------------------------------------------------------------------------
 *
 *  Reading the digest:
 *
 *    chars=0                  Module is not reaching the ESP32 at all.
 *                             TX/RX not crossed, wrong baud, or no power.
 *    chars>0, bad>0           Data arriving corrupted. Baud mismatch or a
 *                             missing common ground.
 *    chars>0, inview=0        Wiring is fine. The antenna sees nothing —
 *                             check the connector, or you are still indoors.
 *    inview>0, used=0         Antenna works, satellites are visible, and it is
 *                             still acquiring. Wait: a cold start can take
 *                             15 minutes. This is the encouraging case.
 *    used>=4, fix=1           Fix acquired.
 * ========================================================================= */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

/* ---- must match GPS_Relay_Ground and the GEN3 firmware -------------------- */
#define FREQ_MHZ      919.0
#define BANDWIDTH_KHZ 125.0
#define SPREADING     7
#define CODING_RATE   5
#define SYNC_WORD     0xAB
#define TX_POWER_DBM  17

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

#define GPS_RX_PIN 20        /* ESP32 receives here  <- module TX */
#define GPS_TX_PIN 19        /* ESP32 transmits here -> module RX */
#define GPS_BAUD   9600

#define PERIOD_MS  1000

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
HardwareSerial GPSSerial(1);
TinyGPSPlus    gps;

/* Satellites in view and fix quality are not in TinyGPSPlus's core API, but they
 * are the two fields that matter most here: "in view" proves the antenna works
 * even when there is no fix yet. NEO-6M is GPS-only, so the GP prefix applies. */
TinyGPSCustom satsInView(gps, "GPGSV", 3);
TinyGPSCustom fixQuality(gps, "GPGGA", 6);
TinyGPSCustom hdopField(gps,  "GPGGA", 8);

/* Rotating capture of one raw sentence per type. */
const char *WANTED[] = { "GGA", "GSV", "RMC", "GSA" };
const int   N_WANTED = 4;
int   wantedIndex = 0;

char  lineBuf[100];
int   lineLen = 0;
char  captured[100];
bool  haveCaptured = false;

uint32_t seq = 0;
uint32_t nextAt = 0;

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
    while (true) delay(1000);
  }

  Serial.println("[GPSREL] flight side up - relaying to ground unit");
  nextAt = millis();
}

/* Collect complete NMEA lines; keep the newest one of the type we currently want. */
void pumpGps() {
  while (GPSSerial.available() > 0) {
    char c = (char)GPSSerial.read();
    gps.encode(c);

    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 6 && strstr(lineBuf, WANTED[wantedIndex]) != NULL) {
        strncpy(captured, lineBuf, sizeof(captured) - 1);
        captured[sizeof(captured) - 1] = '\0';
        haveCaptured = true;
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
  }
}

void loop() {
  pumpGps();

  if ((int32_t)(nextAt - millis()) > 0) { delay(2); return; }
  nextAt += PERIOD_MS;
  seq++;

  uint32_t chars = gps.charsProcessed();
  uint32_t good  = gps.passedChecksum();
  uint32_t bad   = gps.failedChecksum();
  int inview = satsInView.isValid() ? atoi(satsInView.value()) : 0;
  int used   = gps.satellites.isValid() ? gps.satellites.value() : 0;
  int fixq   = fixQuality.isValid() ? atoi(fixQuality.value()) : 0;

  /* ---- 1. digest ---- */
  char digest[120];
  snprintf(digest, sizeof(digest),
           "$GPSD,%lu,chars=%lu,ok=%lu,bad=%lu,inview=%d,used=%d,fix=%d,hdop=%s",
           (unsigned long)seq, (unsigned long)chars, (unsigned long)good,
           (unsigned long)bad, inview, used, fixq,
           hdopField.isValid() ? hdopField.value() : "-");
  radio.transmit(digest);
  Serial.println(digest);

  /* ---- 2. one raw sentence, rotating type ---- */
  if (haveCaptured) {
    radio.transmit(captured);
    Serial.println(captured);
    haveCaptured = false;
  } else {
    char none[40];
    snprintf(none, sizeof(none), "$GPSD,%lu,no %s sentence seen",
             (unsigned long)seq, WANTED[wantedIndex]);
    radio.transmit(none);
    Serial.println(none);
  }
  wantedIndex = (wantedIndex + 1) % N_WANTED;

  /* ---- 3. same story on the glass, since you are stood next to it ---- */
  char l1[24], l2[24], l3[24], l4[24];
  snprintf(l1, sizeof(l1), "GPS RELAY  #%lu", (unsigned long)seq);
  if (chars == 0) {
    snprintf(l2, sizeof(l2), "NO DATA FROM GPS");
    snprintf(l3, sizeof(l3), "check TX/RX + baud");
    snprintf(l4, sizeof(l4), "");
  } else {
    snprintf(l2, sizeof(l2), "chars%6lu bad%3lu", (unsigned long)chars,
             (unsigned long)bad);
    snprintf(l3, sizeof(l3), "in view %2d  used %2d", inview, used);
    snprintf(l4, sizeof(l4), "fix %d  %s", fixq,
             fixq > 0 ? "LOCKED" : (inview > 0 ? "acquiring" : "no sats"));
  }
  oled.clearBuffer();
  oled.drawStr(0, 10, l1);
  oled.drawLine(0, 13, 128, 13);
  oled.drawStr(0, 28, l2);
  oled.drawStr(0, 42, l3);
  oled.drawStr(0, 56, l4);
  oled.sendBuffer();
}
