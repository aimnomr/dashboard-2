/* ============================================================================
 *  Storage — SD card logging.
 *
 *  Open, append, close on every write. Slower than holding the file open, and
 *  chosen deliberately: a power loss at any instant costs at most the line in
 *  flight. The card is the backup that has to survive the landing.
 *
 *  SD owns HSPI. LoRa stays on the default SPI bus — GEN1's layout, and the only
 *  arrangement that has ever run both peripherals together on real hardware.
 * ========================================================================= */

static String logFile = "";

/* First unused FLIGHTnn.CSV wins, so a session never overwrites an earlier one. */
static String nextFilename() {
  for (int i = 1; i <= SD_MAX_FILES; i++) {
    char name[16];
    snprintf(name, sizeof(name), "/FLIGHT%02d.CSV", i);
    if (!SD.exists(name)) return String(name);
  }
  return String("/FLIGHT99.CSV");
}

/* Returns false if the card is unusable. Non-fatal by design: a missing card
 * must not stop a flight, because the downlink is the primary record. */
bool storageBegin() {
#if !ENABLE_SD
  return false;
#else
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("[FLT] SD init failed - continuing without it");
    displayMessage("SD Card FAILED", "Continuing", "without SD", "");
    delay(1500);
    return false;
  }

  logFile = nextFilename();
  File f = SD.open(logFile, FILE_WRITE);
  if (!f) {
    Serial.println("[FLT] SD file create failed - continuing without it");
    return false;
  }

  /* The log holds complete GEN3 packets, framing and checksum included, so the
   * card is a byte-faithful record of what was transmitted and can be replayed
   * through the same parser as the downlink. A bare CSV header would describe
   * something this file does not contain. */
  f.println("# MRC CanSat GEN3 flight log");
  f.println("# $" TEAM_ID ",seq,ms,temp,hum,pres,alt,ax,ay,az,gx,gy,gz,lat,lng,spd,sat,chute*CRC16");
  f.close();

  Serial.print("[FLT] logging to ");
  Serial.println(logFile);
  return true;
#endif
}

void storageWrite(const char *line) {
  if (!sdReady) return;

  File f = SD.open(logFile, FILE_APPEND);
  if (!f) {
    Serial.println("[FLT] SD write error");
    return;
  }
  f.println(line);
  f.close();
}
