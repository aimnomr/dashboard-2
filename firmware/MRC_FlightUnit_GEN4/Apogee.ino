/* ============================================================================
 *  Apogee — the highest altitude seen, and the descent trigger built on it.
 *
 *  Isolated from Chute.ino on the same principle that file states: the DECISION
 *  to release and the MECHANISM that releases are different problems. Chute.ino
 *  owns the pin and the servo; this file owns the question of when. Either can be
 *  replaced without touching the other.
 *
 *  That separation is also why GEN4 has TWO reset commands. There are two latches
 *  and they fail in opposite directions:
 *
 *      trigger state  (here)        apogee, armed, cycle count, autoEjectFired
 *      fire latch     (Chute.ino)   chuteEverFired
 *
 *  Since 061 Chute.ino also carries a SHORT-lived drive latch that expires on its own
 *  (chuteFired, CHUTE_REARM_MS). That one is invisible here on purpose: chuteIsFired()
 *  reports the sticky flag, so nothing in this file has to know the mechanism can now
 *  re-arm itself, and the guard below still means what it has always meant.
 *
 *  Clearing trigger state cannot drive the mechanism — at worst it moves WHERE the
 *  rule will fire. Clearing the fire latch does something categorically different:
 *  it makes an already-fired chute fireable again. One is a safe "never mind", the
 *  other is dangerous in the air, so they do not share a token.
 *
 *  Note the contrast with a power cycle, further down. A brownout re-zeroes the
 *  BAROMETRIC BASELINE as well, so the vehicle comes back reading ~0 m and genuinely
 *  cannot re-arm. RESET leaves the baseline alone, so altitude stays whatever it
 *  was — which is why RESET re-arms and a brownout does not.
 *
 *      RESET        trigger state only. Safe at any time. Use it when the
 *                   barometer has told a lie — a unit warming on the pad can
 *                   report a climb that never happened, and that false apogee is
 *                   a live trigger.
 *
 *                   ⚠ RESET RE-BASES the trigger, it does NOT cancel it. Arming
 *                   tests altitude above BOOT, not a climb, so a vehicle still
 *                   high when RESET arrives re-arms on the very next cycle with
 *                   apogee re-zeroed to wherever it now is — and fires again once
 *                   it has dropped dropM from THERE. Traced: RESET at 150 m
 *                   re-armed at 140 m and fired at 120 m.
 *
 *                   That is useful — it is how you recover from a poisoned apogee
 *                   without losing the trigger — but it is not a cancel.
 *                   SET:AUTO:0 is the cancel.
 *
 *      RESET:CHUTE  the above, plus the fire latch. The IMMEDIATE way to re-run a
 *                   deployment test on a sealed unit without opening it, which is
 *                   what it exists for. In flight it re-arms a fired chute.
 *
 *                   No longer the ONLY way to drive the mechanism twice — since 061
 *                   the drive latch expires by itself and a second EJECT is enough.
 *                   It IS still the only way to clear chuteEverFired, so it remains
 *                   the only way to make the auto-eject rule eligible again after a
 *                   commanded release. That is the dangerous half, and it still has
 *                   its own token.
 *
 *  Note that plain RESET after something has fired does nothing useful: the rule
 *  re-arms and re-decides, and then apogeeUpdate() refuses on chuteIsFired() before
 *  it ever reaches chuteFire(). That is the intended shape, not an oversight — and
 *  it is why the sticky flag had to stay sticky when the drive latch stopped being.
 *
 *  The rule itself:
 *
 *      1. Track the maximum of `alt` every cycle. Always — an apogee figure is
 *         worth having even on a flight that never arms, and even with the trigger
 *         switched off.
 *      2. Arm once altitude passes cfg.armAltM. Before that the rule is inert no
 *         matter what the barometer says.
 *      3. Armed: count CONSECUTIVE cycles where apogee - alt >= cfg.dropM. Any
 *         cycle that does not qualify resets the count to zero.
 *      4. At cfg.confirmN, fire once and never again.
 *
 *  ⚠ Altitude here is BAROMETRIC and relative to boot, which is what makes step 2
 *  load-bearing. It inherits every weakness of a pressure altimeter: a gust, a
 *  slipstream or a sealed-then-warmed payload bay all move it without the vehicle
 *  moving. The arming floor and the confirmation count are what stand between
 *  those and the parachute.
 *
 *  ⚠ No state here survives a power cycle, config included. A brownout returns the
 *  vehicle to the compiled defaults in Config.h, disarmed, with apogee at zero —
 *  and on the way down it will not climb far enough to re-arm. The uplink is the
 *  backup to the backup, and that is the case it covers.
 * ========================================================================= */

