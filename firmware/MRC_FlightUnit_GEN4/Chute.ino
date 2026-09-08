/* ============================================================================
 *  Chute — the release mechanism.  (GEN4)
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

/* Three pieces of state where there used to be one, because "the horn is out",
 * "do not drive it again yet" and "this vehicle has released at some point" have
 * three different lifetimes and only the first two are on a timer.
 *
 *   chuteDriven    the mechanism is away from its rest position, right now.
 *                  Cleared by the return sweep, CHUTE_HOLD_MS after the drive.
 *   chuteFired     the drive latch. Suppresses repeats; self-clears after
 *                  CHUTE_REARM_MS so a later command can drive the mechanism again.
 *   chuteEverFired sticky for the whole boot. Cleared ONLY by chuteResetLatch().
 *                  This is what chuteIsFired() reports, so nothing outside this
 *                  file can see the re-arm timer. */
static bool     chuteDriven    = false;
static bool     chuteFired     = false;
static bool     chuteEverFired = false;
static uint32_t chuteDrivenMs  = 0;

/* Whether the drive latch is allowed to expire — SINGLE vs MULTI release mode.
 *
 * CHUTE_AUTO_REARM is the power-on DEFAULT, not the decision: SET:REPEAT:0|1 moves this
 * at runtime, so a sealed unit can be put in either mode without opening it. Reset to
 * the default by chuteBegin(), which means a reboot forgets it — the same rule the
 * apogee config already follows, and the safe direction when the default is SINGLE.
 *
 * ⚠ This governs the COMMANDED path only. Auto-eject is one-shot per boot in both
 * modes, gated by autoEjectFired and chuteEverFired, and RESET:CHUTE is the only thing
 * that re-arms it. See Apogee.ino — the descent condition stays true for the whole
 * descent, so an expiring latch there would fire every cycle of the fall. */
static bool     chuteRepeat    = (CHUTE_AUTO_REARM != 0);

void chuteBegin() {
#if CHUTE_USE_SERVO
  chuteServo.attach(CHUTE_PIN);
  chuteServo.write(CHUTE_SERVO_ARMED_DEG);
#else
  pinMode(CHUTE_PIN, OUTPUT);
  digitalWrite(CHUTE_PIN, LOW);
#endif
  chuteDriven    = false;
  chuteFired     = false;
  chuteEverFired = false;
  chuteRepeat    = (CHUTE_AUTO_REARM != 0);
}

/* Idempotent while the latch holds. The ground station retries until it sees the
 * count rise, so this will be called again on repeats — driving an already-released
 * mechanism must be harmless, and re-sweeping a servo every second would only draw
 * current and chatter the horn.
 *
 * Non-blocking since 061. The drive is one register write; the return sweep and the
 * re-arm are both deadlines that chuteTick() services from the cycle's existing poll
 * loops. The 1000 ms delay that used to sit here overran the cycle it landed in by
 * construction — 1000 ms of hold on top of ~650 ms of work against a 1000 ms period
 * — and cost one late packet at the exact moment the ground most wants a packet. */
void chuteFire() {
  if (chuteFired) return;
  chuteFired     = true;
  chuteEverFired = true;
  chuteDriven    = true;
  chuteDrivenMs  = millis();

#if CHUTE_USE_SERVO
  chuteServo.write(CHUTE_SERVO_RELEASE_DEG);
#else
  digitalWrite(CHUTE_PIN, HIGH);
#endif

  Serial.println("[FLT] CHUTE RELEASE COMMANDED");
}

/* Called from every poll loop in the cycle, so the two deadlines below are met to
 * within LISTEN_TICK_MS rather than to within a whole cycle. Cheap and reentrant:
 * two unsigned comparisons when there is nothing to do.
 *
 * ⚠ Both deadlines are measured from the drive, not from each other. */
void chuteTick() {
  if (!chuteFired && !chuteDriven) return;

  uint32_t since = millis() - chuteDrivenMs;

  /* 1. Return the mechanism to its rest position. This is what makes a second
   *    release possible at all: a servo left at RELEASE_DEG has nowhere to sweep
   *    from, and a pin left HIGH has nothing to drive. */
  if (chuteDriven && since >= CHUTE_HOLD_MS) {
#if CHUTE_USE_SERVO
    chuteServo.write(CHUTE_SERVO_ARMED_DEG);
#else
    digitalWrite(CHUTE_PIN, LOW);
#endif
    chuteDriven = false;
    Serial.println("[FLT] chute mechanism returned to ARMED position");
  }

  /* 2. Clear the drive latch, in MULTI mode only. Deliberately later than the return
   *    sweep and later than the ground's eject burst — see the arithmetic in Config.h.
   *    Clearing it at CHUTE_HOLD_MS instead would let attempts 4 and 5 of a single
   *    operator EJECT drive the mechanism a second time.
   *
   *    In SINGLE mode the latch stands until RESET:CHUTE, which is the pre-061
   *    behaviour and what a flight build should normally carry. */
  if (chuteRepeat && chuteFired && since >= CHUTE_REARM_MS) {
    chuteFired = false;
    Serial.println("[FLT] chute re-armed - a further release can be commanded");
  }
}

/* SET:REPEAT:0|1. Returns the value actually held, so the caller can report it.
 *
 * Turning repeat OFF does not re-latch a mechanism that has already re-armed: the
 * change applies from here forward, exactly like SET:AUTO leaving apogee state intact.
 * If the intent is "make this vehicle single-shot again from a known state", that is
 * SET:REPEAT:0 followed by RESET:CHUTE. */
void chuteSetRepeat(bool on) {
  chuteRepeat = on;
  Serial.print("[FLT] release mode ");
  Serial.println(on ? "MULTI - the drive latch expires on its own"
                    : "SINGLE - only RESET:CHUTE re-arms");
}

bool chuteRepeatEnabled() {
  return chuteRepeat;
}

/* Sticky for the boot, so apogeeUpdate()'s "the ground got there first" guard is
 * unaffected by the re-arm timer. */
bool chuteIsFired() {
  return chuteEverFired;
}

/* GEN4: clear the one-shot latch so the mechanism can be driven again.
 *
 * Reached ONLY by RESET:CHUTE from the ground station — never by RESET, and never
 * by anything on board. It exists for one reason: to re-run a deployment test on a
 * SEALED unit without opening it, which was previously impossible.
 *
 * ⚠ This does not un-deploy anything. Both paths return the mechanism to its rest
 * position — and since 061 chuteTick() has already done that on its own, so on the
 * bench this command is now about the LATCHES rather than the hardware. Neither
 * path repacks a parachute. Sent in flight after a real deployment, this makes a
 * fired chute fireable again and achieves nothing else.
 *
 * Still the only thing that clears chuteEverFired, which is why it remains the only
 * way to make apogeeUpdate() consider an auto-eject after a commanded release.
 *
 * chuteCommands is deliberately NOT touched — see apogeeReset(). */
void chuteResetLatch() {
#if CHUTE_USE_SERVO
  chuteServo.write(CHUTE_SERVO_ARMED_DEG);
#else
  digitalWrite(CHUTE_PIN, LOW);
#endif
  chuteDriven    = false;
  chuteFired     = false;
  chuteEverFired = false;
  Serial.println("[FLT] chute fire latch CLEARED - mechanism can fire again");
}
