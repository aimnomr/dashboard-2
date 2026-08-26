"""Synthetic telemetry source — DEVELOPMENT ONLY.

Stands in for the CanSat, the LoRa link, the ground station and the USB hop. Emits the
same GEN2 lines those would produce, so everything downstream — raw log, parser,
WebSocket — is the real code under test.

The flight profile is a port of MRC_FlightUnit_V7.ino, documented in
wiki/source/firmware/lora-link-and-protocol.md: eight phases, 78 seconds, apogee 150 m.

Two deliberate choices about realism:

* The feed is NOT clean. Roughly one line in fifty is malformed, and the ground unit's
  "[GCS] Timeout - no packet" status lines appear between packets. A UI built against a
  perfect feed is a UI that has never been tested against the feed it will actually get.
* RSSI and SNR are modelled on the ranges actually observed in CANSAT_DATA
  (RSSI -124..-14 dBm, mean -55.6; SNR -10.75..14 dB), degrading with altitude.
"""

from __future__ import annotations

import asyncio
import logging
import math
import random
from typing import AsyncIterator

from dashboard.sources.base import TelemetrySource

log = logging.getLogger(__name__)

#: (name, duration in seconds) — from MRC_FlightUnit_V7.ino
PHASES: tuple[tuple[str, int], ...] = (
    ("PRE_LAUNCH", 8),
    ("BOOST", 5),
    ("COAST", 8),
    ("APOGEE", 3),
    ("DESCENT_FREE", 12),
    ("DESCENT_CHUTE", 30),
    ("LANDING", 4),
    ("POST_LAND", 8),
)

BASE_LAT = 3.07830
BASE_LNG = 101.71220

#: The uplink wire protocol, mirroring `CMD_EJECT` and `CMD_PING` in
#: `firmware/MRC_GroundStation_GEN3/Config.h`. The mock must reject anything the real
#: ground station would reject — see `send_command` below.
CMD_EJECT = "CMD:EJECT"
CMD_PING = "CMD:PING"

#: GEN4 config commands, mirroring `MRC_GroundStation_GEN4/Config.h`.
#:
#: The mock accepts these because a GEN4 ground station does. It does NOT simulate
#: their effect: there is no auto-eject rule in here to reconfigure, and inventing one
#: would produce a mock that agrees with itself about behaviour the firmware has never
#: demonstrated. Accepting-and-logging is the honest middle — the command path is
#: exercised end to end, and nothing pretends the trigger was retuned.
CMD_RESET = "CMD:RESET"
CMD_RESET_CHUTE = "CMD:RESET:CHUTE"
CMD_SET_PREFIX = "CMD:SET:"

#: Wire format, GEN2. The ground unit appends ",{rssi:.1f},{snr:.2f}" to this.
_PACKET_FMT = (
    "{temp:.2f},{hum:.1f},{pres:.2f},{alt:.2f},"
    "{ax:.3f},{ay:.3f},{az:.3f},"
    "{gx:.2f},{gy:.2f},{gz:.2f},"
    "{lat:.6f},{lng:.6f},{spd:.2f},{sat:d},CHUTE:{chute:d}"
)


def _clamp(value: float, low: float, high: float) -> float:
    return low if value < low else (high if value > high else value)


def _noise(scale: float) -> float:
    return random.uniform(-1.0, 1.0) * scale


def _altitude(phase: str, t: float) -> float:
    if phase == "BOOST":
        return 80.0 * (t / 5.0)
    if phase == "COAST":
        f = t / 8.0
        return 80.0 + 70.0 * (1.0 - (1.0 - f) ** 2)
    if phase == "APOGEE":
        return 150.0
    if phase == "DESCENT_FREE":
        return 150.0 - 70.0 * (t / 12.0)
    if phase == "DESCENT_CHUTE":
        return _clamp(80.0 - 78.0 * (t / 30.0), 0.0, 80.0)
    if phase == "LANDING":
        return _clamp(2.0 - 2.0 * (t / 4.0), 0.0, 2.0)
    return 0.0  # PRE_LAUNCH, POST_LAND


