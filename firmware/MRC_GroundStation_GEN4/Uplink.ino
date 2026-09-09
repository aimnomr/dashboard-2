/* ============================================================================
 *  Uplink — command intake from the PC, the eject burst, and (GEN4) the config
 *  burst that retunes the vehicle's auto-eject trigger in flight.
 *
 *  Retry strategy: a BURST on the serial command, spread wide enough to cover a
 *  whole vehicle cycle. Carried back from MRC_GroundUnit_V3 (GEN2), which had
 *  this right before GEN3 replaced it. See devlog 043 and 044.
 *
 *  The strategy this replaced transmitted once per received telemetry packet, on
 *  the belief that "the vehicle's listen window has just opened". It has just
 *  CLOSED: the vehicle listens first and transmits last, so the packet that
 *  triggered the uplink is emitted at t~646 ms of its cycle and the window shut
 *  at t=400. That put every transmission in the deaf period by construction,
 *  which is why 15 retries in devlog 039 worked exactly as well as one.
 *
 *  Why a burst beats a better-timed single shot: it needs no phase estimate at
 *  all. With attempts spaced under the listen window and spanning more than one
 *  cycle, at least one lands inside the window whatever the phase — and the
 *  vehicle's phase is known to move, since it reports its own cycle overruns and
 *  resynchronises its cadence.
 *
 *      spacing <= LISTEN_WINDOW  and  span >= CYCLE_PERIOD  =>  guaranteed hit
 *
 *  At 300 ms spacing (~351 ms including airtime) against the vehicle's 400 ms
 *  window and 1000 ms cycle, two consecutive attempts span 702 ms, comfortably
 *  wider than the deaf period, so two in a row cannot both miss.
 *
 *  ⚠ "The 600 ms deaf period" is what this comment used to say, and it described
 *  the arrangement devlog 044 replaced — a blind 354 ms delay in the back half of
 *  the cycle, on top of the sensor read and the transmit. The vehicle now listens
 *  through the back half, and since devlog 069 it also services the uplink in the
 *  gap between the sensor read and the transmit. The deaf stretch is the TRANSMIT
 *  ITSELF, ~231 ms of a 1000 ms cycle — the radio is half duplex and that part is
 *  irreducible. Stale in the safe direction: the guarantee is stronger than the
 *  old arithmetic claimed, not weaker.
 *
 *  IMPORTANT: the guarantee depends on the spacing staying under the vehicle's
 *  LISTEN_WINDOW_MS and OVER its deaf stretch. At 351 vs 400 ms the upper margin
 *  is 49 ms; against ~231 ms of transmit the lower margin is ~120 ms. Shortening
 *  the vehicle's window without shortening EJECT_RETRY_MS breaks the first;
 *  shortening EJECT_RETRY_MS below the transmit time breaks the second and lets
 *  two consecutive attempts land in one deaf window. Either turns this back into
 *  a probability, silently.
 * ========================================================================= */

static char    serialLine[SERIAL_LINE_BUF];
static uint8_t serialLen = 0;

/* --------------------------------------------------------------------------
 *  Read whole lines from the PC without ever blocking.
 * ----------------------------------------------------------------------- */
void uplinkPoll() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      serialLine[serialLen] = '\0';
      if (serialLen > 0) handleCommand(serialLine);
      serialLen = 0;
      continue;
    }

    if (serialLen < SERIAL_LINE_BUF - 1) {
      serialLine[serialLen++] = c;
    } else {
      /* Overlong line: discard it rather than silently truncating into
       * something that might match a command. */
      serialLen = 0;
      Serial.println("[GCS] command too long, discarded");
    }
  }

  /* A queued ping normally waits for a received packet, so it lands in the
   * vehicle's listen window. But if nothing is being received — the vehicle is
   * off, out of range, or on another channel — waiting forever would make a
   * link test impossible in exactly the situation you most want to run one.
   * Send it blind after a short delay instead. */
  if (pingPending && (millis() - pingRequestedMs) > PING_BLIND_AFTER_MS) {
    pingPending = false;
    pingsSent++;
    bool sent = radioTransmit(PING_TOKEN);
    Serial.print("[GCS] PING sent blind (no packets to time against)");
    Serial.println(sent ? "" : " - FAILED TO TRANSMIT");
  }

  /* Age out a confirmation that is never going to arrive. Done here, on a tick that
   * runs unconditionally, because radioPoll() only runs when a packet is waiting — and
   * the cases this exists for are exactly the ones where packets stop coming, or where
   * `chute` will never rise at all:
   *
   *   SINGLE mode      the vehicle's latch never expires, a second EJECT drives
   *                    nothing, and no packet can ever satisfy the test
   *   out of range     the vehicle is still flying and still releasing, but nothing
   *                    is being heard
   *
   * Left set, the flag would later explain an UNRELATED rise as confirming a command
   * the operator has long since moved on from.
   *
   * Confirms nothing, blocks nothing, moves no baseline: ejectConfirmed stays false, so
   * the very next EJECT is treated as an ordinary first attempt, which is correct. */
  if (ejectAwaitingConfirm &&
      (uint32_t)(millis() - ejectAwaitingConfirmMs) > EJECT_CONFIRM_TIMEOUT_MS) {
    ejectAwaitingConfirm = false;
    Serial.println("[GCS] EJECT confirmation timed out - no chute rise seen, "
                   "assume it was NOT received");
    Serial.println("[GCS] `ul` rising without `chute` would mean heard but not driven");
  }
}

