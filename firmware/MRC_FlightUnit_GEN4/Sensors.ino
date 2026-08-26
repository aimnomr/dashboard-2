/* ============================================================================
 *  Sensors — BME280, MPU6050, GPS.
 *
 *  MPU6050 is driven by raw register access, carried over from GEN1 unchanged.
 *  The register values and the scale factors only make sense together: 0x1C=0x10
 *  selects +/-8 g, which is why the divisor is 4096; 0x1B=0x08 selects +/-500
 *  deg/s, hence 65.5. Change one and you must change the other.
 * ========================================================================= */

static float gyroXoffset = 0, gyroYoffset = 0, gyroZoffset = 0;
static float accelXoffset = 0, accelYoffset = 0;
static float baseAltitude = 0;
static bool  altitudeZeroed = false;

static float gpsSpeedOffset = 0;
static bool  gpsSpeedCalibrated = false;

/* ---- MPU6050 raw register access ------------------------------------------ */

void mpuWrite(byte reg, byte data) {
  I2C_SENS.beginTransmission(MPU_ADDR);
  I2C_SENS.write(reg);
  I2C_SENS.write(data);
  I2C_SENS.endTransmission(true);
}

void mpuRead(byte reg, byte *buf, int len) {
  I2C_SENS.beginTransmission(MPU_ADDR);
  I2C_SENS.write(reg);
  I2C_SENS.endTransmission(false);
  I2C_SENS.requestFrom((uint16_t)MPU_ADDR, (uint8_t)len, true);
  for (int i = 0; i < len; i++) buf[i] = I2C_SENS.read();
}

/* Burst-read accel and gyro. Bytes 6-7 are the die temperature and are skipped. */
void mpuRawReadings(float &ax, float &ay, float &az,
                    float &gx, float &gy, float &gz) {
  byte buf[14];
  mpuRead(0x3B, buf, 14);
  ax = ((int16_t)((buf[0]  << 8) | buf[1]))  / MPU_ACCEL_SCALE;
  ay = ((int16_t)((buf[2]  << 8) | buf[3]))  / MPU_ACCEL_SCALE;
  az = ((int16_t)((buf[4]  << 8) | buf[5]))  / MPU_ACCEL_SCALE;
  gx = ((int16_t)((buf[8]  << 8) | buf[9]))  / MPU_GYRO_SCALE;
  gy = ((int16_t)((buf[10] << 8) | buf[11])) / MPU_GYRO_SCALE;
  gz = ((int16_t)((buf[12] << 8) | buf[13])) / MPU_GYRO_SCALE;
}

/* ---- init ------------------------------------------------------------------ */

void sensorsBegin() {
  I2C_SENS.begin(I2C_SDA, I2C_SCL);
  delay(250);

  if (!bme.begin(BME_ADDR, &I2C_SENS)) {
    Serial.println("[FLT] BME280 not responding - halted");
    displayMessage("BME280 Error!", "Check wiring", "HALTED", "");
    while (true) delay(1000);
  }
  Serial.println("[FLT] BME280 ready");

  mpuWrite(0x6B, 0x00);            /* wake */
  delay(200);

  byte who[1];
  mpuRead(0x75, who, 1);
  if (who[0] == 0x00 || who[0] == 0xFF) {
    Serial.println("[FLT] MPU6050 not responding - halted");
    displayMessage("MPU6050 Error!", "Check wiring", "HALTED", "");
    while (true) delay(1000);
  }
  mpuWrite(0x1C, MPU_ACCEL_RANGE);
  mpuWrite(0x1B, MPU_GYRO_RANGE);
  Serial.println("[FLT] MPU6050 ready");

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("[FLT] GPS serial open");
}

/* ---- calibration ----------------------------------------------------------- */

