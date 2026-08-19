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

import json
import os
from datetime import datetime, timezone
from pathlib import Path

from .contract import CONTRACT_VERSION, packet_contract


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
        log._write_header(source_name, started)
        log._write_sidecar(source_name, stamp, started)
        return log

    def _write_header(self, source_name: str, started: datetime) -> None:
        """The packet contract, in the log itself.

        Reverses the original decision to keep the log byte-faithful with metadata only
        in the sidecar. The reason it is safe now, and was not obviously safe then: every
        header line starts with '#', and `parse_line()` already classifies those as
        status lines because the vehicle's own SD logs use them. A replay reads straight
        through. Nothing has to be stripped, and no consumer needs to know the header
        exists.

        The reason it is worth doing: a sidecar is a separate file, and separate files
        get lost. A log copied to a USB stick, pasted into a chat, or attached to a
        competition submission arrives alone, and then nobody can tell whether column 14
        is a longitude or a sentinel. The contract belongs where the data is.

        Written before any telemetry, and never again - not per line, not per rotation.
        """
        self._fh.write(packet_contract(source_name, started) + "\n")
        self._fh.flush()
        os.fsync(self._fh.fileno())

    def _write_sidecar(self, source_name: str, stamp: str, started: datetime) -> None:
        """Machine-readable run metadata, beside the log.

        Kept after the header moved into the log itself: this one is for programs, the
        header is for people. Duplicating the few fields they share costs nothing and
        means neither has to be parsed to get at the other.
        """
        meta = {
            "source": source_name,
            "started_at": started.isoformat(),
            "local_stamp": stamp,
            "contract": CONTRACT_VERSION,
            "note": (
                "Raw serial lines exactly as received, including malformed and [GCS] "
                "lines. The file opens with a '#'-prefixed packet contract; the parser "
                "treats those as status lines, so it replays without stripping anything."
            ),
        }
        self.path.with_suffix(".meta.json").write_text(
            json.dumps(meta, indent=2), encoding="utf-8"
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