void handleCommand(const char *line) {
  if (strcmp(line, CMD_PING) == 0) {
    pingPending     = true;
    pingRequestedMs = millis();
    Serial.println("[GCS] PING queued - watch the flight unit's OLED, "
                   "its UL counter should reset");
    return;
  }

  if (strcmp(line, CMD_EJECT) == 0) {
    if (ejectConfirmed) {
      /* Two different situations wear the same flag.
       *
       * Inside the cooldown the vehicle's mechanism is still travelling back and its
       * drive latch is still set, so a burst sent now would be received, counted into
       * `chute`, and drive nothing — a release the operator is shown and that never
       * happened. That is the failure 058 was written to prevent, and it stays
       * blocked.
       *
       * Past the cooldown the vehicle has re-armed itself (CHUTE_REARM_MS), so a
       * second EJECT is an ordinary request and is allowed. The ground re-arms the
       * same way RESET:CHUTE does — by moving the baseline, never by zeroing the
       * vehicle's counter.
       *
       * ⚠ Re-capturing chuteBaseline is not optional. `lastChute` is already above
       * the old baseline from the previous release, so leaving it alone would make
       * fireEjectBurst() confirm on its first test and transmit nothing at all. */
      /* SINGLE mode: the vehicle's latch never expires, so no amount of waiting makes
       * a second EJECT work. Sending one anyway would be received, counted into
       * `chute`, and drive nothing — a release the operator is shown and that never
       * happened. Say so instead of counting down to a cooldown that means nothing. */
      if (!assumedRepeat) {
        Serial.println("[GCS] EJECT already confirmed, ignoring - release mode is SINGLE");
        Serial.println("[GCS] send RESET:CHUTE to re-arm, or SET:REPEAT:1 for repeat releases");
        return;
      }

      if ((uint32_t)(millis() - ejectConfirmedMs) < EJECT_REARM_MS) {
        Serial.println("[GCS] EJECT already confirmed, ignoring - vehicle still re-arming");
        Serial.println("[GCS] wait for the cooldown, or send RESET:CHUTE to re-arm now");
        return;
      }

      ejectConfirmed = false;
      chuteBaseline  = (lastChute >= 0) ? lastChute : 0;
      Serial.print("[GCS] EJECT re-armed after cooldown, chute baseline ");
      Serial.println(chuteBaseline);
      Serial.println("[GCS] the next release must exceed that to confirm");
    }
    Serial.println("[GCS] EJECT armed");
    fireEjectBurst();
    return;
  }

  if (strcmp(line, CMD_RESET_CHUTE) == 0) {
    /* Checked BEFORE CMD_RESET. These are strcmp, so exact — "CMD:RESET" cannot
     * swallow "CMD:RESET:CHUTE" — but the order is kept deliberate anyway. */
    Serial.println("[GCS] RESET:CHUTE armed - this re-arms a FIRED chute");

    /* Captured BEFORE the burst: fireConfigBurst() polls the radio between
     * attempts, so lastChute is live while it runs. The vehicle does not move its
     * chute counter on a reset, so this will not change under us — but reading it
     * first is correct regardless of that. */
    int baselineAtReset = lastChute;

    /* Clear the ground station's eject latches ONLY if the vehicle confirmed.
     *
     * Confirmation is `ul` rising during the burst, which for this command is
     * stronger than it is for SET: there is no rejection path in apogeeReset(), so
     * a RESET:CHUTE that was RECEIVED was applied. For SET, `ul` rising proves
     * receipt and not application, which is why that distinction is laboured
     * everywhere else.
     *
     * Clearing them unconditionally would be worse than not clearing them at all.
     * If the vehicle never heard the reset its fire latch is still set, and an
     * EJECT sent afterwards would transmit, be received, increment `chute` to 2,
     * and drive nothing — a release the operator has been shown and that never
     * happened. */
    if (fireConfigBurst(RESET_CHUTE_TOKEN)) {
      ejectConfirmed = false;
      chuteBaseline  = (baselineAtReset >= 0) ? baselineAtReset : 0;

      /* Discard any confirmation still in flight from a burst sent before this reset.
       * The baseline just moved to a value captured before fireConfigBurst() ran, and
       * that burst polls the radio — so a delayed EJECT confirmation could land during
       * it and then satisfy the test against the NEW baseline, printing "EJECT
       * confirmed" immediately after the operator was told the vehicle is freshly
       * re-armed. A rise arriving after this point is no longer safely attributable to
       * whatever burst set the flag. Let the next EJECT set it again. */
      if (ejectAwaitingConfirm) {
        ejectAwaitingConfirm = false;
        Serial.println("[GCS] pending EJECT confirmation discarded by RESET:CHUTE");
      }

      Serial.print("[GCS] EJECT re-armed at ground, chute baseline ");
      Serial.println(chuteBaseline);
      Serial.println("[GCS] the next release must exceed that to confirm");
    } else {
      Serial.println("[GCS] EJECT still latched here - the vehicle did not confirm");
      Serial.println("[GCS] resend RESET:CHUTE; do NOT assume the chute is re-armed");
    }
    return;
  }

  if (strcmp(line, CMD_RESET) == 0) {
    Serial.println("[GCS] RESET armed - trigger state only");
    fireConfigBurst(RESET_TOKEN);
    return;
  }

  if (strncmp(line, CMD_SET_PREFIX, strlen(CMD_SET_PREFIX)) == 0) {
    const char *arg = line + strlen(CMD_SET_PREFIX);
    if (!configValueInRange(arg)) return;      /* it says why */

    char token[SERIAL_LINE_BUF];
    snprintf(token, sizeof(token), "%s%s", SET_PREFIX, arg);
    Serial.print("[GCS] ");
    Serial.print(token);
    Serial.println(" armed");

    bool sent = fireConfigBurst(token);

    /* Track the release mode so the console guard below knows whether the vehicle's
     * drive latch expires. Recorded only on a CONFIRMED burst: `ul` rising proves
     * receipt, and for REPEAT there is no rejection path the vehicle could take that
     * receipt would hide — the value is 0 or 1 and both were validated here.
     *
     * ⚠ This is an ASSUMPTION, not a readback. GEN3.1 carries no config fields, so the
     * ground cannot see the vehicle's actual mode; it can only remember what it last
     * successfully told it. assumedRepeat is reset on a detected reboot, because the
     * vehicle returns to CHUTE_AUTO_REARM when it restarts. */
    if (sent && strncmp(arg, "REPEAT:", 7) == 0) {
      assumedRepeat = (atoi(arg + 7) == 1);
      Serial.print("[GCS] release mode now assumed ");
      Serial.println(assumedRepeat ? "MULTI" : "SINGLE");
    }
    return;
  }

  Serial.print("[GCS] unknown command: ");
  Serial.println(line);
}

