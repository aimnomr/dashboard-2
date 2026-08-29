/* ============================================================================
 *  MRC CanSat — Ground Station GEN4
 *
 *  Receives telemetry over LoRa, appends link quality, forwards to the PC over
 *  USB serial. Accepts an eject command from the PC and relays it to the vehicle.
 *
 *  The whole unit is a pipe. It does not interpret telemetry beyond the one field
 *  it needs for the retry loop, and it forwards malformed packets verbatim so the
 *  operator sees corruption rather than a dashboard that has quietly gone still.
 *
 *  NOTHING HERE MAY BLOCK.  The GEN1 ground station used radio.receive(), which
 *  halts the CPU until a packet or timeout — so a command sitting in the serial
 *  buffer waited up to a second before anyone looked at it. That is why this
 *  version polls DIO1 and Serial on the same short tick.
 *
 *  Files:  Config.h  Radio.ino  Uplink.ino  Display.ino
 * ========================================================================= */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "Config.h"

/* ---- objects -------------------------------------------------------------- */
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

/* ---- shared state --------------------------------------------------------
 * Defined here, in the first translation unit Arduino concatenates, so every
 * tab can see them without a header dance.
 */
bool     loraReady      = false;

uint32_t packetsOurs    = 0;   /* telemetry from our vehicle, forwarded */
uint32_t packetsForeign = 0;   /* other teams, or noise that decoded */
uint32_t packetsBadCrc  = 0;   /* ours by marker, but the checksum failed */

float    lastRssi       = 0;
float    lastSnr        = 0;
int      lastChute      = -1;  /* -1 = not yet known */

/* GEN4: the vehicle's `ul` counter, packet field 18. The confirmation signal for
 * SET and RESET bursts — when this rises, the vehicle received the command.
 *
 * It does NOT say which command, or what value was applied. That is the accepted
 * cost of leaving the packet at GEN3.1: the operator learns "it heard something",
 * and the applied values are readable on the bench over USB, or after recovery
 * from the `#` config lines on the SD card. -1 until a valid packet carries it. */
int      lastUl         = -1;

uint32_t lastPacketMs   = 0;   /* 0 = nothing received yet */

/* uplink state — see Uplink.ino */
/* No `ejectPending` any more: the burst is synchronous, so there is no state to
 * carry between loop iterations. It was the retry loop's flag. See devlog 044. */
bool     ejectConfirmed = false;
uint8_t  ejectAttempts  = 0;

/* The value of `chute` above which a release counts as a NEW one.
 *
 * 0 from boot, so `lastChute > chuteBaseline` is identical to the old
 * `lastChute >= 1` on a fresh session — this changes nothing until RESET:CHUTE is
 * used.
 *
 * It exists because the vehicle deliberately does NOT zero its chute counter when
 * RESET:CHUTE clears the fire latch (Apogee.ino: zeroing it would make an already
 * fired chute look armed to the operator, "the one lie this system must not
 * tell"). So after a reset the counter still reads 1, and a burst testing for
 * `>= 1` confirms instantly against the PREVIOUS release and transmits nothing —
 * which made RESET:CHUTE useless over the air, the exact opposite of what it is
 * for. Recording where the counter stood at reset time is how the next release is
 * distinguished from the last one, without asking the vehicle to lie.
 *
 * Same shape as the `ul` baseline in fireConfigBurst(), on purpose. */
int      chuteBaseline  = 0;

bool     pingPending    = false;
uint32_t pingRequestedMs = 0;
uint32_t pingsSent      = 0;

/* ========================================================================== */

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);      /* peripherals on */
  delay(100);

  displayBegin();
  displaySplash();

  loraReady = radioBegin();

  if (!loraReady) {
    Serial.println("[GCS] LoRa init FAILED - halted");
    displayFatal("LoRa FAILED", "Check wiring");
    while (true) delay(1000);
  }

  Serial.print("[GCS] ready ");
  Serial.print(FREQ_MHZ, 1);
  Serial.print(" MHz SF");
  Serial.print(SPREADING);
  Serial.print(" team ");
  Serial.println(TEAM_ID);

  /* Which binary is actually running.
   *
   * __DATE__ and __TIME__ are filled in by the COMPILER, so this line cannot be
   * faked by an editor buffer that was never rebuilt. It exists because a whole
   * debugging round was lost to the question "did that upload take?" — the Arduino
   * IDE compiles its in-memory editor text, not the file on disk, so a sketch left
   * open across an external edit uploads the OLD code with no warning anywhere.
   *
   * If this stamp does not move after an upload, nothing was rebuilt. Check that
   * before doubting the change. */
  Serial.print("[GCS] build ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);

  radio.startReceive();
}

void loop() {
  radioPoll();      /* a packet waiting?  */
  uplinkPoll();     /* a command waiting? */
  displayUpdate();  /* throttled internally */
  delay(LOOP_TICK_MS);
}
