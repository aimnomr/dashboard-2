"""Raw byte log.

The first rule of the pipeline: every line reaching the PC is written to disk
*before* anything tries to interpret it. No filtering, no validity gate, no
exceptions. If the parser has a bug or the packet format drifts, the flight is still
fully recoverable afterwards.

This is not theoretical. The v1 bridge (serial_to_mqtt_V3.py) wrote nothing to disk
and discarded malformed lines — a broker outage or an unhandled exception meant the
data was simply gone. See wiki/source/previous-system/serial-to-mqtt-bridge.md.

Competition flights do not get a second attempt.
"""

from __future__ import annotations

import os
from datetime import datetime, timezone
from pathlib import Path

from .contract import contract, dumps


class RawLog:
    """Append-only line log with durable writes.

    Every line is flushed and fsync'd immediately. At 1 Hz the cost is irrelevant and
    the guarantee is worth far more: a hard power loss cannot cost more than the line
    currently in flight.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = self.path.open("a", encoding="utf-8", newline="\n")
        self._count = 0

    @classmethod
    def create(cls, log_dir: Path, source_name: str) -> "RawLog":
        """Open a new timestamped log for a run.

        The source name is part of the filename so a mock-fed log can never be mistaken
        for a real flight when someone finds it six weeks later.
        """
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        started = datetime.now(timezone.utc)
        log = cls(Path(log_dir) / "raw" / f"{stamp}-{source_name}.log")
        log._write_sidecar(source_name, started)
        return log

    def _write_sidecar(self, source_name: str, started: datetime) -> None:
        """The packet contract, beside the log and machine-readable.

        NOT inside the log. A header block was tried on 2026-08-20 and reverted the same
        day (devlog 049, 050): it cost 167 lines of prose in a file whose entire value is
        being a faithful byte record, and prose is the wrong shape for the job anyway —
        anything reading this wants `fields[13]["name"]`, not a paragraph to grep.

        Written ONCE, when the log opens, and never revisited. That is what makes it
        durable: no close-time rewrite to be interrupted, no half-written state after a
        power loss, and the log alone is still the record if this file is lost.

        It is also why the content is restricted to the current wire format. Anything
        transient — sensor faults, plausibility thresholds, observed statistics — would
        be frozen at open and quietly wrong forever after.
        """
        self.path.with_suffix(".meta.json").write_text(
            dumps(contract(source_name, started)), encoding="utf-8"
        )

    @property
    def count(self) -> int:
        return self._count

    def write(self, line: str) -> None:
        self._fh.write(line + "\n")
        self._fh.flush()
        os.fsync(self._fh.fileno())
        self._count += 1

    def close(self) -> None:
        if not self._fh.closed:
            self._fh.flush()
            os.fsync(self._fh.fileno())
            self._fh.close()
