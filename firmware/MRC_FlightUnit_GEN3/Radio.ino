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

  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[FLT] startReceive failed, code ");
    Serial.println(state);
    holdUntil(millis() + windowMs);      /* still burn the window: cadence first */
    return false;
  }

  uint32_t windowEnd = millis() + windowMs;

  while ((int32_t)(windowEnd - millis()) > 0) {
    gpsFeed();

    if (digitalRead(LORA_DIO1) == HIGH) {
      String incoming;
      int rxState = radio.readData(incoming);

      if (rxState == RADIOLIB_ERR_NONE) {
        incoming.trim();

        if (incoming.equals(EJECT_TOKEN)) {
          heardEject = true;
          lastUplinkMs = millis();
          uplinkHeard  = true;
          /* Keep listening for the rest of the window. The ground station
           * retries once per cycle until it sees the count rise, and counting
           * every arrival is what reports uplink quality. */

        } else if (incoming.equals(PING_TOKEN)) {
          /* Link test. Proves the uplink works without touching the chute —
           * this is the check that can safely be run on the pad. */
          pingCount++;
          lastUplinkMs = millis();
          uplinkHeard  = true;
          Serial.print("[FLT] PING received, count ");
          Serial.println(pingCount);
        }
        /* Anything else is another team's traffic. Ignored, not counted as
         * uplink: a foreign packet must never look like our ground station. */
      }
      /* Re-arm for the remainder of the window, whether that packet was ours,
       * another team's, or a failed read. */
      radio.startReceive();
    }

    delay(LISTEN_TICK_MS);
  }

  radio.standby();
  return heardEject;
}