void sensorsCalibrate() {
  displayMessage("MPU Calibrating", "Keep the unit", "FLAT & STILL", "");
  Serial.println("[FLT] MPU calibration - keep flat and still");

  float sumGx = 0, sumGy = 0, sumGz = 0, sumAx = 0, sumAy = 0;
  for (int i = 0; i < MPU_CAL_SAMPLES; i++) {
    float ax, ay, az, gx, gy, gz;
    mpuRawReadings(ax, ay, az, gx, gy, gz);
    sumGx += gx; sumGy += gy; sumGz += gz;
    sumAx += ax; sumAy += ay;

    if (i % 50 == 0) {
      char prog[24];
      snprintf(prog, sizeof(prog), "Progress: %d%%", (i * 100) / MPU_CAL_SAMPLES);
      displayMessage("MPU Calibrating", "FLAT & STILL", prog, "");
    }
    delay(MPU_CAL_DELAY_MS);
  }

  gyroXoffset  = sumGx / MPU_CAL_SAMPLES;
  gyroYoffset  = sumGy / MPU_CAL_SAMPLES;
  gyroZoffset  = sumGz / MPU_CAL_SAMPLES;
  accelXoffset = sumAx / MPU_CAL_SAMPLES;
  accelYoffset = sumAy / MPU_CAL_SAMPLES;

  /* az is deliberately NOT offset-corrected: at rest it reads +1 g, which is
   * signal rather than bias. Carried over from GEN1. */

  Serial.print("[FLT] gyro offsets  x=");  Serial.print(gyroXoffset, 3);
  Serial.print(" y=");                     Serial.print(gyroYoffset, 3);
  Serial.print(" z=");                     Serial.println(gyroZoffset, 3);
  Serial.print("[FLT] accel offsets x=");  Serial.print(accelXoffset, 3);
  Serial.print(" y=");                     Serial.println(accelYoffset, 3);

  /* A large residual on any axis means the unit moved during calibration. Worth
   * saying out loud: a 4 deg/s bias integrates to a full turn over a flight,
   * and exactly that was measured on real hardware. Surfaced on the OLED too —
   * nobody is watching a serial monitor at the pad. */
  gyroBiasWorst = max(fabs(gyroXoffset), max(fabs(gyroYoffset), fabs(gyroZoffset)));
  if (gyroBiasWorst > 5.0f) {
    Serial.print("[FLT] WARNING large gyro offset ");
    Serial.print(gyroBiasWorst, 1);
    Serial.println(" dps - was the unit still?");
  }

  baseAltitude   = bme.readAltitude(SEA_LEVEL_HPA);
  altitudeZeroed = true;
  Serial.print("[FLT] altitude zeroed at ");
  Serial.print(baseAltitude, 1);
  Serial.println(" m");

  displayMessage("Calibration", "Complete", "", "");
  delay(800);
}

/* ---- GPS ------------------------------------------------------------------- */

/* Is the receiver talking to us at all?
 *
 * "No characters" and "characters but no fix" are completely different faults
 * and must never look the same:
 *
 *   chars == 0            -> wiring or baud. The module is not reaching us.
 *   chars > 0, fix == 0   -> wiring is fine. Antenna, sky view, or cold start.
 *   failed checksums high -> data is arriving corrupted: baud mismatch or a
 *                            bad ground.
 *
 * Cheap to expose and it removes an entire afternoon of guessing.
 */
bool gpsHasData()        { return gps.charsProcessed() > 0; }
uint32_t gpsChars()      { return gps.charsProcessed(); }
uint32_t gpsFixSentences() { return gps.sentencesWithFix(); }
uint32_t gpsBadChecksums() { return gps.failedChecksum(); }

/* The module's own fix verdict, from GGA field 6.
 *
 *   -1  never reported — this module does not send GGA, or has not yet
 *    0  the receiver says the fix is INVALID
 *    1  GPS fix        2  DGPS fix
 *
 * Returning -1 rather than 0 for "never reported" is the point of this function.
 * A module that does not send the field must not be able to veto a position, or
 * swapping the GPS for one with a different sentence set would silently blank the
 * track. Only an explicit 0 is a rejection.
 *
 * Age-checked for the same reason everything else here now is: TinyGPSPlus's
 * isValid() latches true forever, so a module that stopped talking would otherwise
 * keep vouching for a fix it can no longer see. See devlog 046. */
/* HDOP from GGA field 8, or 0.0 when the module never reported it.
 *
 * 0.0 is a safe sentinel: a real HDOP is never zero — 1.0 is a perfect-geometry
 * fix and anything under 1 is rare — so zero cannot be mistaken for a good value.
 * Age-checked like everything else here; see devlog 046 for why. */
float gpsHdop() {
  TinyGPSCustom *terms[2] = { &ggaHdopGp, &ggaHdopGn };

  for (uint8_t i = 0; i < 2; i++) {
    if (terms[i]->isValid() && terms[i]->age() < GPS_FIX_MAX_AGE_MS) {
      float v = atof(terms[i]->value());
      if (v > 0.0f) return v;
    }
  }
  return 0.0f;
}

