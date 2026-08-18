/* ============================================================================
 *  GPS RELAY — GROUND SIDE.  Diagnostic sketch, NOT flight code.
 *
 *  Pairs with GPS_Relay_Flight. Stays on the laptop. Receives whatever the
 *  CanSat sends and prints it to USB serial at 115200, so the CanSat can be
 *  outside under clear sky while you read the results indoors.
 *
 *  Radio settings are identical to the GEN3 firmware, so no reconfiguration is
 *  needed to swap between this and the real ground station.
 *
 *  Unlike the real ground station this forwards EVERYTHING — no team-marker
 *  filter, no checksum check. This is a diagnostic: seeing another team's
 *  traffic, or corruption, is information rather than noise.
 *
 *  ---------------------------------------------------------------------------
 *  What you will see, once per second:
 *
 *    $GPSD,12,chars=5820,ok=61,bad=0,inview=7,used=0,fix=0,hdop=-   [-64 dBm]
 *    $GPGGA,,,,,,0,00,99.99,,,,,,*48                                [-64 dBm]
 *
 *  Reading it:
 *
 *    chars=0                Module not reaching the ESP32 — TX/RX not crossed,
 *                           wrong baud, or no power. Nothing else matters yet.
 *    chars>0, bad>0         Data arriving corrupted — baud mismatch or missing
 *                           common ground.
 *    chars>0, inview=0      Wiring is fine, antenna sees nothing. Check the
 *                           connector, or you are still effectively indoors.
 *    inview>0, used=0       Antenna works and satellites are visible; still
 *                           acquiring. A cold start can take 15 minutes.
 *                           This is the encouraging case — just wait.
 *    used>=4, fix=1         Fix acquired.
 * ========================================================================= */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>

/* ---- must match GPS_Relay_Flight ----------------------------------------- */
#define FREQ_MHZ      919.0
#define BANDWIDTH_KHZ 125.0
#define SPREADING     7
#define CODING_RATE   5
#define SYNC_WORD     0xAB
#define TX_POWER_DBM  17

#define LORA_NSS   8
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_PIN   36

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

uint32_t received   = 0;
uint32_t lastRxMs   = 0;
float    lastRssi   = 0;
float    lastSnr    = 0;
char     lastDigest[40] = "waiting...";

void setup() {
  Serial.begin(115200);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(150);

  oled.begin();
  oled.setBusClock(400000);
  oled.setFont(u8g2_font_6x10_tf);
  oled.clearBuffer();
  oled.drawStr(0, 10, "GPS RELAY GROUND");
  oled.drawLine(0, 13, 128, 13);
  oled.drawStr(0, 30, "Listening...");
  oled.sendBuffer();

  int state = radio.begin(FREQ_MHZ, BANDWIDTH_KHZ, SPREADING,
                          CODING_RATE, SYNC_WORD, TX_POWER_DBM);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[GPSREL] LoRa failed, code ");
    Serial.println(state);
    oled.clearBuffer();
    oled.drawStr(0, 20, "LoRa FAILED");
    oled.sendBuffer();
    while (true) delay(1000);
  }

  Serial.println();
  Serial.println("[GPSREL] ground side up. Everything received is printed below.");
  Serial.println("[GPSREL] chars=0 -> wiring/baud.  inview=0 -> antenna/sky.");
  Serial.println("[GPSREL] inview>0 used=0 -> acquiring, be patient.");
  radio.startReceive();
}

void loop() {
  if (digitalRead(LORA_DIO1) == HIGH) {
    String in;
    int state = radio.readData(in);

    if (state == RADIOLIB_ERR_NONE) {
      lastRssi = radio.getRSSI();
      lastSnr  = radio.getSNR();
      in.trim();
      received++;
      lastRxMs = millis();

      Serial.print(in);
      Serial.print("   [");
      Serial.print(lastRssi, 0);
      Serial.print(" dBm  SNR ");
      Serial.print(lastSnr, 1);
      Serial.println("]");

      /* Keep the digest's summary for the screen. */
      if (in.startsWith("$GPSD,")) {
        int inview = in.indexOf("inview=");
        int used   = in.indexOf("used=");
        int fix    = in.indexOf("fix=");
        if (inview > 0 && used > 0 && fix > 0) {
          snprintf(lastDigest, sizeof(lastDigest), "v%s u%s f%s",
                   in.substring(inview + 7, in.indexOf(',', inview)).c_str(),
                   in.substring(used + 5, in.indexOf(',', used)).c_str(),
                   in.substring(fix + 4, in.indexOf(',', fix)).c_str());
        }
      }
    } else {
      Serial.print("[GPSREL] RX error ");
      Serial.println(state);
    }
    radio.startReceive();
  }

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw > 300) {
    lastDraw = millis();
    char l1[24], l2[24], l3[24];
    snprintf(l1, sizeof(l1), "RX %lu", (unsigned long)received);
    if (lastRxMs == 0) {
      snprintf(l2, sizeof(l2), "no packets yet");
      snprintf(l3, sizeof(l3), "");
    } else {
      snprintf(l2, sizeof(l2), "%lus ago  %4.0fdBm",
               (unsigned long)((millis() - lastRxMs) / 1000), lastRssi);
      snprintf(l3, sizeof(l3), "%s", lastDigest);
    }
    oled.clearBuffer();
    oled.drawStr(0, 10, "GPS RELAY GROUND");
    oled.drawLine(0, 13, 128, 13);
    oled.drawStr(0, 30, l1);
    oled.drawStr(0, 44, l2);
    oled.drawStr(0, 58, l3);
    oled.sendBuffer();
  }

  delay(5);
}
