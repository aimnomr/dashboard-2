/* ============================================================================
 *  Display — OLED status.
 *
 *  The operator watches the dashboard, not this screen. Its job is to answer one
 *  question while standing next to the unit with no laptop: is this thing alive
 *  and hearing the vehicle?
 *
 *  Pushing a 1024-byte buffer over I2C is slow, so it is throttled. It must
 *  never be the reason a packet or a command waits.
 * ========================================================================= */

static uint32_t lastDisplayMs = 0;

void displayBegin() {
#if ENABLE_OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();
  oled.setBusClock(400000);   /* default is 100 kHz; a full buffer push there costs ~90 ms */
  oled.setFont(u8g2_font_6x10_tf);
#endif
}

void displaySplash() {
#if ENABLE_OLED
  oled.clearBuffer();
  oled.drawStr(0, 10, "MRC GROUND GEN3");
  oled.drawLine(0, 13, 128, 13);
  oled.drawStr(0, 26, "Initialising...");
  oled.sendBuffer();
#endif
}

void displayFatal(const char *line1, const char *line2) {
#if ENABLE_OLED
  oled.clearBuffer();
  oled.drawStr(0, 10, "MRC GROUND GEN3");
  oled.drawLine(0, 13, 128, 13);
  oled.drawStr(0, 30, line1);
  oled.drawStr(0, 44, line2);
  oled.drawStr(0, 60, "HALTED");
  oled.sendBuffer();
#endif
}

void displayUpdate() {
#if ENABLE_OLED
  if (millis() - lastDisplayMs < OLED_MIN_INTERVAL_MS) return;
  lastDisplayMs = millis();

  char l1[24], l2[24], l3[24], l4[24];

  /* Packet age first. A ground station that has gone deaf looks identical to one
   * watching a quiet vehicle, and the difference matters most when it is worst. */
  if (lastPacketMs == 0) {
    snprintf(l1, sizeof(l1), "RX:0  waiting...");
  } else {
    uint32_t age = (millis() - lastPacketMs) / 1000;
    if (age > 999) age = 999;
    snprintf(l1, sizeof(l1), "RX:%lu  %lus ago",
             (unsigned long)packetsOurs, (unsigned long)age);
  }

  snprintf(l2, sizeof(l2), "RSSI%4.0f SNR%5.1f", lastRssi, lastSnr);

  if (packetsBadCrc > 0) {
    snprintf(l3, sizeof(l3), "BADCRC%lu FGN%lu",
             (unsigned long)packetsBadCrc, (unsigned long)packetsForeign);
  } else if (lastChute >= 1) {
    snprintf(l3, sizeof(l3), "CHUTE CMD x%d", lastChute);
  } else {
    snprintf(l3, sizeof(l3), "Chute armed  FGN%lu", (unsigned long)packetsForeign);
  }

  if (ejectConfirmed) {
    snprintf(l4, sizeof(l4), "EJECT CONFIRMED");
  } else if (ejectPending) {
    snprintf(l4, sizeof(l4), "EJECT %d/%d", ejectAttempts, EJECT_MAX_ATTEMPTS);
  } else if (pingPending) {
    snprintf(l4, sizeof(l4), "PING queued...");
  } else if (pingsSent > 0) {
    snprintf(l4, sizeof(l4), "%s  ping x%lu", TEAM_ID, (unsigned long)pingsSent);
  } else {
    snprintf(l4, sizeof(l4), "%s", TEAM_ID " listening");
  }

  oled.clearBuffer();
  oled.drawStr(0, 10, "MRC GROUND GEN3");
  oled.drawLine(0, 13, 128, 13);
  oled.drawStr(0, 26, l1);
  oled.drawStr(0, 38, l2);
  oled.drawStr(0, 50, l3);
  oled.drawStr(0, 62, l4);
  oled.sendBuffer();
#endif
}