/* --------------------------------------------------------------------------
 *  Validate a SET argument BEFORE it is transmitted. `arg` is "KEY:VALUE".
 *
 *  The vehicle checks these same bounds again and would refuse a bad value anyway,
 *  so why here as well? Because a refusal at the vehicle is nearly invisible: `ul`
 *  rises either way, so the burst confirms, and the operator is told the command
 *  landed while the setting did not change. Catching it here turns a silent
 *  no-op into a message on the screen in front of the person who typed it.
 *
 *  ⚠ Bounds mirrored in MRC_FlightUnit_GEN4/Config.h. Change both together.
 * ----------------------------------------------------------------------- */
bool configValueInRange(const char *arg) {
  const char *sep = strchr(arg, ':');
  if (sep == NULL || sep[1] == 0) {
    Serial.print("[GCS] SET rejected, expected KEY:VALUE, got ");
    Serial.println(arg);
    return false;
  }

  size_t      keyLen = (size_t)(sep - arg);
  const char *value  = sep + 1;

  if (keyLen == 4 && strncmp(arg, "DROP", 4) == 0) {
    float v = atof(value);
    if (v < AUTO_EJECT_DROP_MIN_M || v > AUTO_EJECT_DROP_MAX_M) {
      Serial.print("[GCS] SET:DROP rejected, must be ");
      Serial.print(AUTO_EJECT_DROP_MIN_M, 1);
      Serial.print(" to ");
      Serial.print(AUTO_EJECT_DROP_MAX_M, 1);
      Serial.print(" m, got ");
      Serial.println(v, 1);
      return false;
    }
    return true;
  }

  if (keyLen == 6 && strncmp(arg, "REPEAT", 6) == 0) {
    int v = atoi(value);
    if (v != 0 && v != 1) {
      Serial.print("[GCS] SET:REPEAT rejected, expected 0 or 1, got ");
      Serial.println(value);
      return false;
    }
    return true;
  }

  if (keyLen == 6 && strncmp(arg, "CYCLES", 6) == 0) {
    int v = atoi(value);
    if (v < AUTO_EJECT_CYCLES_MIN || v > AUTO_EJECT_CYCLES_MAX) {
      Serial.print("[GCS] SET:CYCLES rejected, must be ");
      Serial.print(AUTO_EJECT_CYCLES_MIN);
      Serial.print(" to ");
      Serial.print(AUTO_EJECT_CYCLES_MAX);
      Serial.print(", got ");
      Serial.println(v);
      return false;
    }
    return true;
  }

  if (keyLen == 3 && strncmp(arg, "ARM", 3) == 0) {
    float v = atof(value);
    if (v < AUTO_EJECT_ARM_MIN_M || v > AUTO_EJECT_ARM_MAX_M) {
      Serial.print("[GCS] SET:ARM rejected, must be ");
      Serial.print(AUTO_EJECT_ARM_MIN_M, 1);
      Serial.print(" to ");
      Serial.print(AUTO_EJECT_ARM_MAX_M, 1);
      Serial.print(" m, got ");
      Serial.println(v, 1);
      return false;
    }
    return true;
  }

  if (keyLen == 4 && strncmp(arg, "AUTO", 4) == 0) {
    int v = atoi(value);
    if (v != 0 && v != 1) {
      Serial.print("[GCS] SET:AUTO rejected, expected 0 or 1, got ");
      Serial.println(value);
      return false;
    }
    return true;
  }

  Serial.print("[GCS] SET rejected, unknown key: ");
  Serial.println(arg);
  return false;
}

