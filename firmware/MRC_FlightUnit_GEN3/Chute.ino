/* ============================================================================
 *  Chute — the release mechanism.
 *
 *  Isolated in its own file because the mechanism is not yet chosen and this is
 *  the only place that has to change when it is.
 *
 *  ⚠ Nothing here can confirm the parachute opened. There is no feedback sensor,
 *  so the chute counter in the packet means "commanded", never "deployed". The
 *  ground station and the dashboard must not claim otherwise.
 * ========================================================================= */

#if CHUTE_USE_SERVO
#include <ESP32Servo.h>
static Servo chuteServo;
#endif

static bool chuteFired = false;

void chuteBegin() {
#if CHUTE_USE_SERVO
  chuteServo.attach(CHUTE_PIN);
  chuteServo.write(CHUTE_SERVO_ARMED_DEG);
#else
  pinMode(CHUTE_PIN, OUTPUT);
  digitalWrite(CHUTE_PIN, LOW);
#endif
  chuteFired = false;
}

/* Idempotent. The ground station retries until it sees the count rise, so this
 * will be called again on repeats — driving an already-released mechanism must
 * be harmless, and re-sweeping a servo every second would only draw current and
 * chatter the horn. */
void chuteFire() {
  if (chuteFired) return;
  chuteFired = true;

#if CHUTE_USE_SERVO
  chuteServo.write(CHUTE_SERVO_RELEASE_DEG);
  /* CHUTE_HOLD_MS is spent inside the cycle's slack, after the listen window and
   * before the transmit. At the default 400 ms window there is room; if the
   * servo needs longer, shorten LISTEN_WINDOW_MS rather than the period. */
  delay(CHUTE_HOLD_MS);
#else
  digitalWrite(CHUTE_PIN, HIGH);
#endif

  Serial.println("[FLT] CHUTE RELEASE COMMANDED");
}

bool chuteIsFired() {
  return chuteFired;
}
