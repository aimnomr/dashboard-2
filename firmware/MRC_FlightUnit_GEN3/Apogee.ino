/* ============================================================================
 *  Apogee — the highest altitude seen, and the descent trigger built on it.
 *
 *  Isolated from Chute.ino on the same principle that file states: the DECISION
 *  to release and the MECHANISM that releases are different problems. Chute.ino
 *  owns the pin and the servo; this file owns the question of when. Either can be
 *  replaced without touching the other.
 *
 *  The rule, in full:
 *
 *      1. Track the maximum of `alt` every cycle. Always — an apogee figure is
 *         worth having even on a flight that never arms.
 *      2. Arm once altitude passes AUTO_EJECT_ARM_ALT_M. Before that the rule is
 *         inert no matter what the barometer says.
 *      3. Armed: count CONSECUTIVE cycles where apogee - alt >= AUTO_EJECT_DROP_M.
 *         Any cycle that does not qualify resets the count to zero.
 *      4. At AUTO_EJECT_CONFIRM_N, fire once and never again.
 *
 *  ⚠ Altitude here is BAROMETRIC and relative to boot, which is what makes step 2
 *  load-bearing. It inherits every weakness of a pressure altimeter: a gust, a
 *  slipstream or a sealed-then-warmed payload bay all move it without the vehicle
 *  moving. The arming floor and the confirmation count are what stand between
 *  those and the parachute.
 *
 *  ⚠ This state does not survive a reset. A brownout at 100 m returns with apogee
 *  at zero and disarmed, and would need to climb 30 m again to re-arm — which on
 *  the way down it will not. The uplink is the backup to the backup, and that is
 *  the case it covers.
 * ========================================================================= */

static float   apogeeAlt      = 0.0f;   /* highest alt seen, m above boot */
static bool    apogeeArmed    = false;  /* has climbed past the arming floor */
static uint8_t descentCycles  = 0;      /* consecutive cycles over the drop threshold */
static bool    autoEjectFired = false;  /* this file's own one-shot latch */

/* Call after sensorsCalibrate(), never before: the altitude zero is set there, and
 * an apogee tracked against an unzeroed baseline is measuring the launch site's
 * elevation above sea level. */
void apogeeBegin() {
  apogeeAlt      = 0.0f;
  apogeeArmed    = false;
  descentCycles  = 0;
  autoEjectFired = false;

#if ENABLE_AUTO_EJECT
  Serial.print("[FLT] auto-eject enabled  arm ");
  Serial.print(AUTO_EJECT_ARM_ALT_M, 1);
  Serial.print(" m  drop ");
  Serial.print(AUTO_EJECT_DROP_M, 1);
  Serial.print(" m  confirm ");
  Serial.print(AUTO_EJECT_CONFIRM_N);
  Serial.println(" cycles");
#else
  Serial.println("[FLT] auto-eject DISABLED - uplink is the only release path");
#endif
}

/* One cycle of the rule. Returns true on the single cycle the trigger fires, so
 * the caller can count and release; false every other time.
 *
 * Apogee tracking runs even with ENABLE_AUTO_EJECT at 0 — the maximum altitude is
 * a flight figure in its own right, and turning the trigger off should not also
 * throw away the measurement it was built on.
 */
bool apogeeUpdate(float alt) {
  if (alt > apogeeAlt) apogeeAlt = alt;

  if (!apogeeArmed && alt >= AUTO_EJECT_ARM_ALT_M) {
    apogeeArmed = true;
    Serial.print("[FLT] auto-eject ARMED at ");
    Serial.print(alt, 1);
    Serial.println(" m");
  }

#if ENABLE_AUTO_EJECT
  /* chuteIsFired() covers the case the ground got there first. Without it the
   * descent would still be detected on the way down and would still increment the
   * chute counter, reporting a second release that never happened — the mechanism
   * is one-shot, so only the count would move. */
  if (!apogeeArmed || autoEjectFired || chuteIsFired()) return false;

  float drop = apogeeAlt - alt;

  /* CONSECUTIVE, not cumulative. A single sample that qualifies and is then
   * contradicted must not leave credit behind for the next one. */
  if (drop < AUTO_EJECT_DROP_M) {
    descentCycles = 0;
    return false;
  }

  if (++descentCycles < AUTO_EJECT_CONFIRM_N) return false;

  autoEjectFired = true;

  /* Printed with all three numbers, not just a verdict. This line reaches the raw
   * log through the serial echo, and it is the only record of WHY the vehicle
   * decided to release — a bare "AUTO-EJECT" would leave the threshold question
   * unanswerable after the flight. */
  Serial.print("[FLT] AUTO-EJECT  apogee ");
  Serial.print(apogeeAlt, 1);
  Serial.print(" m  alt ");
  Serial.print(alt, 1);
  Serial.print(" m  drop ");
  Serial.print(drop, 1);
  Serial.print(" m over ");
  Serial.print(AUTO_EJECT_CONFIRM_N);
  Serial.println(" cycles");
  return true;
#else
  return false;
#endif
}

float apogeeAltitude() { return apogeeAlt; }
bool  apogeeIsArmed()  { return apogeeArmed; }
bool  apogeeDidFire()  { return autoEjectFired; }
