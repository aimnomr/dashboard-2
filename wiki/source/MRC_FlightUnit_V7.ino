/*
 * ============================================================
 *  MRC CanSat - FLIGHT UNIT  (Heltec Wi-Fi LoRa 32 V4)
 *
 *  FIX from V6:
 *   - Dropped radio.receive() entirely — timeout param behavior
 *     varies between RadioLib versions and was blocking forever.
 *   - New RX approach: startReceive() → poll LORA_BUSY + DIO1
 *     in a millis() timed loop → readData() if packet found
 *     → standby() to cancel if timeout expires.
 *     This works correctly on ALL RadioLib versions.
 *   - RX window = 800ms, TX after, ~1s total cycle.
 * ============================================================
 */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>

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
#define CHUTE_PIN 47

#define FREQ      919.0
#define BW        125.0
#define SF        7
#define CR        5
#define SYNC_WORD 0xAB
#define TX_POWER  17

#define RX_WINDOW_MS 800   // how long to listen each cycle

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
SPIClass loraSPI(HSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

struct Phase { const char* name; int dur; };
const Phase PHASES[] = {
  {"PRE_LAUNCH",   8},
  {"BOOST",        5},
  {"COAST",        8},
  {"APOGEE",       3},
  {"DESCENT_FREE", 12},
  {"DESCENT_CHUTE",30},
  {"LANDING",      4},
  {"POST_LAND",    8},
};
const int N_PHASES = 8;

const float BASE_LAT = 3.07830;
const float BASE_LNG = 101.71220;

bool chute_deployed = false;
int  phase_idx      = 0;
int  t_in_phase     = 0;
int  global_t       = 0;

float fnoise(float scale) {
  return ((float)(random(20000) - 10000) / 10000.0f) * scale;
}
float fclamp(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
float getAlt(const char* p, int t) {
  if      (!strcmp(p,"PRE_LAUNCH"))    return 0.0f;
  else if (!strcmp(p,"BOOST"))         return 80.0f*(t/5.0f);
  else if (!strcmp(p,"COAST"))         { float f=t/8.0f; return 80.0f+70.0f*(1-(1-f)*(1-f)); }
  else if (!strcmp(p,"APOGEE"))        return 150.0f;
  else if (!strcmp(p,"DESCENT_FREE"))  return 150.0f-70.0f*(t/12.0f);
  else if (!strcmp(p,"DESCENT_CHUTE"))return fclamp(80.0f-78.0f*(t/30.0f),0,80);
  else if (!strcmp(p,"LANDING"))       return fclamp(2.0f-2.0f*(t/4.0f),0,2);
  return 0.0f;
}

String buildPacket(const char* phase, float alt) {
  float temp=fclamp(32.5f-alt*0.0065f+fnoise(0.15f),15,50);
  float hum =fclamp(78.0f+alt*0.01f+fnoise(0.4f),30,100);
  float pres=fclamp(1007.9f*expf(-alt/8500.0f)+fnoise(0.08f),900,1050);
  float altr=fclamp(alt+fnoise(0.12f),0,500);
  float ax,ay,az,gx,gy,gz;
  float t=(float)t_in_phase;
  if (!strcmp(phase,"PRE_LAUNCH")){
    ax=fnoise(0.02f);ay=fnoise(0.02f);az=1.0f+fnoise(0.01f);
    gx=fnoise(0.5f);gy=fnoise(0.5f);gz=fnoise(0.3f);
  } else if (!strcmp(phase,"BOOST")){
    ax=fnoise(0.3f);ay=fnoise(0.3f);az=6.5f+fnoise(0.8f);
    gx=fnoise(8.0f);gy=fnoise(8.0f);gz=fnoise(3.0f);
  } else if (!strcmp(phase,"COAST")){
    ax=fnoise(0.15f)+0.3f*sinf(t);ay=fnoise(0.15f)+0.3f*cosf(t);az=0.2f+fnoise(0.1f);
    gx=15.0f*sinf(t*0.8f)+fnoise(5);gy=15.0f*cosf(t*0.6f)+fnoise(5);gz=fnoise(10);
  } else if (!strcmp(phase,"APOGEE")){
    ax=fnoise(0.05f);ay=fnoise(0.05f);az=fnoise(0.08f);
    gx=fnoise(3);gy=fnoise(3);gz=fnoise(2);
  } else if (!strcmp(phase,"DESCENT_FREE")){
    ax=fnoise(0.2f)+0.5f*sinf(t*1.2f);ay=fnoise(0.2f)+0.5f*cosf(t*0.9f);az=-0.8f+fnoise(0.15f);
    gx=20.0f*sinf(t*1.5f)+fnoise(6);gy=20.0f*cosf(t*1.1f)+fnoise(6);gz=10.0f*sinf(t*0.7f)+fnoise(4);
  } else if (!strcmp(phase,"DESCENT_CHUTE")){
    ax=fnoise(0.06f);ay=fnoise(0.06f);az=0.85f+fnoise(0.04f);
    gx=fnoise(2);gy=fnoise(2);gz=fnoise(1.5f);
  } else if (!strcmp(phase,"LANDING")){
    float imp=(t_in_phase<1)?4.0f:1.0f;
    ax=fnoise(0.1f)*imp;ay=fnoise(0.1f)*imp;az=1.0f+imp*fnoise(0.5f);
    gx=fnoise(5)*imp;gy=fnoise(5)*imp;gz=fnoise(3)*imp;
  } else {
    ax=fnoise(0.01f);ay=fnoise(0.01f);az=1.0f+fnoise(0.005f);
    gx=fnoise(0.3f);gy=fnoise(0.3f);gz=fnoise(0.2f);
  }
  float lat=BASE_LAT+0.00045f*sinf(global_t*0.05f)+fnoise(0.000008f);
  float lng=BASE_LNG+0.00030f*cosf(global_t*0.04f)+fnoise(0.000008f);
  float spd;
  if      (!strcmp(phase,"BOOST"))         spd=180.0f+fnoise(15);
  else if (!strcmp(phase,"COAST"))         spd=fclamp(180.0f-t*20,0,250)+fnoise(8);
  else if (!strcmp(phase,"APOGEE"))        spd=fabsf(fnoise(2));
  else if (!strcmp(phase,"DESCENT_FREE"))  spd=40.0f+fnoise(5);
  else if (!strcmp(phase,"DESCENT_CHUTE"))spd=8.0f+fnoise(1.5f);
  else spd=fclamp(fabsf(fnoise(0.3f)),0,5);
  int sat=(!strcmp(phase,"PRE_LAUNCH")||!strcmp(phase,"BOOST"))?5:9;
  char buf[180];
  snprintf(buf,sizeof(buf),
    "%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.6f,%.6f,%.2f,%d,CHUTE:%d",
    temp,hum,pres,altr,
    fclamp(ax,-16,16),fclamp(ay,-16,16),fclamp(az,-16,16),
    fclamp(gx,-250,250),fclamp(gy,-250,250),fclamp(gz,-250,250),
    lat,lng,fclamp(spd,0,400),sat,chute_deployed?1:0
  );
  return String(buf);
}

void oledShow(const char* l1,const char* l2,const char* l3,const char* l4){
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  if(l1)oled.drawStr(0,10,l1);
  if(l2)oled.drawStr(0,24,l2);
  if(l3)oled.drawStr(0,38,l3);
  if(l4)oled.drawStr(0,52,l4);
  oled.sendBuffer();
}

// ── Listen for EJECT using startReceive() + millis() poll ────
// Returns true if "EJECT" was received within windowMs
bool listenForEject(unsigned long windowMs) {
  // Put radio into receive mode (non-blocking)
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[RX] startReceive failed: %d\n", state);
    return false;
  }

  Serial.printf("[RX] Listening %lums...\n", windowMs);
  unsigned long t0 = millis();

  while (millis() - t0 < windowMs) {
    // DIO1 goes HIGH when a packet is fully received
    if (digitalRead(LORA_DIO1) == HIGH) {
      String incoming = "";
      int rxState = radio.readData(incoming);

      if (rxState == RADIOLIB_ERR_NONE) {
        incoming.trim();
        Serial.printf("[RX] Got: '%s'\n", incoming.c_str());
        radio.standby();
        if (incoming.equals("EJECT")) {
          return true;
        }
        // Some other packet — restart receive for rest of window
        unsigned long remaining = windowMs - (millis() - t0);
        if (remaining > 50) {
          radio.startReceive();
        }
      } else {
        Serial.printf("[RX] readData error: %d — restarting\n", rxState);
        radio.standby();
        delay(10);
        radio.startReceive();
      }
    }
    delay(5);
  }

  // Timeout — cancel receive mode cleanly
  radio.standby();
  Serial.println("[RX] Window closed - no command received");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] MRC Flight Unit V4 - V7 firmware");

  pinMode(VEXT_PIN,OUTPUT); digitalWrite(VEXT_PIN,LOW); delay(100);
  pinMode(OLED_RST,OUTPUT); digitalWrite(OLED_RST,LOW); delay(20);
  digitalWrite(OLED_RST,HIGH); delay(20);
  Wire.begin(OLED_SDA,OLED_SCL);
  oled.begin();
  oledShow("MRC FLIGHT UNIT","V4 / FW V7","Initialising...",NULL);

  pinMode(CHUTE_PIN,OUTPUT);
  digitalWrite(CHUTE_PIN,LOW);
  randomSeed(esp_random());

  loraSPI.begin(LORA_SCK,LORA_MISO,LORA_MOSI,LORA_NSS);

  Serial.println("[LORA] Initialising...");
  int state = radio.begin(FREQ,BW,SF,CR,SYNC_WORD,TX_POWER);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LORA] FAILED: %d\n",state);
    oledShow("FLIGHT UNIT","LoRa FAILED!","Check wiring",NULL);
    while(true) delay(1000);
  }
  radio.setDio1Action(NULL);

  Serial.printf("[LORA] OK - 919MHz SF7 BW125 RX_WINDOW=%dms\n", RX_WINDOW_MS);
  oledShow("MRC FLIGHT UNIT","LoRa OK - V7","LISTEN → TX cycle","Starting 2s...");
  delay(2000);
  Serial.println("[FLIGHT] Started. Cycle: listenForEject() then TX.");
}

