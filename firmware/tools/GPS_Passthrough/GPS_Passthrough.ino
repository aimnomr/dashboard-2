/* ============================================================================
 *  GPS passthrough — diagnostic sketch. NOT flight code.
 *
 *  Dumps whatever the GPS module sends straight to the USB serial monitor, and
 *  tries several baud rates in turn. It answers one question:
 *
 *      Is the receiver talking to the ESP32 at all?
 *
 *  Open the Serial Monitor at 115200 and read the result:
 *
 *    NMEA lines ($GPGGA, $GPRMC, $GPGSV ...)
 *        Wiring and baud are correct. The problem is the antenna, the sky view,
 *        or a cold start. Look at the GGA fix-quality field: "$GPGGA,,,,,,0" —
 *        that 0 after the empty fields means no fix yet.
 *
 *    Nothing at all
 *        The module is not reaching the ESP32. Check TX/RX are crossed and that
 *        the module is powered — most NEO-6M boards blink an LED once per second
 *        ONLY after they achieve a fix, so a dark LED is normal while searching
 *        but a completely dead board is not.
 *
 *    Garbage characters
 *        Baud mismatch, or a missing common ground.
 *
 *  Wiring, matching the flight unit's Config.h:
 *      GPS module TX  ->  ESP32 pin 19      (the ESP32 RECEIVES here)
 *      GPS module RX  ->  ESP32 pin 20      (the ESP32 TRANSMITS here)
 *      GPS VCC -> 3V3,  GPS GND -> GND
 *
 *  Note the crossover. "GPS_RX 19" in Config.h means the ESP32's RX pin, which
 *  connects to the module's TX. Wiring TX-to-TX is the classic failure here and
 *  produces exactly the silence above — and it is what was actually wrong, found
 *  2026-08-19. The pin numbers above were themselves reversed until then.
 * ========================================================================= */

#include <HardwareSerial.h>

/* Corrected 2026-08-19: these were swapped, which is what produced chars=0. */
#define GPS_RX_PIN  19      /* ESP32 receives on this pin <- module TX */
#define GPS_TX_PIN  20      /* ESP32 transmits on this pin -> module RX */
#define VEXT_PIN    36

HardwareSerial GPSSerial(1);

/* NEO-6M ships at 9600. It is tried first, but a module reconfigured in u-center
 * and saved to flash will be silent at the wrong rate — hence the sweep. */
const uint32_t BAUDS[] = { 9600, 38400, 57600, 115200, 4800 };
const int  N_BAUDS = sizeof(BAUDS) / sizeof(BAUDS[0]);
const uint32_t TRY_MS = 6000;

int      baudIndex = 0;
uint32_t triedAt   = 0;
uint32_t charCount = 0;
uint32_t lineCount = 0;

void startBaud(int i) {
  GPSSerial.end();
  delay(50);
  GPSSerial.begin(BAUDS[i], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  charCount = 0;
  lineCount = 0;
  triedAt = millis();

  Serial.println();
  Serial.print("=== trying ");
  Serial.print(BAUDS[i]);
  Serial.println(" baud for 6 s ===");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);      /* peripherals on */
  delay(200);

  Serial.println();
  Serial.println("GPS passthrough diagnostic");
  Serial.println("Module TX -> pin 20, module RX -> pin 19, VCC 3V3, GND common");
  Serial.println("Take the unit OUTSIDE with a clear view of the sky.");

  startBaud(0);
}

void loop() {
  while (GPSSerial.available() > 0) {
    char c = (char)GPSSerial.read();
    Serial.write(c);
    charCount++;
    if (c == '\n') lineCount++;
  }

  if (millis() - triedAt >= TRY_MS) {
    Serial.println();
    Serial.print(">>> ");
    Serial.print(BAUDS[baudIndex]);
    Serial.print(" baud: ");
    Serial.print(charCount);
    Serial.print(" chars, ");
    Serial.print(lineCount);
    Serial.println(" lines");

    if (charCount > 0 && lineCount > 0) {
      Serial.println(">>> DATA FOUND at this baud rate.");
      Serial.println(">>> Wiring is correct. If there is still no fix, the cause");
      Serial.println(">>> is the antenna, the sky view, or a cold start.");
      Serial.println(">>> Leave it outside, stationary, for up to 15 minutes.");
    } else if (charCount > 0) {
      Serial.println(">>> Characters but no complete lines - wrong baud, or a");
      Serial.println(">>> missing common ground.");
    }

    baudIndex = (baudIndex + 1) % N_BAUDS;
    startBaud(baudIndex);
  }
}
