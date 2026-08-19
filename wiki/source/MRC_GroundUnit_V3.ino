/*
 * ============================================================
 *  MRC CanSat - GROUND UNIT  (Heltec Wi-Fi LoRa 32 V3)
 *
 *  FIX from V2:
 *   - Ground unit now uses startReceive() + DIO1 polling
 *     instead of blocking radio.receive() so the serial
 *     port can be checked every 10ms loop iteration.
 *   - EJECT is transmitted immediately when serial command
 *     arrives — no more queuing until end of RX window.
 *   - Retransmit EJECT up to 5 times with 300ms gaps to
 *     maximise chance of landing in flight unit's RX window.
 *   - radio.standby() called before any TX, startReceive()
 *     called immediately after to resume listening.
 * ============================================================
 */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <SPI.h>

#define VEXT_PIN  36
#define OLED_SDA  17
#define OLED_SCL  18
#define OLED_RST  21
#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_DIO1 14

#define FREQ      919.0
#define BW        125.0
#define SF        7
#define CR        5
#define SYNC_WORD 0xAB
#define TX_POWER  17

// How many times to retransmit EJECT to maximise hit rate
#define EJECT_RETRIES     5
#define EJECT_RETRY_MS  300   // gap between retries (ms)

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
SPIClass loraSPI(HSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

bool  chuteConfirmed = false;
int   packetCount    = 0;
float lastRSSI       = 0;
float lastSNR        = 0;
String serialBuf     = "";

void oledShow(const char* l1,const char* l2,const char* l3,const char* l4){
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  if(l1)oled.drawStr(0,10,l1);
  if(l2)oled.drawStr(0,24,l2);
  if(l3)oled.drawStr(0,38,l3);
  if(l4)oled.drawStr(0,52,l4);
  oled.sendBuffer();
}

// ── Transmit EJECT with retries ───────────────────────────────
// Each attempt is 300ms apart = 5 x 300ms = 1.5s total window
// Flight unit listens 800ms per cycle so statistically at
// least one attempt should land within the RX window.
void fireEject() {
  Serial.println("[CMD] EJECT received from Node-RED — firing!");
  oledShow("GROUND UNIT","EJECT FIRING...","Transmitting x5",NULL);

  for (int i = 0; i < EJECT_RETRIES; i++) {
    radio.standby();
    delay(10);

    int state = radio.transmit("EJECT");
    if (state == RADIOLIB_ERR_NONE) {
      Serial.printf("[TX] EJECT attempt %d/%d OK\n", i+1, EJECT_RETRIES);
    } else {
      Serial.printf("[TX] EJECT attempt %d/%d failed: %d\n", i+1, EJECT_RETRIES, state);
    }

    // Resume receive between retries so we don't miss a
    // confirmation packet from flight unit
    radio.startReceive();
    delay(EJECT_RETRY_MS);
  }

  Serial.println("[CMD] EJECT sequence complete");
  oledShow("GROUND UNIT","EJECT SENT x5","Waiting confirm...",NULL);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] MRC Ground Unit V3");

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20);
  digitalWrite(OLED_RST, HIGH); delay(20);

  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();
  oledShow("MRC GROUND UNIT","V3 Board","Initialising...",NULL);

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  int state = radio.begin(FREQ, BW, SF, CR, SYNC_WORD, TX_POWER);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LORA] FAILED: %d\n", state);
    oledShow("GROUND UNIT","LoRa FAILED!","Check wiring",NULL);
    while(true) delay(1000);
  }
  radio.setDio1Action(NULL);

  Serial.println("[LORA] OK - listening...");
  oledShow("MRC GROUND UNIT","LoRa OK","919MHz SF7","Listening...");

  // Start continuous receive immediately
  radio.startReceive();
}

void loop() {

  // ── 1. Check serial for EJECT (highest priority) ─────────
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      serialBuf.trim();
      if (serialBuf.equalsIgnoreCase("EJECT")) {
        fireEject();           // transmit immediately, with retries
        radio.startReceive();  // back to RX after retries done
      }
      serialBuf = "";
    } else {
      serialBuf += c;
      if (serialBuf.length() > 32) serialBuf = "";
    }
  }

  // ── 2. Check DIO1 for incoming telemetry packet ───────────
  if (digitalRead(LORA_DIO1) == HIGH) {
    String received = "";
    int rxState = radio.readData(received);

    if (rxState == RADIOLIB_ERR_NONE) {
      received.trim();
      lastRSSI = radio.getRSSI();
      lastSNR  = radio.getSNR();
      packetCount++;

      // Parse chute flag
      int chuteVal = 0;
      int chuteSep = received.lastIndexOf(",CHUTE:");
      String sensorPart = received;
      if (chuteSep > 0) {
        chuteVal   = received.substring(chuteSep + 7).toInt();
        sensorPart = received.substring(0, chuteSep);
      }

      if (chuteVal == 1 && !chuteConfirmed) {
        chuteConfirmed = true;
        Serial.println("[INFO] Chute CONFIRMED by flight unit");
      }

      // Forward 17-field CSV to Node-RED
      Serial.print(sensorPart);
      Serial.print(",");
      Serial.print(lastRSSI, 1);
      Serial.print(",");
      Serial.print(lastSNR, 1);
      Serial.print(",");
      Serial.println(chuteVal);

      // OLED
      char l2[32],l3[32],l4[32];
      snprintf(l2,sizeof(l2),"Pkts: %d",packetCount);
      snprintf(l3,sizeof(l3),"RSSI:%.0fdBm SNR:%.1f",lastRSSI,lastSNR);
      snprintf(l4,sizeof(l4),chuteConfirmed?"CHUTE: DEPLOYED!":"Chute: armed");
      oledShow("MRC GROUND UNIT",l2,l3,l4);

    } else {
      Serial.printf("[RX] Error: %d\n", rxState);
    }

    // Resume receive after reading
    radio.startReceive();
  }

  delay(10);  // small yield, keeps serial responsive
}
