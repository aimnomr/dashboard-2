/* ============================================================================
 *  ServoEjectTest — bench the release servo, with a button instead of a radio.
 *
 *  NOT FLIGHT CODE. This exists to answer one question — does the mechanism
 *  actually throw at these two angles — without flashing the flight firmware,
 *  standing up a ground station, or putting a live uplink near a live servo.
 *
 *  The angles below are the measured ones from the working bench rig and are
 *  the SAME PAIR now compiled into both flight generations:
 *
 *      firmware/MRC_FlightUnit_GEN3/Config.h   CHUTE_SERVO_ARMED_DEG / _RELEASE_DEG
 *      firmware/MRC_FlightUnit_GEN4/Config.h   ditto
 *
 *  If you retune them here, change them in all three places or the bench will
 *  stop predicting the vehicle. That is the entire reason this file records
 *  where its own numbers live.
 *
 *  ⚠ This sketch cannot confirm the parachute opened, and neither can the
 *  flight firmware. There is no feedback sensor anywhere in this system. A
 *  servo that reaches RELEASE_DEG proves the horn moved, nothing further.
 * ========================================================================= */

#include <ESP32Servo.h>

Servo myServo;

/* ---- PINS -----------------------------------------------------------------
 * These are for the BENCH BOARD, which is a classic ESP32 — not the flight
 * unit. The Heltec V3 is an ESP32-S3 and has no GPIO 22-25 at all (its map
 * runs 0-21, then 26-48), so `buttonPin` below could not even exist there.
 *
 * Do not copy these numbers into Config.h. CHUTE_PIN is a separate decision on
 * a different chip.
 *
 * On a classic ESP32, avoid GPIO 34-39 for the servo: they are input-only, and
 * attach() will succeed while the horn never moves.
 */
const int servoPin  = 18;
const int buttonPin = 25;   /* to GND; INPUT_PULLUP means LOW == pressed */

/* ---- ANGLES ---------------------------------------------------------------
 * ARMED is where the horn sits for the whole flight. RELEASE is the throw.
 */
const int ARMED_DEG   = 90;
const int RELEASE_DEG = 160;

/* Time to let the horn travel before reading anything into the result. Matches
 * CHUTE_HOLD_MS in both Config.h files. The flight firmware BLOCKS for this on
 * the cycle the command lands and overruns its 1 Hz budget doing it — a knowing
 * trade recorded in Chute.ino. Here it costs nothing. */
const int HOLD_MS   = 1000;
const int RETURN_MS = 2000;   /* bench only: flight never returns to ARMED */

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(buttonPin, INPUT_PULLUP);
  myServo.attach(servoPin);
  myServo.write(ARMED_DEG);

  Serial.println("[BENCH] ServoEjectTest ready");
  Serial.print("[BENCH] servo GPIO ");   Serial.println(servoPin);
  Serial.print("[BENCH] armed ");        Serial.print(ARMED_DEG);
  Serial.print(" deg, release ");        Serial.print(RELEASE_DEG);
  Serial.println(" deg");
  Serial.println("[BENCH] press the button to throw");
}

void loop() {
  if (digitalRead(buttonPin) == LOW) {
    Serial.println("[BENCH] RELEASE");
    myServo.write(RELEASE_DEG);
    delay(HOLD_MS);

    delay(RETURN_MS - HOLD_MS);
    myServo.write(ARMED_DEG);
    Serial.println("[BENCH] returned to armed");

    /* Crude debounce: without it a held button re-throws immediately and the
     * horn chatters between the two positions. */
    while (digitalRead(buttonPin) == LOW) delay(10);
  }
}
