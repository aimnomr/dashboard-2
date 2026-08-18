# Serial → MQTT Bridge

Source: `serial_to_mqtt_V3.py`

PC-side Python script bridging the ground unit's USB serial output to MQTT for Node-RED.

## Configuration

```python
SERIAL_PORT = "COM12"          # "updated to new ground unit port"
BAUD_RATE   = 115200
MQTT_BROKER = "localhost"
MQTT_PORT   = 1883
MQTT_TOPIC  = "cansat/telemetry"
```

Dependencies: `pyserial`, `paho-mqtt` (using `CallbackAPIVersion.VERSION2`).

## Behaviour

1. Connect to MQTT, `loop_start()` in background.
2. Open serial, 2 s settle delay.
3. Per line: decode UTF-8 with `errors="ignore"`, strip.
4. Lines starting with `[` are ground-unit status messages — printed, not published.
5. Otherwise split on `,`; require exactly 16 fields; cast all to `float`, then `sat` to `int`.
6. Publish as JSON to `cansat/telemetry`.
7. Malformed lines are printed with a `[WARN]` and **discarded**.

## Limitations worth carrying forward

- **Nothing is written to disk.** Every packet exists only in flight to MQTT. If the broker is
  down, Node-RED isn't running, or the script crashes, that data is gone permanently. There is
  no raw log.
- **Strict 16-field match** — rejects the 15-field simulator firmware entirely
  (see `../firmware/packet-format.md`).
- **`float()` on every field** would raise on the `CHUTE:1` token, so the chute flag cannot pass
  through this bridge even if the field count were tolerated.
- **Hardcoded COM port** — needs editing whenever the port enumerates differently.
- **`serial.SerialException` breaks the loop and exits.** A USB re-enumeration mid-flight ends
  the session; there is no reconnect.
- **No validation beyond type casting.** A garbled-but-numeric line with the right field count
  is published as if it were good data.
- Runs as a **third process** alongside the MQTT broker and Node-RED — three things that must
  all be up before any data flows.
