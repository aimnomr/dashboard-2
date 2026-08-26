/* ============================================================================
 *  Radio — LoRa init and the receive window.
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
 *  Returns true if EJECT arrived. The window still runs to completion either
 *  way — leaving early would shorten the cycle and break the fixed cadence.
 * ----------------------------------------------------------------------- */
bool radioListenForEject(uint32_t windowMs) {
  bool heardEject = false;

  /* Service before arming. The hold at the end of the previous cycle now leaves the
   * radio receiving, so a packet may already be waiting with DIO1 high — and
   * startReceive() would throw it away. */
  if (radioServiceUplink()) heardEject = true;
  else radioArmReceive();

  uint32_t windowEnd = millis() + windowMs;

  while ((int32_t)(windowEnd - millis()) > 0) {
    gpsFeed();

    /* Keep listening for the rest of the window even after a hit: the ground station
     * retries until it sees the count rise, and counting every arrival is what
     * reports uplink quality. */
    if (radioServiceUplink()) heardEject = true;

    delay(LISTEN_TICK_MS);
  }

  /* Deliberately NOT standby(). The radio stays armed across the sensor read, and is
   * re-armed immediately after the transmit, so the only deaf stretch in the cycle is
   * the transmit itself. */
  return heardEject;
}
