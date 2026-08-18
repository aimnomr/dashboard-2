/* ============================================================================
 *  Uplink — command intake from the PC, and the eject retry loop.
 *
 *  Retry strategy: transmit once per received telemetry packet, because that is
 *  when the vehicle opens its listen window. A blind burst has a 40% chance per
 *  shot and leaves this unit deaf for over a second; timing it to follow a
 *  received packet is near-certain and costs 41 ms.
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
}

void handleCommand(const char *line) {
  if (strcmp(line, CMD_EJECT) == 0) {
    if (ejectConfirmed) {
      Serial.println("[GCS] EJECT already confirmed, ignoring");
      return;
    }
    if (ejectPending) {
      Serial.println("[GCS] EJECT already armed");
      return;
    }
    ejectPending   = true;
    ejectConfirmed = false;
    ejectAttempts  = 0;
    Serial.println("[GCS] EJECT armed");
    return;
  }

  Serial.print("[GCS] unknown command: ");
  Serial.println(line);
}

/* --------------------------------------------------------------------------
 *  Called from Radio.ino immediately after a telemetry packet is forwarded.
 *  The vehicle's listen window has just opened.
 * ----------------------------------------------------------------------- */
void uplinkOnPacketReceived() {
  if (!ejectPending || ejectConfirmed) return;

  /* Confirmation first: the packet we just received may already carry it, in
   * which case transmitting again would be pointless noise on a shared band.
   *
   * NOTE: chute >= 1 means the vehicle received the command and drove the
   * servo. It does NOT mean the parachute opened — there is no feedback sensor.
   * Nothing downstream may claim otherwise. */
  if (lastChute >= 1) {
    ejectConfirmed = true;
    ejectPending   = false;
    Serial.print("[GCS] EJECT confirmed after ");
    Serial.print(ejectAttempts);
    Serial.println(" attempt(s)");
    return;
  }

  if (ejectAttempts >= EJECT_MAX_ATTEMPTS) {
    ejectPending = false;
    Serial.print("[GCS] EJECT gave up after ");
    Serial.println(EJECT_MAX_ATTEMPTS);
    return;
  }

  ejectAttempts++;
  bool sent = radioTransmitEject();

  Serial.print("[GCS] EJECT attempt ");
  Serial.print(ejectAttempts);
  Serial.print("/");
  Serial.print(EJECT_MAX_ATTEMPTS);
  Serial.println(sent ? "" : " FAILED TO TRANSMIT");
}