int gpsFixQuality() {
  TinyGPSCustom *terms[2] = { &ggaQualityGp, &ggaQualityGn };
  int best = -1;

  for (uint8_t i = 0; i < 2; i++) {
    if (terms[i]->isValid() && terms[i]->age() < GPS_FIX_MAX_AGE_MS) {
      int v = atoi(terms[i]->value());
      if (v > best) best = v;
    }
  }
  return best;
}

/* Drain the UART into the parser. Must be called often — at 9600 baud the FIFO
 * fills in roughly 130 ms. */
void gpsFeed() {
  while (GPSSerial.available() > 0) {
    gps.encode(GPSSerial.read());
  }

#if ENABLE_GPS_SPEED_CAL
  /* GEN1 captured this inside the GPS *display* function, so it only ran once
   * that OLED screen had rotated into view. It belongs here, and it now waits
   * for a usable fix rather than taking the first reading from a one-satellite
   * solution. */
  if (!gpsSpeedCalibrated &&
      gps.speed.isValid() &&
      gps.satellites.isValid() &&
      gps.satellites.value() >= GPS_CAL_MIN_SATS) {
    gpsSpeedOffset = gps.speed.kmph();
    gpsSpeedCalibrated = true;
    Serial.print("[FLT] GPS speed offset ");
    Serial.print(gpsSpeedOffset, 2);
    Serial.println(" km/h");
  }
#endif
}

/* ---- per-cycle read -------------------------------------------------------- */

void sensorsRead(Telemetry &t) {
  gpsFeed();

  t.temp = bme.readTemperature();
  t.hum  = bme.readHumidity();
  t.pres = bme.readPressure() / 100.0F;

  float alt = bme.readAltitude(SEA_LEVEL_HPA);
  t.alt = altitudeZeroed ? (alt - baseAltitude) : 0.0f;

  float ax, ay, az, gx, gy, gz;
  mpuRawReadings(ax, ay, az, gx, gy, gz);
  t.ax = ax - accelXoffset;
  t.ay = ay - accelYoffset;
  t.az = az;                       /* see note in sensorsCalibrate() */
  t.gx = gx - gyroXoffset;
  t.gy = gy - gyroYoffset;
  t.gz = gz - gyroZoffset;

  /* An invalid fix reports 0.0 rather than a stale position. The dashboard
   * treats exact zeros as "no fix" — a stale one would be plotted as real.
   *
   * That intent was right and isValid() did not deliver it. In TinyGPSPlus
   * isValid() latches true on the first successful parse and NEVER returns to
   * false: it means "has ever been valid", not "is valid now". After the fix
   * dropped on 2026-08-19 the vehicle transmitted its last known position for
   * 14 straight packets, frozen to the digit, while satellites read 0.
   *
   * Freshness is a separate question and age() is what answers it.
   *
   * Three conditions now, and the third is the receiver's own opinion. Age alone
   * catches a module that went quiet; it does NOT catch one that keeps emitting
   * GGA with the fix flag cleared, which is exactly what a receiver does the
   * moment it loses lock indoors. Only quality == 0 rejects — -1 means the field
   * was never reported, and a module that does not send it must not be able to
   * veto a position. */
  int  fixQuality = gpsFixQuality();
  bool fixFresh   = gps.location.isValid() &&
                    gps.location.age() < GPS_FIX_MAX_AGE_MS &&
                    fixQuality != 0;

  t.lat = fixFresh ? gps.location.lat() : 0.0;
  t.lng = fixFresh ? gps.location.lng() : 0.0;
  t.sat  = gps.satellites.isValid() ? gps.satellites.value() : 0;

  /* Reported whatever the fix state, and deliberately NOT gated on fixFresh: when
   * the position is withheld these two are the only things that say why. A ground
   * operator seeing 0,0 needs to tell "no signal" from "receiver says invalid"
   * from "module not talking", and that distinction lived on a dead OLED until now. */
  t.hdop = gpsHdop();
  t.fixq = fixQuality;

  /* Speed carries the same latch, and froze at 1.6 km/h in the same packets. */
  if (gps.speed.isValid() && gps.speed.age() < GPS_FIX_MAX_AGE_MS) {
    float raw = gps.speed.kmph();
    t.spd = gpsSpeedCalibrated ? max(0.0f, raw - gpsSpeedOffset) : raw;
  } else {
    t.spd = 0.0f;
  }
}
