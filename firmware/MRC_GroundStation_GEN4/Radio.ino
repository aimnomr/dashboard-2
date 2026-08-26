/* ============================================================================
 *  Radio — receive, classify, forward.
 * ========================================================================= */

/* Returns true if the radio came up. */
bool radioBegin() {
  int state = radio.begin(FREQ_MHZ, BANDWIDTH_KHZ, SPREADING,
                          CODING_RATE, SYNC_WORD, TX_POWER_DBM);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[GCS] radio.begin failed, code ");
    Serial.println(state);
    return false;
  }
  return true;
}

/* --------------------------------------------------------------------------
 *  CRC16/CCITT-FALSE  — poly 0x1021, init 0xFFFF, no reflection, no final xor.
 *
 *  The vehicle computes this over everything between '$' and '*'. We recompute
 *  it for ONE purpose only: deciding whether the chute field can be trusted to
 *  stop the retry loop. A corrupted packet with a garbage chute value would
 *  otherwise stop us retrying against a vehicle that never heard the command.
 *
 *  Forwarding to the PC is verbatim either way — the dashboard does its own
 *  check and must be able to see corruption.
 * ----------------------------------------------------------------------- */
uint16_t crc16Ccitt(const char *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* Verify "$...*XXXX". Returns true when the checksum matches.
 * Plain C throughout: Arduino's String allocates, and a long flight is exactly
 * where heap fragmentation would surface. */
bool packetCrcValid(const char *packet) {
  if (packet[0] != '$') return false;

  const char *star = strrchr(packet, '*');
  if (star == NULL || strlen(star + 1) < 4) return false;

  char hex[5];
  memcpy(hex, star + 1, 4);
  hex[4] = '\0';
  uint16_t expected = (uint16_t)strtoul(hex, NULL, 16);

  return crc16Ccitt(packet + 1, (size_t)(star - packet - 1)) == expected;
}

/* The chute counter, by INDEX rather than by position from the end.
 *
 * This used to walk back from '*' and take the last field, which was correct while
 * chute WAS last. GEN3.1 appends ul, hdop and fixq after it (devlog 048), so that
 * version would now return fixq — which is -1 or 0 in exactly the situations where a
 * lost GPS fix coincides with a real deployment. The eject burst would have gone on
 * firing at a vehicle that had already deployed, and the OLED would have said "armed".
 *
 * Counting forward is also robust to the NEXT field being appended, which is precisely
 * the failure this one was: a rule that depended on nothing ever changing.
 *
 * Field 0 is the team marker, so chute is index 17 and ul is index 18.
 * Returns -1 if it cannot be read.
 *
 * GEN4 generalised the walk into parseFieldInt() rather than copying it: `ul` is
 * read the same way by the same code. Two near-identical parsers is how the
 * walk-back bug described above survived a format change in the first place. */
#define CHUTE_FIELD_INDEX 17
#define UL_FIELD_INDEX    18

static int parseFieldInt(const char *packet, int index) {
  const char *star = strrchr(packet, '*');
  if (star == NULL) return -1;

  const char *p = packet;
  for (int field = 0; field < index; field++) {
    p = strchr(p, ',');
    if (p == NULL || p >= star) return -1;    /* fewer fields than promised */
    p++;
  }

  const char *end = strchr(p, ',');
  if (end == NULL || end > star) end = star;  /* last field before the checksum */

  size_t len = (size_t)(end - p);
  if (len == 0 || len > 10) return -1;        /* 10 digits covers a uint32 */

  char buf[11];
  memcpy(buf, p, len);
  buf[len] = '\0';
  return atoi(buf);
}

int parseChute(const char *packet) { return parseFieldInt(packet, CHUTE_FIELD_INDEX); }

/* `ul` — the vehicle's count of uplink commands received. GEN4 uses it to confirm
 * SET and RESET, exactly as `chute` confirms EJECT.
 *
 * Returns -1 against a GEN3.0 vehicle, which has no field 18. That is the correct
 * degradation: the config burst then runs every attempt and reports that it could
 * not confirm, rather than inferring success from a field that does not exist. */
int parseUl(const char *packet) { return parseFieldInt(packet, UL_FIELD_INDEX); }


/* --------------------------------------------------------------------------
 *  Called every tick. Non-blocking: DIO1 goes high when a packet is fully
 *  received, and the SX1262 holds it until read, so nothing is lost by checking
 *  on a 5 ms tick rather than sitting in a blocking call.
 * ----------------------------------------------------------------------- */
void radioPoll() {
  if (!loraReady) return;
  if (digitalRead(LORA_DIO1) != HIGH) return;

  String received;
  int state = radio.readData(received);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[GCS] RX error code ");
    Serial.println(state);
    radio.startReceive();
    return;
  }

  lastRssi = radio.getRSSI();
  lastSnr  = radio.getSNR();
  received.trim();

  /* Not ours. Another team, or noise that happened to decode. Count it and say
   * so — this is the line that separates "the band is busy" from "my vehicle is
   * silent", which are otherwise indistinguishable. See ISS-13. */
  if (!received.startsWith(PACKET_PREFIX)) {
    packetsForeign++;
    Serial.print("[GCS] foreign packet, ");
    Serial.print(packetsForeign);
    Serial.println(" so far");
    radio.startReceive();
    return;
  }

  /* Ours. Forward verbatim with link quality appended, whatever its condition. */
  char out[PACKET_BUF];
  snprintf(out, sizeof(out), "%s,%.1f,%.1f", received.c_str(), lastRssi, lastSnr);
  Serial.println(out);
  packetsOurs++;
  lastPacketMs = millis();

  /* Only now, and only for the retry loop, do we look inside. */
  if (packetCrcValid(received.c_str())) {
    lastChute = parseChute(received.c_str());
    lastUl    = parseUl(received.c_str());
  } else {
    packetsBadCrc++;
    /* Deliberately leave lastChute alone: a failed checksum tells us nothing
     * about the chute, and guessing could stop the retries early. */
  }

  /* The vehicle transmits, then immediately opens its listen window. This is
   * that moment — the single best time in the whole cycle to send a command. */
  uplinkOnPacketReceived();

  radio.startReceive();
}

/* Transmit an uplink token. Blocks for roughly 41 ms at SF7, which is
 * negligible: the next telemetry packet is about a second away. Receive mode is
 * always restored, so a failed transmit cannot leave this unit deaf. */
bool radioTransmit(const char *token) {
  int state = radio.transmit(token);
  radio.startReceive();
  return state == RADIOLIB_ERR_NONE;
}
