/* ============================================================================
 *  GPS MINIMAL — power isolation test. Diagnostic sketch, NOT flight code.
 *
 *  ---------------------------------------------------------------------------
 *  WHY THIS EXISTS
 *
 *  The GPS module stopped lighting up once the whole system was powered
 *  together. That points at power, not protocol — and an unpowered module
 *  explains chars=0 on its own, without any pin theory being involved.
 *
 *  This sketch starts NOTHING except the GPS UART. No LoRa, no SD card, no
 *  OLED, no I2C sensors. It is the minimum possible load on the 3.3 V rail.
 *
 *      GPS lights up and chars climb here, but not in the full firmware
 *          -> POWER BUDGET. The rail cannot carry everything at once. Note
 *             that Pins_Assignment.md already puts the SD card on external
 *             5 V, so this board has hit a supply limit before.
 *
 *      GPS still dark here
 *          -> Not a budget problem. Either it is wired to the switched Ve rail
 *             and that is not coming up, or the wiring/module is at fault.
 *             Try VEXT_ENABLED 0 and 1 below, and measure at the module.
 *  ---------------------------------------------------------------------------
 *
 *  THE ONE MEASUREMENT WORTH MAKING
 *
 *  Put a multimeter on the GPS module's VCC and GND while this runs:
 *
 *      ~3.3 V  -> the module has power. If it is still dark, check which LED
 *                 you are looking at: on many NEO-6M breakouts the only LED is
 *                 the PPS/fix indicator, which stays dark until a fix exists.
 *                 A dark PPS LED with no fix is NORMAL.
 *      ~0 V    -> no power reaching it. Wiring, or the Ve rail is off.
 *      sagging -> brownout under load.
 *
 *  ---------------------------------------------------------------------------
 *  WIRING
 *      GPS TX  -> pin 20   (ESP32 receives)
 *      GPS RX  -> pin 19   (ESP32 transmits)
 *      GPS VCC -> try the PERMANENT 3V3 pin, not Ve
 *      GPS GND -> GND
 * ========================================================================= */

#include <HardwareSerial.h>

/* Corrected 2026-08-19: these were swapped, which is what produced chars=0. */
#define GPS_RX_PIN     19
#define GPS_TX_PIN     20
#define GPS_BAUD       9600

/* Heltec V3 switches its external 3.3 V rail with this pin, active LOW.
 * Set VEXT_ENABLED to 0 to leave the pin alone entirely — useful if the GPS is
 * on the permanent 3V3 rail and you want to rule Vext out of the picture. */
#define VEXT_PIN       36
#define VEXT_ENABLED   1

HardwareSerial GPSSerial(1);

uint32_t total = 0;
uint32_t lastReport = 0;
uint32_t sinceReport = 0;
char line[128];
int  lineLen = 0;
bool sawSentence = false;

void setup() {
  Serial.begin(115200);
  delay(700);

  Serial.println();
  Serial.println("=============================================");
  Serial.println(" GPS MINIMAL - power isolation test");
  Serial.println(" Nothing else is initialised: no LoRa, no SD,");
  Serial.println(" no OLED, no I2C. Minimum load on the rail.");
#if VEXT_ENABLED
  Serial.println(" Vext: ENABLED (pin 36 driven LOW)");
#else
  Serial.println(" Vext: NOT TOUCHED");
#endif
  Serial.println(" Measure 3.3 V at the module's VCC pin now.");
  Serial.println("=============================================");
  Serial.println();

#if VEXT_ENABLED
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(300);        /* let the rail settle before the module is expected up */
#endif

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  lastReport = millis();
}

void loop() {
  while (GPSSerial.available() > 0) {
    char c = (char)GPSSerial.read();
    total++;
    sinceReport++;

    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = '\0';
      if (lineLen > 6) {
        Serial.println(line);
        sawSentence = true;
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < (int)sizeof(line) - 1) line[lineLen++] = c;
  }

  if (millis() - lastReport >= 5000) {
    Serial.print(">>> ");
    Serial.print(sinceReport);
    Serial.print(" chars in 5 s, ");
    Serial.print(total);
    Serial.print(" total");

    if (total == 0) {
      Serial.println("  -> STILL NOTHING.");
      Serial.println(">>> Not a power-budget problem: this sketch loads the rail");
      Serial.println(">>> less than anything else you can run. Measure VCC at the");
      Serial.println(">>> module. 0 V means wiring or the Ve rail; 3.3 V means the");
      Serial.println(">>> module or the TX/RX crossing.");
    } else if (!sawSentence) {
      Serial.println("  -> characters but no complete sentences.");
      Serial.println(">>> Wrong baud rate, or a missing common ground.");
    } else {
      Serial.println("  -> DATA IS FLOWING.");
      Serial.println(">>> The module and wiring are fine. If the full firmware");
      Serial.println(">>> shows chars=0, the difference is load on the 3.3 V rail.");
    }

    sinceReport = 0;
    lastReport = millis();
  }

  delay(2);
}
