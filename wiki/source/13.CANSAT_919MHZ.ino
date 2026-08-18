#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

// =====================
// PIN DEFINITIONS
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

#define I2C_SDA    1
#define I2C_SCL    2
#define MPU_ADDR   0x68

#define GPS_RX     20
#define GPS_TX     19

#define SD_CS      4
#define SD_SCK     5
#define SD_MOSI    6
#define SD_MISO    7

// =====================
// OBJECTS
// =====================
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
TwoWire I2C_SENS = TwoWire(1);
Adafruit_BME280 bme;
TinyGPSPlus gps;
HardwareSerial GPSSerial(1);
SPIClass sdSPI(HSPI);

// =====================
// SCREEN MANAGEMENT
// =====================
int currentScreen = 0;
unsigned long lastScreenSwitch = 0;
unsigned long lastTransmit     = 0;
#define SCREEN_INTERVAL   5000
#define TRANSMIT_INTERVAL 1000

// =====================
// CALIBRATION
// =====================
float gpsSpeedOffset = 0;
bool  gpsCalibrated  = false;

float gyroXoffset = 0, gyroYoffset = 0, gyroZoffset = 0;
float accelXoffset = 0, accelYoffset = 0;

float seaLevelPressure = 1013.25;
float baseAltitude     = -9999;

// =====================
// SD CARD
// =====================
bool   sdReady = false;
String logFile = "";

// Auto-increment session file: FLIGHT01.CSV, FLIGHT02.CSV, ...
String getNextFilename() {
  for (int i = 1; i <= 99; i++) {
    char name[16];
    sprintf(name, "/FLIGHT%02d.CSV", i);
    if (!SD.exists(name)) return String(name);
  }
  return "/FLIGHT99.CSV";
}

void initSD() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("[SD] Init FAILED!");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 20, "SD Card FAILED!");
    u8g2.drawStr(0, 40, "Continuing w/o SD");
    u8g2.sendBuffer();
    delay(2000);
    return;
  }

  logFile = getNextFilename();  // fixed: removed stray '='
  File f = SD.open(logFile, FILE_WRITE);
  if (f) {
    f.println("temp,hum,pres,alt,ax,ay,az,gx,gy,gz,lat,lng,spd,sat");
    f.close();
    Serial.print("[SD] Logging to: ");
    Serial.println(logFile);
    sdReady = true;
  } else {
    Serial.println("[SD] Failed to create log file!");
  }
}

void logToSD(const char* payload) {
  if (!sdReady) return;
  File f = SD.open(logFile, FILE_APPEND);
  if (f) {
    f.println(payload);
    f.close();
  } else {
    Serial.println("[SD] Write error!");
  }
}

// =====================
// HELPER FUNCTIONS
// =====================
void VextON() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
}

void mpuWrite(byte reg, byte data) {
  I2C_SENS.beginTransmission(MPU_ADDR);
  I2C_SENS.write(reg);
  I2C_SENS.write(data);
  I2C_SENS.endTransmission(true);
}

void mpuRead(byte reg, byte* buf, int len) {
  I2C_SENS.beginTransmission(MPU_ADDR);
  I2C_SENS.write(reg);
  I2C_SENS.endTransmission(false);
  I2C_SENS.requestFrom((uint16_t)MPU_ADDR, (uint8_t)len, true);
  for (int i = 0; i < len; i++) buf[i] = I2C_SENS.read();
}

void getRawReadings(float &ax, float &ay, float &az,
                    float &gx, float &gy, float &gz) {
  byte buf[14];
  mpuRead(0x3B, buf, 14);
  ax = ((int16_t)((buf[0]  << 8) | buf[1]))  / 4096.0;
  ay = ((int16_t)((buf[2]  << 8) | buf[3]))  / 4096.0;
  az = ((int16_t)((buf[4]  << 8) | buf[5]))  / 4096.0;
  gx = ((int16_t)((buf[8]  << 8) | buf[9]))  / 65.5;
  gy = ((int16_t)((buf[10] << 8) | buf[11])) / 65.5;
  gz = ((int16_t)((buf[12] << 8) | buf[13])) / 65.5;
}

