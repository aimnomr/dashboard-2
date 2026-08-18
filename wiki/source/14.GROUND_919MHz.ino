#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>

// =====================
// PIN DEFINITIONS
// Heltec WiFi LoRa 32 V3 pins — same SX1262 layout as CanSat
// NOTE: If you're on V4, double-check your pinout diagram!
// V4 reportedly keeps the same LoRa pins but verify before flashing.
// =====================
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
#define Vext       36

// =====================
// OBJECTS
// =====================
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

// =====================
// STATE
// =====================
int   packetCount = 0;
bool  loraReady   = false;

// =====================
// HELPER
// =====================
void VextON() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
}

// =====================
// DISPLAY: Waiting screen
// =====================
void showWaiting() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "-- MEeT GCS --");
  u8g2.drawLine(0, 14, 128, 14);
  u8g2.drawStr(0, 32, "919 MHz Ready");
  u8g2.drawStr(0, 48, "Waiting for TX...");

  char pktStr[20];
  sprintf(pktStr, "Pkts: %d", packetCount);
  u8g2.drawStr(0, 62, pktStr);
  u8g2.sendBuffer();
}

// =====================
// DISPLAY: Telemetry screen
// Parses the first 4 fields (temp, hum, pres, alt) + shows link quality
// =====================
void showTelemetry(const String& payload, float rssi, float snr) {
  float temp = 0, hum = 0, pres = 0, alt = 0;

  // Quick parse — only need first 4 fields for display
  sscanf(payload.c_str(), "%f,%f,%f,%f", &temp, &hum, &pres, &alt);

  char line1[25], line2[25], line3[25], line4[25];
  sprintf(line1, "T:%.1fC  H:%.0f%%",  temp, hum);
  sprintf(line2, "P:%.1fhPa A:%.1fm",  pres, alt);
  sprintf(line3, "RSSI:%.0f SNR:%.1f", rssi, snr);
  sprintf(line4, "Pkts: %d",            packetCount);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "-- MEeT GCS --");
  u8g2.drawLine(0, 14, 128, 14);
  u8g2.drawStr(0, 26, line1);
  u8g2.drawStr(0, 38, line2);
  u8g2.drawStr(0, 50, line3);
  u8g2.drawStr(0, 62, line4);
  u8g2.sendBuffer();
}

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(115200);
  VextON();
  delay(100);
  u8g2.begin();

  // Splash
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "MEeT CanSat");
  u8g2.drawStr(0, 40, "Ground Station");
  u8g2.drawStr(0, 60, "Initializing...");
  u8g2.sendBuffer();
  delay(1500);

  // LoRa init — must MATCH CanSat exactly
  // Freq: 919.0 MHz | BW: 125 | SF: 7 | CR: 5 | Sync: 0xAB
  int state = radio.begin(919.0, 125.0, 7, 5, 0xAB, 17);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[GCS] LoRa OK @ 919 MHz");
    loraReady = true;

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 20, "LoRa Ready!");
    u8g2.drawStr(0, 40, "Freq: 919 MHz");
    u8g2.drawStr(0, 60, "Waiting for TX...");
    u8g2.sendBuffer();
    delay(1500);

    radio.startReceive();
  } else {
    Serial.print("[GCS] LoRa FAILED, code: ");
    Serial.println(state);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "LoRa FAILED!");
    u8g2.drawStr(0, 35, "Check wiring");
    u8g2.drawStr(0, 55, "Halted.");
    u8g2.sendBuffer();
    while (1) delay(1000);
  }
}

// =====================
// LOOP
// =====================
void loop() {
  if (!loraReady) return;

  String received = "";
  int state = radio.receive(received);  // blocks until packet or timeout

  if (state == RADIOLIB_ERR_NONE) {
    float rssi = radio.getRSSI();
    float snr  = radio.getSNR();
    packetCount++;

    char output[160];
    snprintf(output, sizeof(output), "%s,%.1f,%.2f",
             received.c_str(), rssi, snr);
    Serial.println(output);
    showTelemetry(received, rssi, snr);

  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println("[GCS] Timeout - no packet");
    showWaiting();

  } else {
    Serial.print("[GCS] RX error code: ");
    Serial.println(state);
    showWaiting();
  }
}