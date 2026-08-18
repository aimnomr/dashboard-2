"""GEN2 telemetry parsing.

The packet format is documented in wiki/source/firmware/packet-format.md and the
decisions this module implements are in wiki/decisions/ingest-pipeline.md.

Design notes worth keeping in view:

* GEN2 (17 fields at the PC) is canonical, but GEN1 (16) still parses — the SD card
  logs and the v1 database are GEN1, and old ground unit firmware may still be in use.
* Field 15 arrives as the literal text ``CHUTE:0`` / ``CHUTE:1``. Stripping it here is
  the agreed interim workaround for ISS-07; the firmware is unchanged.
* There is no packet counter, timestamp or checksum (ISS-08), so a corrupted line that
  still parses is indistinguishable from good data. Range checks are the only available
  defence, and they produce *warnings* — never rejections. A genuinely anomalous flight
  must still reach the screen.
* Nothing is ever silently dropped. A line that cannot be parsed comes back as a failed
  result carrying the original text, so the operator sees corruption rather than a
  dashboard that has quietly stopped moving.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Literal

#: Fields 1-14, shared by both generations, in wire order.
_COMMON_FIELDS = (
    "temp", "hum", "pres", "alt",
    "ax", "ay", "az",
    "gx", "gy", "gz",
    "lat", "lng", "spd", "sat",
)

#: GEN2 adds the chute flag; the ground unit appends link quality to both generations.
GEN2_FIELDS = (*_COMMON_FIELDS, "chute", "rssi", "snr")   # 17
GEN1_FIELDS = (*_COMMON_FIELDS, "rssi", "snr")            # 16

#: Fields that are conceptually integers. Everything else is a float.
_INT_FIELDS = frozenset({"sat", "chute"})

#: Plausibility bounds. Deliberately wider than the simulator clamps in
#: packet-format.md — those are properties of the simulator, not of physics. Real
#: logged data already sits outside them (altitude reached -14.3 m in CANSAT_DATA,
#: because altitude is relative to boot and drifts with pressure).
#: Violations are reported as warnings and never suppress a frame.
_PLAUSIBLE: dict[str, tuple[float, float]] = {
    "temp": (-20.0, 80.0),
    "hum": (0.0, 100.0),
    "pres": (800.0, 1100.0),
    "alt": (-100.0, 2000.0),
    "ax": (-16.0, 16.0),      # MPU6050 configured to +/-8 g; +/-16 is the part's ceiling
    "ay": (-16.0, 16.0),
    "az": (-16.0, 16.0),
    "gx": (-500.0, 500.0),    # configured to +/-500 deg/s
    "gy": (-500.0, 500.0),
    "gz": (-500.0, 500.0),
    "lat": (-90.0, 90.0),
    "lng": (-180.0, 180.0),
    "spd": (0.0, 500.0),
    "sat": (0.0, 64.0),
    "chute": (0.0, 1.0),
    "rssi": (-150.0, 0.0),
    "snr": (-30.0, 30.0),
}

#: The ground unit interleaves its own status messages with telemetry on the same
#: serial stream, e.g. "[GCS] Timeout - no packet". They are reported, never parsed.
_STATUS_PREFIX = "["

Kind = Literal["frame", "status", "empty"]


@dataclass(slots=True)
class ParseResult:
    kind: Kind
    ok: bool
    raw: str
    frame: dict[str, float | int | None] | None = None
    error: str | None = None
    warnings: list[str] = field(default_factory=list)
    generation: Literal["GEN1", "GEN2"] | None = None


def parse_line(line: str) -> ParseResult:
    """Classify and parse a single line from the ground unit."""
    raw = line.strip()

    if not raw:
        return ParseResult(kind="empty", ok=True, raw=raw)

    if raw.startswith(_STATUS_PREFIX):
        return ParseResult(kind="status", ok=True, raw=raw)

    parts = raw.split(",")

    if len(parts) == len(GEN2_FIELDS):
        names, generation = GEN2_FIELDS, "GEN2"
    elif len(parts) == len(GEN1_FIELDS):
        names, generation = GEN1_FIELDS, "GEN1"
    else:
        return ParseResult(
            kind="frame", ok=False, raw=raw,
            error=(
                f"expected {len(GEN2_FIELDS)} fields (GEN2) "
                f"or {len(GEN1_FIELDS)} (GEN1), got {len(parts)}"
            ),
        )

    frame: dict[str, float | int | None] = {}
    for name, token in zip(names, parts):
        token = token.strip()
        if name == "chute":
            token = _strip_chute_prefix(token)
        try:
            value = float(token)
        except ValueError:
            return ParseResult(
                kind="frame", ok=False, raw=raw, generation=generation,
                error=f"field {name!r} is not numeric: {token!r}",
            )
        if not math.isfinite(value):
            return ParseResult(
                kind="frame", ok=False, raw=raw, generation=generation,
                error=f"field {name!r} is not finite: {token!r}",
            )
        frame[name] = int(value) if name in _INT_FIELDS else value

    # GEN1 carries no chute flag. Absent is not the same as "not deployed", so it is
    # None rather than 0 — the UI must show "unknown", never a reassuring "ARMED".
    if generation == "GEN1":
        frame["chute"] = None

    return ParseResult(
        kind="frame", ok=True, raw=raw, frame=frame,
        generation=generation, warnings=_check_ranges(frame),
    )


def _strip_chute_prefix(token: str) -> str:
    """``CHUTE:1`` -> ``1``. Bare ``0``/``1`` passes through untouched.

    ISS-07: the prefix is stripped here rather than in firmware, so the dashboard is
    not blocked waiting on a change owned by another team member.
    """
    head, sep, tail = token.partition(":")
    if sep and head.strip().upper() == "CHUTE":
        return tail.strip()
    return token


def _check_ranges(frame: dict[str, float | int | None]) -> list[str]:
    warnings: list[str] = []
    for name, value in frame.items():
        bounds = _PLAUSIBLE.get(name)
        if bounds is None or value is None:
            continue
        low, high = bounds
        if not (low <= value <= high):
            warnings.append(f"{name}={value:g} outside plausible range [{low:g}, {high:g}]")
    return warnings