void loop() {
  if (phase_idx >= N_PHASES) {
    Serial.println("[DONE] Flight complete.");
    oledShow("FLIGHT COMPLETE","Reset to restart",NULL,NULL);
    while(true) delay(5000);
  }

  const char* phase = PHASES[phase_idx].name;
  float alt = fclamp(getAlt(phase,t_in_phase)+fnoise(0.08f),0,500);

  Serial.printf("[CYCLE] #%d | %-15s | alt=%.1fm | chute=%d\n",
    global_t+1, phase, alt, chute_deployed?1:0);

  // ── STEP 1: LISTEN (800ms) ───────────────────────────────
  if (!chute_deployed) {
    bool ejected = listenForEject(RX_WINDOW_MS);
    if (ejected) {
      chute_deployed = true;
      digitalWrite(CHUTE_PIN, HIGH);
      Serial.println("[!!!] PARACHUTE DEPLOYED!");
      oledShow("!! EJECT FIRED !!","CHUTE DEPLOYED","GPIO47 HIGH",NULL);
      delay(500);
    }
  } else {
    delay(RX_WINDOW_MS);  // keep cycle time consistent
  }

  // ── STEP 2: TX telemetry ─────────────────────────────────
  // radio is in standby after listenForEject() returns — safe to TX
  String packet = buildPacket(phase, alt);
  Serial.println("[TX] Sending telemetry...");
  int txState = radio.transmit(packet);
  if (txState == RADIOLIB_ERR_NONE) {
    Serial.println("[TX] OK");
  } else {
    Serial.printf("[TX] Error: %d\n", txState);
  }

  // ── OLED + phase advance ──────────────────────────────────
  char l1[32],l2[32],l3[32],l4[32];
  snprintf(l1,sizeof(l1),"Phase: %s",phase);
  snprintf(l2,sizeof(l2),"Alt:%.1fm T:%ds",alt,global_t);
  snprintf(l3,sizeof(l3),"Pkt #%d",global_t+1);
  snprintf(l4,sizeof(l4),chute_deployed?"** CHUTE DEPLOYED **":"Chute: ARMED");
  oledShow(l1,l2,l3,l4);

  t_in_phase++;
  global_t++;
  if (t_in_phase >= PHASES[phase_idx].dur) {
    t_in_phase = 0;
    phase_idx++;
    if (phase_idx < N_PHASES)
      Serial.printf("\n[PHASE] >>> %s <<<\n\n",PHASES[phase_idx].name);
  }
}
