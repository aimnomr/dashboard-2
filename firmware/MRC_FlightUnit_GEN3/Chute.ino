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
  /* ⚠ This delay happens once, on the cycle the command arrives, and it WILL
   * overrun that cycle: 1000 ms of hold on top of ~650 ms of work. The scheduler
   * reports the overrun and resynchronises rather than firing a catch-up burst,
   * so the cost is one late packet at the moment of deployment.
   *
   * Judged acceptable — the release is more important than that packet — but it
   * is a knowing violation of the 1 Hz rule, not an oversight. If it matters,
   * drive the servo without blocking and let it travel across the next cycle. */
  delay(CHUTE_HOLD_MS);
#else
  digitalWrite(CHUTE_PIN, HIGH);
#endif

  Serial.println("[FLT] CHUTE RELEASE COMMANDED");
}

bool chuteIsFired() {
  return chuteFired;
}