/* Runtime configuration. GEN3 read the Config.h macros directly; GEN4 copies them
 * once at boot and reads the copy, so the ground station can change them in flight.
 * Nothing below this line refers to the macros again except apogeeBegin(). */
struct AutoEjectCfg {
  bool    enabled;
  float   armAltM;
  float   dropM;
  uint8_t confirmN;
};

static AutoEjectCfg cfg = {
  ENABLE_AUTO_EJECT != 0,
  AUTO_EJECT_ARM_ALT_M,
  AUTO_EJECT_DROP_M,
  AUTO_EJECT_CONFIRM_N
};

static float   apogeeAlt      = 0.0f;   /* highest alt seen, m above boot */
static bool    apogeeArmed    = false;  /* has climbed past the arming floor */
static uint8_t descentCycles  = 0;      /* consecutive SAMPLES over the drop threshold */
static bool    autoEjectFired = false;  /* this file's own one-shot latch */

/* millis() of the last altitude sample this file took for itself. The trigger no longer
 * rides the telemetry cycle — see apogeeTick() and AUTO_EJECT_SAMPLE_MS. */
static uint32_t apogeeSampledMs = 0;

/* Non-finite altitudes refused by apogeeUpdate(), for the whole boot. Counted rather
 * than merely ignored: a rising number is the only in-flight evidence that the trigger
 * is being starved, and the packet has no field to carry it. */
static uint32_t apogeeRejected  = 0;

/* The active configuration as one line, prefixed `#` so it can be appended to the
 * SD log directly.
 *
 * `#` is not decoration. Every FLIGHTnn.CSV already opens with two `#` lines, and
 * the dashboard parser classifies them as status rather than as rejected frames —
 * verified, not assumed — so writing more of them mid-file costs nothing and breaks
 * no replay. It is also the ONLY in-flight record of what the vehicle was actually
 * configured to do: GEN4 deliberately did not add packet fields for this, so a
 * sealed unit's config is answerable after recovery and not before. */
void apogeeConfigLine(char *out, size_t cap) {
  snprintf(out, cap, "# %lu cfg auto=%d arm=%.1f drop=%.1f cycles=%u repeat=%d",
           (unsigned long)millis(), cfg.enabled ? 1 : 0,
           cfg.armAltM, cfg.dropM, (unsigned)cfg.confirmN,
           chuteRepeatEnabled() ? 1 : 0);
}

static void apogeeReportConfig(const char *why) {
  char line[80];
  apogeeConfigLine(line, sizeof(line));
  Serial.print("[FLT] ");
  Serial.print(why);
  Serial.print("  ");
  Serial.println(line + 2);        /* skip the "# " when talking to a human */
  storageWrite(line);              /* but keep it on the card exactly as written */
}

/* Call after sensorsCalibrate(), never before: the altitude zero is set there, and
 * an apogee tracked against an unzeroed baseline is measuring the launch site's
 * elevation above sea level. */
void apogeeBegin() {
  cfg.enabled  = (ENABLE_AUTO_EJECT != 0);
  cfg.armAltM  = AUTO_EJECT_ARM_ALT_M;
  cfg.dropM    = AUTO_EJECT_DROP_M;
  cfg.confirmN = AUTO_EJECT_CONFIRM_N;

  apogeeAlt      = 0.0f;
  apogeeArmed    = false;
  descentCycles  = 0;
  autoEjectFired = false;

  apogeeReportConfig("auto-eject boot config");
}

