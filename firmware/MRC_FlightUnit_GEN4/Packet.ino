/* ============================================================================
 *  Packet — GEN3 framing and checksum.
 *
 *  GEN3.1, from 2026-08-20:
 *  $MRC,seq,ms,temp,hum,pres,alt,ax,ay,az,gx,gy,gz,lat,lng,spd,sat,chute,ul,hdop,fixq*CRC16
 *
 *  The three new fields are APPENDED, never inserted. Every existing field keeps its
 *  index, so a 17-field parser and a 20-field parser disagree only about how much they
 *  can see — which is what lets the GEN3.0 regression corpus (FLIGHT21/22.CSV) keep
 *  pinning this code unchanged. Inserting `hdop` next to `sat`, where it belongs
 *  semantically, would have shifted `chute` and broken every consumer at once.
 *
 *  The ground station appends ",rssi,snr" AFTER the checksum, so this checksum
 *  covers exactly what left the vehicle. That is what makes corruption over the
 *  RF hop detectable; a checksum recomputed downstream would only prove the USB
 *  cable worked.
 *
 *  Field formats are matched to what the sensors actually resolve — see
 *  wiki/decisions/gen3-packet-format.md. Do not widen them without a reason:
 *  every character is airtime, and airtime is collision exposure when other
 *  teams share the band.
 * ========================================================================= */

/* CRC16/CCITT-FALSE — poly 0x1021, init 0xFFFF, no reflection, no final xor.
 * The ground station and the dashboard both reimplement this; all three must
 * agree byte for byte. firmware/tests/verify_gen3.py is the reference. */
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

void packetBuild(char *out, size_t cap, const Telemetry &t,
                 uint32_t seq, uint32_t ms, uint32_t chute, uint32_t ul) {
  char body[PACKET_BUF];

  snprintf(body, sizeof(body),
    "%s,%lu,%lu,"                 /* team, seq, ms                */
    "%.2f,%.1f,%.2f,%.1f,"        /* temp, hum, pres, alt         */
    "%.3f,%.3f,%.3f,"             /* ax, ay, az                   */
    "%.2f,%.2f,%.2f,"             /* gx, gy, gz                   */
    "%.5f,%.5f,%.1f,%d,"          /* lat, lng, spd, sat           */
    "%lu,"                        /* chute: 0 armed, N commanded  */
    "%lu,%.1f,%d",                /* ul, hdop, fixq  (GEN3.1)     */
    TEAM_ID,
    (unsigned long)seq,
    (unsigned long)ms,
    t.temp, t.hum, t.pres, t.alt,
    t.ax, t.ay, t.az,
    t.gx, t.gy, t.gz,
    t.lat, t.lng, t.spd, t.sat,
    (unsigned long)chute,
    (unsigned long)ul, t.hdop, t.fixq);

  snprintf(out, cap, "$%s*%04X", body, crc16Ccitt(body, strlen(body)));
}
