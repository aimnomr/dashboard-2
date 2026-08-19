/* ============================================================================
 *  Uplink — command intake from the PC, and the eject burst.
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
 *  window and 1000 ms cycle, three consecutive attempts span 702 ms, wider than
 *  the 600 ms deaf period, so three in a row cannot all miss.
 *
 *  IMPORTANT: that guarantee depends on the spacing staying under the vehicle's
 *  LISTEN_WINDOW_MS. At 300 vs 400 ms the margin is 49 ms. Shortening the
 *  vehicle's window without shortening EJECT_RETRY_MS turns this back into a
 *  probability, silently.
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
      Serial.println("[GCS] EJECT already confirmed, ignoring");
      return;
    }
    Serial.println("[GCS] EJECT armed");
    fireEjectBurst();
    return;
  }

  Serial.print("[GCS] unknown command: ");
  Serial.println(line);
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
    /* Stop early if the vehicle has already reported it. radioPoll() keeps
     * lastChute current between attempts, since receive is restored each time.
     *
     * NOTE: chute >= 1 means the vehicle received the command and drove the
     * servo. It does NOT mean the parachute opened — there is no feedback
     * sensor. Nothing downstream may claim otherwise. */
    if (lastChute >= 1) {
      ejectConfirmed = true;
      Serial.print("[GCS] EJECT confirmed after ");
      Serial.print(i);
      Serial.println(" attempt(s)");
      return;
    }

    ejectAttempts++;
    bool sent = radioTransmit(EJECT_TOKEN);

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

  Serial.print("[GCS] EJECT burst complete, ");
  Serial.print(EJECT_ATTEMPTS);
  Serial.println(" sent - watch chute in telemetry");
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
