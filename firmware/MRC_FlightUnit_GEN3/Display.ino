/* ============================================================================
 *  Display — OLED.
 *
 *  THIS IS THE PRE-LAUNCH INSTRUMENT. At the pad the unit is sealed, powered and
 *  nowhere near a laptop, so this glass is the only way to know it is working.
 *  Everything vital has to be readable in one glance, without scrolling and
 *  without waiting for a page to rotate.
 *
 *  The five checks it must answer:
 *      1. Is it alive and cycling?          -> sequence number climbing
 *      2. Are the sensors real?             -> accel magnitude near 1.00 g
 *      3. Is calibration good?              -> gyro bias figure
 *      4. Does it have a fix, and is it logging?
 *      5. Is the uplink working?            -> time since last PING/EJECT
 *
 *  GEN1 rotated three screens every five seconds and re-read the sensors inside
 *  each one. Rotation is wrong here: you look up at the moment you look up, and
 *  the page you need may be four seconds away. One dense page instead, built
 *  from the same snapshot the packet was made from — so what is on the glass is
 *  exactly what went on the air.
 *
 *  Refreshed every OLED_EVERY_N cycles: a 1024-byte buffer push takes ~25 ms at
 *  400 kHz and roughly 90 ms at the 100 kHz default, hence setBusClock() below.
 * ========================================================================= */

void displayBegin() {
#if ENABLE_OLED
  oled.begin();
  oled.setBusClock(400000);
  oled.setFont(u8g2_font_6x10_tf);   /* 21 characters per line at 128 px */
#endif
}

void displayMessage(const char *l1, const char *l2, const char *l3, const char *l4) {
#if ENABLE_OLED
  oled.clearBuffer();
  if (l1 && *l1) oled.drawStr(0, 10, l1);
  oled.drawLine(0, 13, 128, 13);
  if (l2 && *l2) oled.drawStr(0, 28, l2);
  if (l3 && *l3) oled.drawStr(0, 42, l3);
  if (l4 && *l4) oled.drawStr(0, 56, l4);
  oled.sendBuffer();
#endif
}

void displayTelemetry(const Telemetry &t) {
#if ENABLE_OLED
  char l1[24], l2[24], l3[24], l4[24], l5[24];

  /* 1 — alive. A frozen sequence number is the fastest way to spot a hang. */
  snprintf(l1, sizeof(l1), "GEN3 #%lu", (unsigned long)seqNumber);

  /* 2 & 3 — are the sensors real, and is calibration sound?
   * Accel magnitude is the single best IMU sanity check: at rest it must read
   * close to 1.00 g whatever the orientation. 0.00 means a dead sensor, and
   * anything far off means it is being moved or the ranges are wrong. */
  float g = sqrtf(t.ax * t.ax + t.ay * t.ay + t.az * t.az);
  snprintf(l2, sizeof(l2), "IMU %.2fg  bias%4.1f", g, gyroBiasWorst);

  /* 4 — fix and logging, on one line.
   *
   * "NO DATA" and "no fix" are different faults and must not look alike. No data
   * means the module is not reaching the ESP32 at all — wiring or baud — and no
   * amount of standing outside will fix it. */
  if (!gpsHasData()) {
    snprintf(l3, sizeof(l3), "GPS NO DATA  %s", sdReady ? "SD ok" : "SD --");
  } else if (t.sat > 0 && (t.lat != 0.0 || t.lng != 0.0)) {
    snprintf(l3, sizeof(l3), "GPS %2dsat %s", t.sat, sdReady ? "SD ok" : "SD --");
  } else {
    snprintf(l3, sizeof(l3), "GPS no fix(%d) %s", t.sat, sdReady ? "SD ok" : "SD --");
  }

  /* Primary flight values. */
  snprintf(l4, sizeof(l4), "Alt%6.1fm  T%4.1fC", t.alt, t.temp);

  /* 5 — chute state and uplink health, the two things that decide whether this
   * unit is safe and controllable. "CMD" not "DEPLOYED": nothing on board can
   * confirm the canopy opened, and that is as true of an automatic release as a
   * commanded one — "AUTO" below means the rule fired, not that a canopy opened.
   *
   * "ARMED+A" is the chute unfired with auto-eject armed behind it. Two different
   * senses of "armed" share this line: the chute is armed until it fires, and the
   * auto-eject rule is armed once the vehicle has climbed past its floor. On the
   * pad the correct reading is a bare "ARMED" — a "+A" before launch would mean
   * the arming altitude had been crossed while the unit sat there. */
  char chuteStr[12];
  if (chuteCommands > 0) {
    /* "AUTO" vs "CMD" is the only place the CAUSE of a release is visible at the
     * pad. The packet carries one chute counter for both paths by design, so if
     * this glass does not say which fired, nothing does until the log is read. */
    snprintf(chuteStr, sizeof(chuteStr), "%s x%lu",
             apogeeDidFire() ? "AUTO" : "CMD", (unsigned long)chuteCommands);
  } else {
    snprintf(chuteStr, sizeof(chuteStr), "ARMED%s", apogeeIsArmed() ? "+A" : "");
  }

  if (uplinkHeard) {
    uint32_t age = (millis() - lastUplinkMs) / 1000;
    if (age > 999) age = 999;
    snprintf(l5, sizeof(l5), "%-9s UL %lus", chuteStr, (unsigned long)age);
  } else {
    /* Never heard the ground station. Before launch this must not stay "--". */
    snprintf(l5, sizeof(l5), "%-9s UL --", chuteStr);
  }

  oled.clearBuffer();
  oled.drawStr(0, 9,  l1);
  oled.drawLine(0, 12, 128, 12);
  oled.drawStr(0, 24, l2);
  oled.drawStr(0, 36, l3);
  oled.drawStr(0, 48, l4);
  oled.drawStr(0, 62, l5);

  /* An un-proven uplink is the one pre-launch fault that is invisible in the
   * numbers, so it gets a marker rather than a value nobody notices. */
  if (!uplinkHeard) oled.drawStr(112, 9, "!");

  oled.sendBuffer();
#endif
}