/* ---- the rule -------------------------------------------------------------- */

/* One cycle. Returns true on the single cycle the trigger fires, so the caller can
 * count and release; false every other time.
 *
 * Apogee tracking runs even with the trigger disabled — the maximum altitude is a
 * flight figure in its own right, and switching the rule off should not also throw
 * away the measurement it was built on. This is now a runtime test rather than the
 * `#if` GEN3 used, because SET:AUTO can flip it after the build.
 */
bool apogeeUpdate(float alt) {
  /* REJECT THE SAMPLE AND HOLD STATE. Nothing below this line runs, so apogeeAlt is not
   * moved, apogeeArmed is not changed, descentCycles is neither reset nor incremented,
   * and the latch is not touched. The rule simply does not advance on evidence it does
   * not have, and picks up where it left off when good data returns.
   *
   * This is devlog 065 fault 4, and it was the dangerous one. Every comparison against
   * NaN is false, and the rule read those falses in OPPOSITE directions:
   *
   *     if (alt > apogeeAlt)          false -> apogee frozen
   *     if (!armed && alt >= armAltM) false -> cannot ARM on NaN        (pad was safe)
   *     if (drop < cfg.dropM)         false -> descentCycles NOT reset  (and climbs)
   *
   * So an already-armed vehicle that started reading NaN counted to confirmN and
   * deployed wherever it was. Session 20260909-024620 produced exactly that input on a
   * bench: -nan altitudes, humidity pinned at 100 %, pressure at -175 hPa, and both I2C
   * sensors out together across five restarts.
   *
   * The guard mattered more after devlog 071 than before it. At one sample per second
   * three NaNs took 3 s; at AUTO_EJECT_SAMPLE_MS they take ~375 ms.
   *
   * ⚠ HOLDING is not the same as recovering. A barometer that fails permanently leaves
   * the trigger frozen for the rest of the flight — it will not fire wrongly, and it
   * will not fire at all. The uplink is the backup for that case, which is what the
   * uplink has always been for. The count is printed rather than left silent because
   * GEN3.1 has no packet field that could carry it. */
  if (!isfinite(alt)) {
    apogeeRejected++;
    if (apogeeRejected == 1 || (apogeeRejected % 100) == 0) {
      Serial.print("[FLT] auto-eject: non-finite altitude REJECTED, state held, count ");
      Serial.println(apogeeRejected);
    }
    return false;
  }

  if (alt > apogeeAlt) apogeeAlt = alt;

  if (!apogeeArmed && alt >= cfg.armAltM) {
    apogeeArmed = true;
    Serial.print("[FLT] auto-eject ARMED at ");
    Serial.print(alt, 1);
    Serial.println(" m");
  }

  /* chuteIsFired() covers the case the ground got there first. Without it the
   * descent would still be detected on the way down and would still increment the
   * chute counter, reporting a second release that never happened — the mechanism
   * is one-shot, so only the count would move. */
  if (!cfg.enabled || !apogeeArmed || autoEjectFired || chuteIsFired()) return false;

  float drop = apogeeAlt - alt;

  /* CONSECUTIVE, not cumulative. A single sample that qualifies and is then
   * contradicted must not leave credit behind for the next one. */
  if (drop < cfg.dropM) {
    descentCycles = 0;
    return false;
  }

  if (++descentCycles < cfg.confirmN) return false;

  autoEjectFired = true;

  /* Printed with all three numbers, not just a verdict. This line reaches the raw
   * log through the serial echo, and it is the only record of WHY the vehicle
   * decided to release — a bare "AUTO-EJECT" would leave the threshold question
   * unanswerable after the flight, and on GEN4 the threshold is no longer knowable
   * from the source alone. */
  Serial.print("[FLT] AUTO-EJECT  apogee ");
  Serial.print(apogeeAlt, 1);
  Serial.print(" m  alt ");
  Serial.print(alt, 1);
  Serial.print(" m  drop ");
  Serial.print(drop, 1);
  Serial.print(" m over ");
  Serial.print(cfg.confirmN);
  Serial.println(" samples");
  return true;
}

