"""Replay a captured log through the real pipeline — DEVELOPMENT ONLY.

`base.py` reserved this seat: "the real serial link, the development mock, and — later —
a file replayer". This is that replayer.

It exists because the SD card already holds framed GEN3 packets, byte for byte what the
ground station receives. Nothing needs converting: pointing this at a capture puts real
vehicle data through the real raw logger, the real parser and the real transport, which
is a stronger test than any synthetic profile because nobody here invented the numbers.

Two things it deliberately does NOT do:

* **It does not add link quality.** RSSI and SNR are measured by the ground station's
  radio as a packet arrives. A packet read from a file arrived over no radio, so there is
  no measurement, and inventing one would put a fabricated dBm figure on the operator's
  screen. They stay absent, and the UI must render that as "—".
* **It does not clean the file up.** Malformed lines, headers and corruption are emitted
  exactly as captured, for the same reason the mock injects noise: a UI tested only
  against a perfect feed has never been tested against the feed it will get.

`simulated = True`, so the banner is unmissable — and, as with the mock, the launch entry
point has no route here at all. Replay is reachable only from `devtools/run_replay.py`.
"""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path
from typing import AsyncIterator

from ..parser import parse_line
from .base import TelemetrySource

log = logging.getLogger(__name__)

#: Used between packets whose spacing cannot be taken from the vehicle clock.
DEFAULT_INTERVAL = 1.0

#: A gap longer than this is not paced literally. Real captures contain resets and hand
#: edits, and honouring a 40-minute jump would hang the replay looking like a crash.
MAX_GAP_SECONDS = 10.0


class FileSource(TelemetrySource):
    """Emit the lines of a captured log at the rate the vehicle produced them."""

    name = "replay"
    simulated = True

    def __init__(self, path: str | Path, *, speed: float = 1.0, loop: bool = False,
                 interval: float | None = None, hold: bool = False) -> None:
        if speed <= 0:
            raise ValueError("speed must be positive")

        self.path = Path(path)
        self.speed = speed
        self.loop = loop
        #: When set, pacing ignores the vehicle clock and uses this fixed gap instead.
        self.interval = interval
        #: Keep the source open after the capture ends, emitting nothing.
        #:
        #: The runner stops the whole server as soon as the pipeline finishes, which is
        #: right for the mock but wrong here: the reason to replay a capture is usually to
        #: look at the dashboard, and without this the window it produced closes before it
        #: can be read. Holding leaves the final state on screen, going stale honestly.
        self.hold = hold
        self._closed = False

        if not self.path.is_file():
            raise FileNotFoundError(f"no such capture: {self.path}")

        # errors="replace" matches serial_source: corruption becomes a visible U+FFFD
        # rather than vanishing, so a replay is damaged in the same way a live feed is.
        text = self.path.read_text(encoding="utf-8", errors="replace")
        self._lines = [ln for ln in text.splitlines() if ln.strip()]
        if not self._lines:
            raise ValueError(f"capture is empty: {self.path}")

        # Read up front rather than streamed. These files are a few hundred KB, and
        # pacing needs the next packet's timestamp before the current one is due.
        self._name_hint = self.path.name

    async def lines(self) -> AsyncIterator[str]:
        log.warning("FileSource active — REPLAY of %s, not a live link", self._name_hint)
        log.info("%d line(s), speed x%.3g, pacing=%s", len(self._lines), self.speed,
                 "fixed" if self.interval is not None else "vehicle clock")

        passes = 0
        while not self._closed:
            previous_ms: int | None = None

            for raw in self._lines:
                if self._closed:
                    return

                delay, previous_ms = self._delay_before(raw, previous_ms)
                if delay > 0:
                    await asyncio.sleep(delay)
                if self._closed:
                    return

                yield raw

            passes += 1
            if not self.loop:
                log.info("replay of %s complete (%d line(s))",
                         self._name_hint, len(self._lines))
                if self.hold:
                    log.info("holding the dashboard open — Ctrl-C to stop. "
                             "The link will go stale, then lost, exactly as it would "
                             "if the vehicle stopped transmitting.")
                    while not self._closed:
                        await asyncio.sleep(0.5)
                return

            # Looping rewinds the vehicle clock and restarts `seq` at 1 — the same shape
            # as a real reboot. Useful on purpose: it is the only way this source can
            # produce the restart case, which no single capture contains.
            log.info("replay pass %d complete — restarting (seq will reset)", passes)

    def _delay_before(self, raw: str, previous_ms: int | None) -> tuple[float, int | None]:
        """How long to wait before emitting `raw`, and the clock value to carry forward.

        The parser is used here for its timestamp ONLY. Nothing is dropped, reordered or
        rewritten on the strength of it — a line that fails to parse is still emitted, it
        just cannot contribute a gap, which is exactly true of a corrupt packet on a real
        link too.
        """
        result = parse_line(raw)

        # Non-telemetry goes out immediately in EITHER pacing mode. On a real link a
        # status line arrives between packets, not instead of one, so it must never
        # consume a slot in the cadence.
        #
        # This check sits above the fixed-interval branch deliberately. Below it, the
        # self-describing header added on 2026-08-20 would cost 167 intervals — nearly
        # three minutes of an empty dashboard at --interval 1 — before the first packet.
        if result.kind != "frame":
            return 0.0, previous_ms

        if self.interval is not None:
            return self.interval / self.speed, None

        current_ms = result.vehicle_ms if result.ok else None

        if current_ms is None:
            # A generation with no onboard clock, or a corrupt packet. Either way there
            # is no gap to reproduce, so it follows immediately.
            return 0.0, previous_ms

        if previous_ms is None:
            return 0.0, current_ms

        gap = (current_ms - previous_ms) / 1000.0
        if gap <= 0 or gap > MAX_GAP_SECONDS:
            # Backwards means the capture contains a reset; enormous means trimmed rows
            # or a dropout. Neither is worth reproducing literally.
            gap = DEFAULT_INTERVAL

        return gap / self.speed, current_ms

    async def send_command(self, command: str) -> bool:
        """Always false. A file cannot receive anything.

        Reported honestly rather than faked: during a replay the EJECT button must not
        appear to work, or the one thing it teaches the operator is wrong.
        """
        log.warning("REPLAY: command %r not sent — a capture has no uplink", command)
        return False

    async def aclose(self) -> None:
        self._closed = True
