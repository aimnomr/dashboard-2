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

/* Last comma-separated field before '*' is the chute counter.
 * Returns -1 if it cannot be read. */
int parseChute(const char *packet) {
  const char *star = strrchr(packet, '*');
  if (star == NULL) return -1;

  const char *comma = star;
  while (comma > packet && *comma != ',') comma--;
  if (*comma != ',') return -1;

  size_t len = (size_t)(star - comma - 1);
  if (len == 0 || len > 5) return -1;

  char buf[6];
  memcpy(buf, comma + 1, len);
  buf[len] = '\0';
  return atoi(buf);
}

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

  /* Only now, and only for the retry loop, do we look inside. */
  if (packetCrcValid(received.c_str())) {
    lastChute = parseChute(received.c_str());
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

/* Transmit the eject token. Blocks for roughly 41 ms at SF7, which is
 * negligible: the next telemetry packet is about a second away. */
bool radioTransmitEject() {
  int state = radio.transmit(EJECT_TOKEN);
  radio.startReceive();
  return state == RADIOLIB_ERR_NONE;
}