def _imu(phase: str, t: float) -> tuple[float, float, float, float, float, float]:
    """Per-phase accelerometer (g) and gyro (deg/s), mirroring the firmware."""
    if phase == "BOOST":
        return (_noise(0.3), _noise(0.3), 6.5 + _noise(0.8),
                _noise(8.0), _noise(8.0), _noise(3.0))
    if phase == "COAST":
        return (_noise(0.15) + 0.3 * math.sin(t), _noise(0.15) + 0.3 * math.cos(t),
                0.2 + _noise(0.1),
                15.0 * math.sin(t * 0.8) + _noise(5), 15.0 * math.cos(t * 0.6) + _noise(5),
                _noise(10))
    if phase == "APOGEE":
        return (_noise(0.05), _noise(0.05), _noise(0.08),
                _noise(3), _noise(3), _noise(2))
    if phase == "DESCENT_FREE":
        return (_noise(0.2) + 0.5 * math.sin(t * 1.2), _noise(0.2) + 0.5 * math.cos(t * 0.9),
                -0.8 + _noise(0.15),
                20.0 * math.sin(t * 1.5) + _noise(6), 20.0 * math.cos(t * 1.1) + _noise(6),
                10.0 * math.sin(t * 0.7) + _noise(4))
    if phase == "DESCENT_CHUTE":
        return (_noise(0.06), _noise(0.06), 0.85 + _noise(0.04),
                _noise(2), _noise(2), _noise(1.5))
    if phase == "LANDING":
        impact = 4.0 if t < 1 else 1.0
        return (_noise(0.1) * impact, _noise(0.1) * impact, 1.0 + impact * _noise(0.5),
                _noise(5) * impact, _noise(5) * impact, _noise(3) * impact)
    if phase == "PRE_LAUNCH":
        return (_noise(0.02), _noise(0.02), 1.0 + _noise(0.01),
                _noise(0.5), _noise(0.5), _noise(0.3))
    return (_noise(0.01), _noise(0.01), 1.0 + _noise(0.005),
            _noise(0.3), _noise(0.3), _noise(0.2))  # POST_LAND


def _speed(phase: str, t: float) -> float:
    if phase == "BOOST":
        return 180.0 + _noise(15)
    if phase == "COAST":
        return _clamp(180.0 - t * 20, 0, 250) + _noise(8)
    if phase == "APOGEE":
        return abs(_noise(2))
    if phase == "DESCENT_FREE":
        return 40.0 + _noise(5)
    if phase == "DESCENT_CHUTE":
        return 8.0 + _noise(1.5)
    return _clamp(abs(_noise(0.3)), 0, 5)


