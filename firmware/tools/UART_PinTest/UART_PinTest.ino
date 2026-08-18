/* ============================================================================
 *  UART PIN TEST — is a pin pair usable as a serial port at all?
 *
 *  Diagnostic sketch. NOT flight code. No LoRa, no GPS — just the two pins.
 *
 *  ---------------------------------------------------------------------------
 *  WHY THIS EXISTS
 *
 *  The GPS relay reported chars=0: not corrupted data, not garbage, but zero
 *  bytes ever arriving. A wrong baud rate still yields garbage, because the UART
 *  samples edges and emits nonsense. Zero means nothing is reaching the pin.
 *
 *  On the ESP32-S3, GPIO19 and GPIO20 are the native USB D- and D+ lines. With
 *  "USB CDC On Boot" enabled in the Arduino board menu, the USB peripheral takes
 *  both and they stop working as a UART — silently, and looking exactly like a
 *  dead GPS module.
 *
 *  This sketch tells you which it is, without involving the GPS at all.
 *  ---------------------------------------------------------------------------
 *
 *  HOW TO USE
 *
 *  1. Disconnect the GPS module from these pins.
 *  2. Put a jumper wire directly between TEST_TX_PIN and TEST_RX_PIN.
 *  3. Flash, open Serial Monitor at 115200.
 *
 *      LOOPBACK OK   -> the pins work. The fault is the GPS module, its power,
 *                       or the wiring between them.
 *      LOOPBACK FAIL -> the pins are NOT usable as a UART. On GPIO19/20 this
 *                       almost certainly means USB CDC has claimed them:
 *                       Tools > USB CDC On Boot > Disabled, reflash, retry.
 *                       If it still fails, move the GPS to different pins.
 *
 *  4. Change TEST_RX_PIN / TEST_TX_PIN to try a candidate pair before
 *     committing to rewiring.
 * ========================================================================= */

#include <HardwareSerial.h>

/* The pair currently used for the GPS. Change these to test alternatives. */
#define TEST_RX_PIN   20
#define TEST_TX_PIN   19

#define TEST_BAUD     9600
#define VEXT_PIN      36

HardwareSerial TestSerial(1);

const char *PROBE = "PINTEST";
uint32_t attempts = 0, passes = 0;

void setup() {
  Serial.begin(115200);
  delay(600);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(150);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" UART pin test");
  Serial.print(" RX pin ");  Serial.print(TEST_RX_PIN);
  Serial.print(", TX pin "); Serial.print(TEST_TX_PIN);
  Serial.print(", ");        Serial.print(TEST_BAUD);
  Serial.println(" baud");
  Serial.println();
  Serial.println(" Put a jumper between those two pins.");
  Serial.println(" GPS module should be DISCONNECTED.");
  Serial.println("=========================================");

  TestSerial.begin(TEST_BAUD, SERIAL_8N1, TEST_RX_PIN, TEST_TX_PIN);
  delay(100);
}

void loop() {
  attempts++;

  /* Flush anything stale so a pass cannot be a leftover from last round. */
  while (TestSerial.available() > 0) TestSerial.read();

  TestSerial.print(PROBE);
  TestSerial.print('\n');
  TestSerial.flush();

  char got[32];
  int  n = 0;
  uint32_t deadline = millis() + 300;
  while (millis() < deadline && n < (int)sizeof(got) - 1) {
    if (TestSerial.available() > 0) {
      char c = (char)TestSerial.read();
      if (c == '\n') break;
      got[n++] = c;
    }
  }
  got[n] = '\0';

  bool ok = (n > 0) && (strcmp(got, PROBE) == 0);
  if (ok) passes++;

  Serial.print("attempt ");
  Serial.print(attempts);
  Serial.print(": received ");
  Serial.print(n);
  Serial.print(" byte(s) ");

  if (ok) {
    Serial.println("-> LOOPBACK OK");
  } else if (n == 0) {
    Serial.println("-> LOOPBACK FAIL (nothing came back)");
  } else {
    Serial.print("-> LOOPBACK GARBLED: '");
    Serial.print(got);
    Serial.println("'");
  }

  if (attempts == 5) {
    Serial.println();
    Serial.println("-----------------------------------------");
    if (passes >= 4) {
      Serial.println(" VERDICT: these pins WORK as a UART.");
      Serial.println(" The fault is the GPS module, its power,");
      Serial.println(" or the wiring between module and board.");
      Serial.println(" Check: module TX -> board RX pin,");
      Serial.println("        module RX -> board TX pin,");
      Serial.println("        VCC 3V3 present, GND common.");
    } else {
      Serial.println(" VERDICT: these pins DO NOT work as a UART.");
      Serial.println(" On GPIO19/20 of an ESP32-S3 these are the");
      Serial.println(" native USB D-/D+ lines.");
      Serial.println(" Try: Tools > USB CDC On Boot > Disabled,");
      Serial.println("      reflash, and run this again.");
      Serial.println(" If it still fails, move the GPS to a");
      Serial.println(" different pin pair and retest here first.");
    }
    Serial.println("-----------------------------------------");
    Serial.println();
  }

  delay(1000);
}