/* --------------------------------------------------------------------------
 *  The trigger's own clock. Called from every poll loop in the cycle, like
 *  chuteTick(), and self-throttled to one altitude read per AUTO_EJECT_SAMPLE_MS.
 *
 *  Until devlog 071 the rule ran exactly once per telemetry cycle, so confirmN was
 *  counted in whole seconds: three confirmations cost 2 s of fall plus up to another
 *  second of sampling granularity — 57 to 96 m in freefall, for a decision the
 *  barometer could have supported eight times over in the same span.
 *
 *  Decoupling the two is the whole change. Telemetry stays at 1 Hz — this is NOT the
 *  2 Hz proposal rejected in devlog 036, and no packet is sent any faster — while the
 *  trigger samples at the rate the SENSOR can actually support.
 *
 *  ⚠ AUTO_EJECT_SAMPLE_MS is set just above the BME280's worst-case conversion time,
 *  so every sample is a fresh measurement. Read faster and the same conversion is
 *  counted twice, which would let ONE physical measurement satisfy two confirmations —
 *  the exact thing confirmN exists to prevent. The reasoning and the numbers are in
 *  Config.h; do not lower it without reading them.
 *
 *  Cooperative, not a second task. The ESP32-S3 has a spare core, but the barometer is
 *  the rate limit rather than the CPU, so a parallel task would sample no faster while
 *  putting a mutex between two contexts on an I2C bus that dropped both sensors in
 *  session 20260909-024620, and making chuteFire() reentrant across cores. All cost,
 *  no gain.
 * ----------------------------------------------------------------------- */
void apogeeTick() {
  uint32_t now = millis();
  if ((uint32_t)(now - apogeeSampledMs) < AUTO_EJECT_SAMPLE_MS) return;
  apogeeSampledMs = now;

  /* Sampled even when the trigger is switched off, because apogeeUpdate() tracks the
   * highest altitude seen whether or not it is allowed to act on it, and an apogee
   * figure is worth having on a flight that never arms. */
  if (apogeeUpdate(sensorsAltitude())) chuteFireFromAuto();
}

/* How many samples the trigger has refused. Nothing reads this yet — it exists so a
 * bench session can ask, and so the number has a name. */
uint32_t apogeeRejectedCount() {
  return apogeeRejected;
}

/* ---- uplink commands ------------------------------------------------------- */

/* RESET, and RESET:CHUTE when alsoChute is true. See the header for why these are
 * separate commands rather than one. */
void apogeeReset(bool alsoChute) {
  apogeeAlt      = 0.0f;
  apogeeArmed    = false;
  descentCycles  = 0;
  autoEjectFired = false;

  /* ⚠ Re-bases, does not cancel. If the vehicle is still above cfg.armAltM it will
   * re-arm on the next cycle against a fresh apogee. SET:AUTO:0 is the cancel. */
  if (alsoChute) {
    chuteResetLatch();
    Serial.println("[FLT] RESET:CHUTE - trigger state cleared AND fire latch cleared");
    Serial.println("[FLT] the mechanism can be driven again");
  } else {
    Serial.println("[FLT] RESET - trigger state cleared, fire latch untouched");
  }

  /* chuteCommands is NOT reset by either command. It is the ground station's
   * confirmation signal for the eject burst, and zeroing it would make an already
   * fired chute look armed to the operator — the one lie this system must not tell. */
  apogeeReportConfig(alsoChute ? "after RESET:CHUTE" : "after RESET");
}

