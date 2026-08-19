"""source -> raw log -> parser -> browsers.

The ordering in `run()` is the whole point of this module and is not an
implementation detail: the raw log write happens before parsing is attempted, so a
parser bug cannot cost data.
"""

from __future__ import annotations

import logging
from datetime import datetime, timezone

from .hub import Hub
from .parser import parse_line
from .rawlog import RawLog
from .sources.base import TelemetrySource

log = logging.getLogger(__name__)


class Pipeline:
    def __init__(self, source: TelemetrySource, rawlog: RawLog, hub: Hub) -> None:
        self.source = source
        self.rawlog = rawlog
        self.hub = hub
        self._rx_index = 0

    @property
    def rx_index(self) -> int:
        """Lines that arrived and looked like telemetry.

        Emphatically NOT a packet counter. This cannot detect loss: a gap between two
        consecutive values means nothing was received, not that nothing was sent. Named
        `rx_index` rather than `seq` so nobody downstream is tempted to compute a loss
        figure from it.

        GEN3 does carry a real counter, and it is forwarded as `seq` in the envelope —
        but loss arithmetic is deliberately not done here. It belongs in `linkstats.py`
        (step 2 of wiki/decisions/dashboard-gen3-plan.md), which owns baselines, restart
        detection and the rolling window. Subtracting two `seq` values in a UI would get
        every one of those cases wrong.
        """
        return self._rx_index

    async def run(self) -> None:
        log.info("pipeline started, source=%s simulated=%s",
                 self.source.name, self.source.simulated)
        try:
            async for line in self.source.lines():
                # ---- durability first, always ----------------------------------
                self.rawlog.write(line)

                # ---- then interpretation ---------------------------------------
                envelope = self._envelope(line)
                if envelope is not None:
                    await self.hub.broadcast(envelope)
        finally:
            self.rawlog.close()
            await self.source.aclose()
            log.info("pipeline stopped after %d raw line(s)", self.rawlog.count)

    def _envelope(self, line: str) -> dict | None:
        result = parse_line(line)
        now = datetime.now(timezone.utc).isoformat()

        if result.kind == "empty":
            return None

        if result.kind == "status":
            return {
                "type": "status",
                "pc_time": now,
                "raw": result.raw,
                "simulated": self.source.simulated,
            }

        self._rx_index += 1
        envelope = {
            "type": "frame",
            "rx_index": self._rx_index,
            # Arrival time at the PC, NOT sampling time. Use this for staleness and
            # link health only: the vehicle clock cannot measure silence, because a
            # dropped packet carries no timestamp with it.
            "pc_time": now,
            # The vehicle's own clock and packet counter. GEN3 only — None for GEN1 and
            # GEN2, which carry neither (ISS-08), and the UI must fall back rather than
            # substitute a zero. Anything derived FROM the data belongs on this clock:
            # it is the moment of sampling, free of link and scheduling jitter.
            "seq": result.seq,
            "vehicle_ms": result.vehicle_ms,
            # True/False for GEN3, None where the generation carries no checksum at all.
            # None is not "passed" — it means the question could not be asked.
            "crc_ok": result.crc_ok,
            "raw": result.raw,
            "ok": result.ok,
            "frame": result.frame,
            "simulated": self.source.simulated,
        }
        if result.generation:
            envelope["generation"] = result.generation
        if result.error:
            envelope["error"] = result.error
        if result.warnings:
            envelope["warnings"] = result.warnings
        return envelope