/* --------------------------------------------------------------------------
 *  Transmit a config command as a burst, and stop early when `ul` rises.
 *
 *  Same geometry as fireEjectBurst() and for the same reason — a single shot lands
 *  in the vehicle's deaf period more often than not. The difference is the witness:
 *  EJECT watches `chute`, this watches `ul`.
 *
 *  ⚠ `ul` rising proves the vehicle RECEIVED a command. It does not prove which one
 *  arrived, or that the value was applied rather than refused. It is the strongest
 *  signal available without adding a packet field, which GEN4 deliberately did not
 *  do. Treat "confirmed" as "it heard me", not as "it is now set to 15".
 *
 *  Against a vehicle that does not report `ul` at all, baseline stays -1, nothing
 *  ever looks like a rise, and the burst runs to completion and says so.
 * ----------------------------------------------------------------------- */
bool fireConfigBurst(const char *token) {
  int baseline = lastUl;

  for (uint8_t i = 0; i < CONFIG_ATTEMPTS; i++) {
    if (baseline >= 0 && lastUl > baseline) {
      Serial.print("[GCS] ");
      Serial.print(token);
      Serial.print(" confirmed after ");
      Serial.print(i);
      Serial.print(" attempt(s), ul ");
      Serial.print(baseline);
      Serial.print(" -> ");
      Serial.println(lastUl);
      return true;
    }

    bool sent = radioTransmit(token);
    Serial.print("[GCS] ");
    Serial.print(token);
    Serial.print(" attempt ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.print(CONFIG_ATTEMPTS);
    Serial.println(sent ? "" : " FAILED TO TRANSMIT");

    uint32_t until = millis() + CONFIG_RETRY_MS;
    while ((int32_t)(until - millis()) > 0) {
      radioPoll();
      delay(LOOP_TICK_MS);
    }
  }

  if (baseline >= 0 && lastUl > baseline) {
    Serial.print("[GCS] ");
    Serial.print(token);
    Serial.println(" confirmed on the final attempt");
    return true;
  }

  Serial.print("[GCS] ");
  Serial.print(token);
  Serial.print(" NOT confirmed after ");
  Serial.print(CONFIG_ATTEMPTS);
  Serial.println(" attempts - ul did not rise");
  Serial.println("[GCS] the vehicle may be GEN3 (no ul), out of range, or deaf");
  return false;
}

/* --------------------------------------------------------------------------
 *  Transmit EJECT as a burst, wide enough to cover a whole vehicle cycle.
 *
 *  Blocks for about 1.4 s, during which one or two telemetry packets are missed.
 *  That is the trade and it is the right way round: losing a packet while
 *  commanding recovery costs a row in a log, and missing the listen window costs
 *  the parachute. radioTransmit() restores receive after every attempt, so the
 *  gap is only the airtime, not the whole burst.
 * ----------------------------------------------------------------------- */
void fireEjectBurst() {
  for (uint8_t i = 0; i < EJECT_ATTEMPTS; i++) {
    /* Stop sending once the vehicle has been seen to confirm. The DECISION is not
     * made here any more — radioPoll() makes it, and radioPoll() runs on every tick of
     * the wait loop below, so a confirmation is noticed within one LOOP_TICK_MS of
     * arriving. Reading the shared flag rather than recomputing lastChute against
     * chuteBaseline keeps exactly one place deciding what "confirmed" means; two
     * copies of that boundary is how it drifts. See devlog 070.
     *
     * All this exit does now is stop wasting airtime. */
    if (ejectConfirmed) {
      Serial.print("[GCS] EJECT burst stopped after ");
      Serial.print(i);
      Serial.println(" attempt(s) - already confirmed");
      return;
    }

    ejectAttempts++;
    bool sent = radioTransmit(EJECT_TOKEN);

    /* Set at the point of TRANSMISSION, so both exits from this function leave it set.
     * The run-out-of-attempts exit below recorded nothing at all until 070, which is
     * precisely how a confirmation arriving a moment too late poisoned the next EJECT.
     *
     * Refreshed per attempt rather than set once, so the timeout is measured from the
     * last thing actually sent. */
    ejectAwaitingConfirm   = true;
    ejectAwaitingConfirmMs = millis();

    Serial.print("[GCS] EJECT attempt ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.print(EJECT_ATTEMPTS);
    Serial.println(sent ? "" : " FAILED TO TRANSMIT");

    /* Keep receiving during the gap rather than sleeping through it: a
     * confirmation may land here, and it is what stops the burst early. */
    uint32_t until = millis() + EJECT_RETRY_MS;
    while ((int32_t)(until - millis()) > 0) {
      radioPoll();
      delay(LOOP_TICK_MS);
    }
  }

  /* radioPoll() may have confirmed during the final attempt's wait, after the loop's
   * own check last ran. Re-tested for the same reason fireConfigBurst() re-tests `ul`
   * after its loop: the last iteration's wait is the one window this function would
   * otherwise never look back at. */
  if (ejectConfirmed) {
    Serial.println("[GCS] EJECT confirmed during the final attempt");
    return;
  }

  /* Not "failed". The burst is over; the wait is not. ejectAwaitingConfirm is still
   * set, so a confirmation arriving in the next few seconds is still attributed to
   * this burst rather than being dropped on the floor — which is the whole of 070. */
  Serial.print("[GCS] EJECT burst complete, ");
  Serial.print(EJECT_ATTEMPTS);
  Serial.println(" sent - awaiting confirmation, watch chute in telemetry");
}

/* --------------------------------------------------------------------------
 *  Called from Radio.ino immediately after a telemetry packet is forwarded.
 *
 *  Only PING still rides on packet arrival, and only because a ping is a single
 *  shot with nothing to confirm it. With the flight unit listening through the
 *  back half of its cycle (devlog 044) this moment is now INSIDE the window
 *  rather than 246 ms after it closed.
 * ----------------------------------------------------------------------- */
void uplinkOnPacketReceived() {
  if (!pingPending) return;

  pingPending = false;
  pingsSent++;
  bool sent = radioTransmit(PING_TOKEN);
  Serial.print("[GCS] PING sent");
  Serial.println(sent ? " - no acknowledgement exists, check the flight unit's USB serial"
                      : " FAILED TO TRANSMIT");
}