class MockSource(TelemetrySource):
    name = "mock"
    simulated = True

    def __init__(self, *, interval: float = 1.0, loop: bool = True,
                 noise: bool = True, seed: int | None = None) -> None:
        self.interval = interval
        self.loop = loop
        self.noise = noise
        self._chute_deployed = False
        self._closed = False
        self._global_t = 0
        if seed is not None:
            random.seed(seed)

    async def lines(self) -> AsyncIterator[str]:
        log.warning("MockSource active — synthetic telemetry, not a real flight")
        while not self._closed:
            for phase, duration in PHASES:
                for t in range(duration):
                    if self._closed:
                        return
                    await asyncio.sleep(self.interval)

                    if self.noise and random.random() < 0.04:
                        yield "[GCS] Timeout - no packet"
                        continue

                    line = self._packet(phase, t)
                    if self.noise and random.random() < 0.02:
                        line = self._corrupt(line)
                    yield line
                    self._global_t += 1

            if not self.loop:
                log.info("mock flight complete (%d s); firmware halts here", self._global_t)
                return

            log.info("mock flight complete — restarting")
            self._chute_deployed = False
            self._global_t = 0

    def _packet(self, phase: str, t: int) -> str:
        alt_true = _clamp(_altitude(phase, t) + _noise(0.08), 0, 500)
        ax, ay, az, gx, gy, gz = _imu(phase, t)

        values = {
            "temp": _clamp(32.5 - alt_true * 0.0065 + _noise(0.15), 15, 50),
            "hum": _clamp(78.0 + alt_true * 0.01 + _noise(0.4), 30, 100),
            "pres": _clamp(1007.9 * math.exp(-alt_true / 8500.0) + _noise(0.08), 900, 1050),
            "alt": _clamp(alt_true + _noise(0.12), 0, 500),
            "ax": _clamp(ax, -16, 16), "ay": _clamp(ay, -16, 16), "az": _clamp(az, -16, 16),
            "gx": _clamp(gx, -250, 250), "gy": _clamp(gy, -250, 250),
            "gz": _clamp(gz, -250, 250),
            "lat": BASE_LAT + 0.00045 * math.sin(self._global_t * 0.05) + _noise(0.000008),
            "lng": BASE_LNG + 0.00030 * math.cos(self._global_t * 0.04) + _noise(0.000008),
            "spd": _clamp(_speed(phase, t), 0, 400),
            "sat": 5 if phase in ("PRE_LAUNCH", "BOOST") else 9,
            "chute": 1 if self._chute_deployed else 0,
        }
        packet = _PACKET_FMT.format(**values)

        # The ground unit appends link quality — model it degrading with altitude,
        # within the range actually observed in CANSAT_DATA.
        rssi = _clamp(-50.0 - alt_true * 0.06 + _noise(4.0), -124.0, -14.0)
        snr = _clamp(9.5 - alt_true * 0.005 + _noise(2.0), -10.75, 14.0)
        return f"{packet},{rssi:.1f},{snr:.2f}"

    @staticmethod
    def _corrupt(line: str) -> str:
        """Produce the kinds of damage the real link produces."""
        mode = random.choice(("truncate", "garble", "drop_field"))
        if mode == "truncate":
            return line[: random.randint(5, max(6, len(line) - 10))]
        if mode == "drop_field":
            parts = line.split(",")
            del parts[random.randrange(len(parts))]
            return ",".join(parts)
        parts = line.split(",")
        parts[random.randrange(len(parts))] = "��"
        return ",".join(parts)

    async def send_command(self, command: str) -> bool:
        """Accept exactly what the real ground station accepts, and nothing else.

        This used to match on "EJECT", which is what the dashboard happened to send —
        so the mock agreed with the dashboard rather than with the firmware, and the two
        of them validated each other into a protocol the hardware does not speak. Every
        eject would have been answered with `[GCS] unknown command` at the pad.

        A mock that is lenient about the protocol is worse than no mock: it converts a
        loud failure into a silent one, and moves the discovery to launch day. These
        strings mirror `CMD_EJECT` and `CMD_PING` in
        `firmware/MRC_GroundStation_GEN3/Config.h`, and the GEN4 additions mirror
        `firmware/MRC_GroundStation_GEN4/Config.h`. They must be changed together.
        """
        if command == CMD_EJECT:
            self._chute_deployed = True
            log.warning("MOCK: %s received — chute flag now 1", CMD_EJECT)
            return True

        if command == CMD_PING:
            # The real ground station transmits and prints; there is nothing on the
            # downlink to simulate, because a ping is confirmed on the vehicle's OLED.
            log.warning("MOCK: %s received — no downlink effect, as on hardware",
                        CMD_PING)
            return True

        if command == CMD_RESET_CHUTE:
            # Checked before CMD_RESET. Exact comparisons, so the order cannot matter
            # today — kept deliberate because the firmware keeps it deliberate too.
            #
            # This one DOES have an effect worth mirroring: on hardware it clears the
            # fire latch so the mechanism can be driven again, while the chute COUNTER
            # stays where it is. Clearing the flag here keeps the mock honest about the
            # one thing RESET:CHUTE actually changes.
            self._chute_deployed = False
            log.warning("MOCK: %s received — chute flag cleared, counter untouched",
                        CMD_RESET_CHUTE)
            return True

        if command == CMD_RESET:
            # Trigger state only. The mock has no trigger state, so nothing changes —
            # which is exactly right: RESET must never touch the chute.
            log.warning("MOCK: %s received — trigger state only, chute untouched",
                        CMD_RESET)
            return True

        if command.startswith(CMD_SET_PREFIX):
            # Bounds are validated in `api.py` before anything reaches a source, and
            # again on the vehicle. Re-checking here would be a third copy of the same
            # numbers with no third opinion to offer.
            log.warning("MOCK: %s received — accepted, no simulated trigger to retune",
                        command)
            return True

        log.error("MOCK: unknown command %r — the ground station would reject this too",
                  command)
        return False

    async def aclose(self) -> None:
        self._closed = True
