/* ============================================================================
 *  Radio — LoRa init, the receive window, and GEN4 command dispatch.
 * ========================================================================= */

bool radioBegin() {
  int state = radio.begin(FREQ_MHZ, BANDWIDTH_KHZ, SPREADING,
                          CODING_RATE, SYNC_WORD, TX_POWER_DBM);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[FLT] radio.begin failed, code ");
    Serial.println(state);
    return false;
  }
  return true;
}

/* --------------------------------------------------------------------------
 *  Arm the receiver and return immediately.
 *
 *  The SX1262 receives on its own once armed: it fills its own buffer, raises DIO1,
 *  and HOLDS the packet until it is read. The CPU is free in between. That is what
 *  makes listening through the back half of the cycle almost free — see the call
 *  site in MRC_FlightUnit_GEN3.ino, immediately after the transmit.
 * ----------------------------------------------------------------------- */
void radioArmReceive() {
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[FLT] startReceive failed, code ");
    Serial.println(state);
  }
}

/* --------------------------------------------------------------------------
 *  One non-blocking check for an uplink command. Returns true if EJECT arrived.
 *
 *  Extracted from radioListenForEject() so the front window and the post-transmit
 *  hold dispatch commands through exactly the same code. Two copies of the token
 *  matching is how a second EJECT/CMD:EJECT mismatch (entry 033) gets written.
 *
 *  Re-arms after every read, whether the packet was ours, another team's, or a
 *  failed decode.
 * ----------------------------------------------------------------------- */
bool radioServiceUplink() {
  if (digitalRead(LORA_DIO1) != HIGH) return false;

  bool heardEject = false;
  String incoming;
  int rxState = radio.readData(incoming);

  if (rxState == RADIOLIB_ERR_NONE) {
    incoming.trim();

    if (incoming.equals(EJECT_TOKEN)) {
      heardEject   = true;
      uplinkCount++;
      lastUplinkMs = millis();
      uplinkHeard  = true;

    } else if (incoming.equals(PING_TOKEN)) {
      /* Link test. Proves the uplink works without touching the chute — this is the
       * check that can safely be run on the pad. */
      pingCount++;
      uplinkCount++;
      lastUplinkMs = millis();
      uplinkHeard  = true;
      Serial.print("[FLT] PING received, count ");
      Serial.println(pingCount);

    } else if (incoming.equals(RESET_CHUTE_TOKEN)) {
      /* Checked BEFORE RESET_TOKEN would matter if either were a prefix match. They
       * are not — equals() is exact, so "RESET" cannot swallow "RESET:CHUTE" — but
       * the order is kept deliberate anyway, because the day someone changes one of
       * these to startsWith() is the day the ordering starts mattering silently. */
      uplinkCount++;
      lastUplinkMs = millis();
      uplinkHeard  = true;
      apogeeReset(true);

    } else if (incoming.equals(RESET_TOKEN)) {
      uplinkCount++;
      lastUplinkMs = millis();
      uplinkHeard  = true;
      apogeeReset(false);

    } else if (incoming.startsWith(SET_PREFIX)) {
      /* The one prefix match in the protocol, because it is the one command that
       * carries a value. Everything else stays an exact comparison.
       *
       * uplinkCount rises even when the value is REFUSED, and that is correct: `ul`
       * counts commands RECEIVED, and a rejected command was received. It is also
       * what lets the ground station's burst stop on a command it got wrong instead
       * of hammering the channel five times over. The operator learns the value was
       * refused from the ground station, which pre-validates against the same bounds
       * and should never have transmitted it in the first place. */
      uplinkCount++;
      lastUplinkMs = millis();
      uplinkHeard  = true;
      apogeeHandleSet(incoming.c_str() + strlen(SET_PREFIX));
    }
    /* Anything else is another team's traffic. Ignored, not counted as uplink: a
     * foreign packet must never look like our ground station.
     *
     * uplinkCount is the `ul` telemetry field, and it is incremented HERE — inside
     * the branches that matched one of our own tokens — rather than derived at the
     * packet from pingCount + chuteCommands. Derivation was correct only while the
     * chute could not move on its own; auto-eject ended that. This is the one place
     * that can honestly answer "did we hear the ground station", so it is the one
     * place that counts it. */
  }

  radio.startReceive();
  return heardEject;
}