// =====================
// MPU CALIBRATION
// =====================
void calibrateMPU() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, "MPU Calibrating");
  u8g2.drawStr(0, 35, "Keep sensor");
  u8g2.drawStr(0, 50, "FLAT & STILL!");
  u8g2.sendBuffer();

  int samples = 500;
  float sumGx = 0, sumGy = 0, sumGz = 0;
  float sumAx = 0, sumAy = 0;

  for (int i = 0; i < samples; i++) {
    float ax, ay, az, gx, gy, gz;
    getRawReadings(ax, ay, az, gx, gy, gz);
    sumGx += gx; sumGy += gy; sumGz += gz;
    sumAx += ax; sumAy += ay;

    if (i % 50 == 0) {
      char prog[20];
      sprintf(prog, "Progress: %d%%", (i * 100) / samples);
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(0, 15, "MPU Calibrating");
      u8g2.drawStr(0, 35, "Keep FLAT & STILL");
      u8g2.drawStr(0, 55, prog);
      u8g2.sendBuffer();
    }
    delay(5);
  }

  gyroXoffset  = sumGx / samples;
  gyroYoffset  = sumGy / samples;
  gyroZoffset  = sumGz / samples;
  accelXoffset = sumAx / samples;
  accelYoffset = sumAy / samples;

  u8g2.clearBuffer();
  u8g2.drawStr(0, 25, "MPU Calibration");
  u8g2.drawStr(0, 45, "Complete!");
  u8g2.sendBuffer();
  delay(1000);
}

// =====================
// LORA TRANSMIT + SD LOG
// =====================
void transmitData() {
  // BME280
  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float alt  = bme.readAltitude(seaLevelPressure);
  if (baseAltitude == -9999) baseAltitude = alt;
  alt = alt - baseAltitude;

  // MPU6050
  float ax, ay, az, gx, gy, gz;
  getRawReadings(ax, ay, az, gx, gy, gz);
  ax -= accelXoffset;
  ay -= accelYoffset;
  gx -= gyroXoffset;
  gy -= gyroYoffset;
  gz -= gyroZoffset;

  // GPS
  float lat = gps.location.isValid()  ? gps.location.lat()   : 0.0;
  float lng = gps.location.isValid()  ? gps.location.lng()   : 0.0;
  float spd = gps.speed.isValid()     ? max(0.0f, (float)(gps.speed.kmph() - gpsSpeedOffset)) : 0.0;
  int   sat = gps.satellites.isValid() ? gps.satellites.value() : 0;

  // Build 14-field CSV payload
  // Format: temp,hum,pres,alt,ax,ay,az,gx,gy,gz,lat,lng,spd,sat
  char payload[128];
  snprintf(payload, sizeof(payload),
    "%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.5f,%.5f,%.1f,%d",
    temp, hum, pres, alt,
    ax, ay, az,
    gx, gy, gz,
    lat, lng, spd, sat
  );

  // Transmit via LoRa
  int state = radio.transmit(payload);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("[TX] ");
    Serial.println(payload);
  } else {
    Serial.print("[TX] Failed, code: ");
    Serial.println(state);
  }

  // Log same payload to SD card
  logToSD(payload);
}

// =====================
// SCREEN FUNCTIONS
// =====================
void showBMEScreen() {
  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float alt  = bme.readAltitude(seaLevelPressure);
  if (baseAltitude != -9999) alt = alt - baseAltitude;

  char tempStr[25], humStr[25], presStr[25], altStr[25];
  sprintf(tempStr, "Temp : %.1f C",   temp);
  sprintf(humStr,  "Humi : %.1f %%",  hum);
  sprintf(presStr, "Pres : %.1f hPa", pres);
  sprintf(altStr,  "Alt  : %.1f m",   alt);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "-- BME280 --");
  u8g2.drawLine(0, 14, 128, 14);
  u8g2.drawStr(0, 26, tempStr);
  u8g2.drawStr(0, 38, humStr);
  u8g2.drawStr(0, 50, presStr);
  u8g2.drawStr(0, 62, altStr);
  u8g2.sendBuffer();
}

void showGPSScreen() {
  if (gps.speed.isValid() && !gpsCalibrated) {
    gpsSpeedOffset = gps.speed.kmph();
    gpsCalibrated  = true;
  }

  if (gps.location.isValid()) {
    float lat = gps.location.lat();
    float lng = gps.location.lng();
    float spd = max(0.0f, (float)(gps.speed.kmph() - gpsSpeedOffset));
    int   sat = gps.satellites.value();

    char latStr[25], lngStr[25], spdStr[25], satStr[25];
    sprintf(latStr, "Lat: %.5f",      lat);
    sprintf(lngStr, "Lng: %.5f",      lng);
    sprintf(spdStr, "Spd: %.1f km/h", spd);
    sprintf(satStr, "Sat: %d",        sat);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "-- GPS --");
    u8g2.drawLine(0, 14, 128, 14);
    u8g2.drawStr(0, 26, latStr);
    u8g2.drawStr(0, 38, lngStr);
    u8g2.drawStr(0, 50, spdStr);
    u8g2.drawStr(0, 62, satStr);
    u8g2.sendBuffer();
  } else {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "-- GPS --");
    u8g2.drawLine(0, 14, 128, 14);
    u8g2.drawStr(0, 35, "Searching...");
    u8g2.drawStr(0, 50, "Go near window!");
    u8g2.sendBuffer();
  }
}