/* Apply one `SET:` command. `arg` is everything AFTER the "SET:" prefix, e.g.
 * "DROP:15.0". Returns true if the setting was accepted and applied.
 *
 * Out-of-range values are refused, never clamped — see the bounds block in Config.h
 * for why. atof/atoi return 0 on garbage, which lands outside every range here, so
 * a malformed value is rejected by the same check that catches a silly one.
 */
bool apogeeHandleSet(const char *arg) {
  const char *sep = strchr(arg, ':');
  if (sep == NULL || sep[1] == 0) {
    Serial.print("[FLT] SET rejected, expected KEY:VALUE, got ");
    Serial.println(arg);
    return false;
  }

  size_t      keyLen = (size_t)(sep - arg);
  const char *value  = sep + 1;

  if (keyLen == 4 && strncmp(arg, "DROP", 4) == 0) {
    float v = atof(value);
    if (v < AUTO_EJECT_DROP_MIN_M || v > AUTO_EJECT_DROP_MAX_M) {
      Serial.print("[FLT] SET:DROP rejected, out of range: ");
      Serial.println(v, 1);
      return false;
    }
    cfg.dropM = v;

  } else if (keyLen == 6 && strncmp(arg, "CYCLES", 6) == 0) {
    int v = atoi(value);
    if (v < AUTO_EJECT_CYCLES_MIN || v > AUTO_EJECT_CYCLES_MAX) {
      Serial.print("[FLT] SET:CYCLES rejected, out of range: ");
      Serial.println(v);
      return false;
    }
    cfg.confirmN = (uint8_t)v;
    /* A shorter count must not fire on credit accumulated under the old one: the
     * operator asked for N consecutive cycles from now, not N counted retroactively. */
    descentCycles = 0;

  } else if (keyLen == 3 && strncmp(arg, "ARM", 3) == 0) {
    float v = atof(value);
    if (v < AUTO_EJECT_ARM_MIN_M || v > AUTO_EJECT_ARM_MAX_M) {
      Serial.print("[FLT] SET:ARM rejected, out of range: ");
      Serial.println(v, 1);
      return false;
    }
    cfg.armAltM = v;
    /* Deliberately does NOT disarm an already-armed vehicle. Raising the floor
     * mid-flight is a request about future arming, not an instruction to forget
     * that this vehicle has already flown. RESET is the command that forgets. */

  } else if (keyLen == 4 && strncmp(arg, "AUTO", 4) == 0) {
    int v = atoi(value);
    if (v != 0 && v != 1) {
      Serial.print("[FLT] SET:AUTO rejected, expected 0 or 1, got ");
      Serial.println(value);
      return false;
    }
    cfg.enabled = (v == 1);
    /* Switching off mid-descent leaves apogee and armed intact, so switching back
     * on resumes the rule where it was rather than starting over. */

  } else if (keyLen == 6 && strncmp(arg, "REPEAT", 6) == 0) {
    int v = atoi(value);
    if (v != 0 && v != 1) {
      Serial.print("[FLT] SET:REPEAT rejected, expected 0 or 1, got ");
      Serial.println(value);
      return false;
    }
    /* Dispatched here because every SET is, but the state lives in Chute.ino with the
     * mechanism it governs. This one does NOT touch the apogee rule: auto-eject is
     * one-shot per boot in both modes, and RESET:CHUTE is its only re-arm. */
    chuteSetRepeat(v == 1);

  } else {
    Serial.print("[FLT] SET rejected, unknown key: ");
    Serial.println(arg);
    return false;
  }

  apogeeReportConfig("config changed");
  return true;
}

/* ---- accessors ------------------------------------------------------------- */

float   apogeeAltitude() { return apogeeAlt; }
bool    apogeeIsArmed()  { return apogeeArmed; }
bool    apogeeDidFire()  { return autoEjectFired; }
bool    apogeeEnabled()  { return cfg.enabled; }
float   apogeeDropM()    { return cfg.dropM; }
uint8_t apogeeConfirmN() { return cfg.confirmN; }