/* --------------------------------------------------------------------------
 *  Listen for the eject command for windowMs, then return to standby.
 *
 *  Carried over from GEN2 essentially unchanged, and the shape is deliberate.
 *  Its author's note:
 *
 *      Dropped radio.receive() entirely - timeout param behavior varies
 *      between RadioLib versions and was blocking forever.
 *
 *  So: startReceive() to arm the radio, then poll DIO1 in a millis() loop, then
 *  standby() to cancel cleanly when the window closes. That works on every
 *  RadioLib version.
 *
 *  The GPS parser is fed on every tick. At 9600 baud the UART FIFO fills in
 *  about 130 ms, so a 400 ms window with no reads would drop NMEA sentences.
 *
 *  Returns nothing. The window still runs to completion — leaving early would
 *  shorten the cycle and break the fixed cadence — but the RELEASE no longer waits
 *  for it.
 *
 *  ⚠ Until 2026-09-09 this accumulated a bool and returned it at window close, and
 *  the caller drove the mechanism only then. A command heard at t=5 ms of the window
 *  was therefore not acted on until t=400 ms: up to ~395 ms of latency on the one
 *  path in this system where latency costs a parachute. The back-half hold had always
 *  fired per packet, inside its loop, so the two intake paths disagreed about when a
 *  release happens as well as about how it is counted (devlog 065 fault 2, counted
 *  half fixed in 067). Both halves now fire on receipt, through chuteFireFromUplink().
 *  See devlog 069.
 * ----------------------------------------------------------------------- */
void radioListenForEject(uint32_t windowMs) {
  /* Service before arming. The hold at the end of the previous cycle now leaves the
   * radio receiving, so a packet may already be waiting with DIO1 high — and
   * startReceive() would throw it away. */
  if (radioServiceUplink()) chuteFireFromUplink();
  else radioArmReceive();

  uint32_t windowEnd = millis() + windowMs;

  while ((int32_t)(windowEnd - millis()) > 0) {
    gpsFeed();

    /* Added 2026-09-09. This was the one poll loop in the cycle that did NOT service
     * the mechanism, which made chuteTick()'s own claim — deadlines met to within
     * LISTEN_TICK_MS — untrue for anything falling inside this 400 ms window. A release
     * driven early in the window had its return sweep deferred to holdUntilListening()
     * later in the same cycle, up to ~450 ms late.
     *
     * Harmless as the constants stand, because CHUTE_HOLD_MS is 1000 and the horn
     * simply dwelled longer. It stops being harmless the moment CHUTE_HOLD_MS is
     * tightened toward the width of this window, so the loop is corrected rather than
     * the comment. Cheap: two unsigned comparisons when there is nothing to do. */
    chuteTick();

    /* The apogee rule samples on its own clock too, for the same reason and from the
     * same four loops. Self-throttled to AUTO_EJECT_SAMPLE_MS, so this is one unsigned
     * comparison on most of the ~80 ticks in this window. See devlog 071. */
    apogeeTick();

    /* Keep listening for the rest of the window even after a hit: the ground station
     * retries until it sees the count rise, and counting every arrival is what
     * reports uplink quality.
     *
     * Driven HERE, on the tick it is heard, rather than accumulated and acted on at
     * window close. chuteFire() is idempotent behind its latch, so the four remaining
     * attempts of one operator burst landing later in this same window cost a call
     * each and move nothing. */
    if (radioServiceUplink()) chuteFireFromUplink();

    delay(LISTEN_TICK_MS);
  }

  /* Deliberately NOT standby(). The radio stays armed across the sensor read, and is
   * re-armed immediately after the transmit, so the only deaf stretch in the cycle is
   * the transmit itself.
   *
   * ⚠ "Armed across the sensor read" is only useful if something READS what arrives
   * there. Nothing did until 2026-09-09 — see the service call after sensorsRead() in
   * the main sketch, and devlog 069. */
}