void showMPUScreen() {
  float ax, ay, az, gx, gy, gz;
  getRawReadings(ax, ay, az, gx, gy, gz);
  ax -= accelXoffset; ay -= accelYoffset;
  gx -= gyroXoffset;  gy -= gyroYoffset; gz -= gyroZoffset;

  char accelStr1[25], accelStr2[25], gyroStr1[25], gyroStr2[25];
  sprintf(accelStr1, "Ax:%.2f Ay:%.2f", ax, ay);
  sprintf(accelStr2, "Az:%.2f g",        az);
  sprintf(gyroStr1,  "Gx:%.1f Gy:%.1f", gx, gy);
  sprintf(gyroStr2,  "Gz:%.1f d/s",      gz);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "-- MPU6050 --");
  u8g2.drawLine(0, 14, 128, 14);
  u8g2.drawStr(0, 26, accelStr1);
  u8g2.drawStr(0, 38, accelStr2);
  u8g2.drawStr(0, 50, gyroStr1);
  u8g2.drawStr(0, 62, gyroStr2);
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

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "CanSat Project");
  u8g2.drawStr(0, 40, "Initializing...");
  u8g2.sendBuffer();
  delay(1500);

  // ---- Shared I2C bus (BME280 + MPU6050) ----
  I2C_SENS.begin(I2C_SDA, I2C_SCL);
  delay(250);

  // ---- BME280 ----
  if (!bme.begin(0x76, &I2C_SENS)) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 15, "BME280 Error!");
    u8g2.drawStr(0, 35, "Check wiring");
    u8g2.sendBuffer();
    while (1) delay(1000);
  }
  Serial.println("BME280 Ready!");

  // ---- GPS ----
  GPSSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Ready!");

  // ---- MPU6050 ----
  mpuWrite(0x6B, 0x00);  // wake up
  delay(200);

  byte buf[1];
  mpuRead(0x75, buf, 1);
  if (buf[0] == 0x00 || buf[0] == 0xFF) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 15, "MPU6050 Error!");
    u8g2.drawStr(0, 35, "Check wiring");
    u8g2.sendBuffer();
    while (1) delay(1000);
  }
  mpuWrite(0x1C, 0x10);  // accel range ±8g
  mpuWrite(0x1B, 0x08);  // gyro range ±500 d/s
  Serial.println("MPU6050 Ready!");

  // ---- LoRa ----
  // 919.0 MHz — Malaysian ISM band
  // BW: 125 kHz | SF: 7 | CR: 5 | Sync: 0xAB | Power: 17 dBm
  int state = radio.begin(919.0, 125.0, 7, 5, 0xAB, 17);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa Ready!");
  } else {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 15, "LoRa Error!");
    u8g2.drawStr(0, 35, "Check wiring");
    u8g2.sendBuffer();
    while (1) delay(1000);
  }

  // ---- SD Card ----
  // Uses HSPI bus to avoid conflict with LoRa SPI
  initSD();

  // ---- MPU Calibration ----
  calibrateMPU();

  // ---- Ready ----
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "CanSat Ready!");
  u8g2.drawStr(0, 40, sdReady ? "SD: OK" : "SD: NOT FOUND");
  u8g2.sendBuffer();
  delay(1500);
}

// =====================
// LOOP
// =====================
void loop() {
  // Feed GPS parser
  while (GPSSerial.available() > 0) {
    gps.encode(GPSSerial.read());
  }

  // Transmit + log every 1 second
  if (millis() - lastTransmit >= TRANSMIT_INTERVAL) {
    lastTransmit = millis();
    transmitData();
  }

  // Rotate OLED screen every 5 seconds
  if (millis() - lastScreenSwitch >= SCREEN_INTERVAL) {
    lastScreenSwitch = millis();
    currentScreen = (currentScreen + 1) % 3;
  }

  switch (currentScreen) {
    case 0: showBMEScreen(); break;
    case 1: showGPSScreen(); break;
    case 2: showMPUScreen(); break;
  }

  delay(200);
}