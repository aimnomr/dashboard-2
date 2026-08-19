"""Telemetry parsing.

The packet formats are documented in wiki/source/firmware/packet-format.md and
wiki/decisions/gen3-packet-format.md. The decisions this module implements are in
wiki/decisions/ingest-pipeline.md and wiki/decisions/dashboard-gen3-plan.md.

Three generations are accepted:

    GEN3.1  $MRC,seq,ms,…,chute,ul,hdop,fixq*CRC16[,rssi,snr]   canonical, 20 vehicle
    GEN3.0  $MRC,seq,ms,…,chute*CRC16[,rssi,snr]                17 vehicle
    GEN2    temp,…,sat,CHUTE:n,rssi,snr                         17 fields
    GEN1    temp,…,sat,rssi,snr                                 16 fields

GEN1 and GEN2 tolerance is kept deliberately: the bench firmware is GEN1, the CanSat SD
logs from that era are GEN1, and so is CANSAT_DATA.

GEN3.0 tolerance is not optional either. FLIGHT21/22.CSV in tests/fixtures/ are 17-field
captures and are what pins this parser against real firmware output; a parser that only
accepted the new shape would have thrown away its own regression corpus. The three GEN3.1
fields are APPENDED for exactly that reason — every older index is unchanged, so the two
shapes differ only in how much they carry, never in where anything is.

GEN3's link-quality fields are optional. The ground station appends them, but a packet
read straight off the SD card has none — and that is the same file a replay source would
consume, so it must parse without special-casing.

Design notes worth keeping in view:

* Nothing is ever silently dropped. A line that cannot be parsed comes back as a failed
  result carrying the original text, so corruption reaches the operator rather than
  presenting as a dashboard that has quietly stopped moving.
* A failed checksum REJECTS the frame (`ok=False`, `frame=None`). A corrupt packet must
  never move the altitude trace. The raw line still travels, so the raw feed can show it.
* Range checks produce *warnings*, never rejections. A genuinely anomalous flight must
  still reach the screen.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Literal

# --------------------------------------------------------------------------- GEN3

#: Vehicle-supplied fields, in wire order, after the team marker. GEN3.0 stops here.
GEN3_VEHICLE_FIELDS = (
    "seq", "ms",
    "temp", "hum", "pres", "alt",
    "ax", "ay", "az",
    "gx", "gy", "gz",
    "lat", "lng", "spd", "sat",
    "chute",
)

#: Appended by GEN3.1 firmware, 2026-08-20. Absent from every capture taken before it.
#:
#: ``ul``    total uplink commands received (pings + ejects). Non-zero is proof the
#:           two-way link has worked at least once — the question devlogs 037 to 044
#:           were all argued blind for want of.
#: ``hdop``  horizontal dilution of precision. 0.0 means the receiver never reported
#:           it, which is safe because a real HDOP is never zero.
#: ``fixq``  the receiver's own verdict: -1 not reported, 0 invalid, 1 GPS, 2 DGPS.
GEN3_EXTENDED_FIELDS = ("ul", "hdop", "fixq")

#: Appended by the ground station after the checksum. Absent when read from SD.
GEN3_LINK_FIELDS = ("rssi", "snr")

# ---------------------------------------------------------------- GEN1 and GEN2

_COMMON_FIELDS = (
    "temp", "hum", "pres", "alt",
    "ax", "ay", "az",
    "gx", "gy", "gz",
    "lat", "lng", "spd", "sat",
)

GEN2_FIELDS = (*_COMMON_FIELDS, "chute", "rssi", "snr")   # 17
GEN1_FIELDS = (*_COMMON_FIELDS, "rssi", "snr")            # 16

#: Human-readable contract for every field, rendered into the header of each raw log
#: so a .log file explains itself without the codebase. `(unit, printf, meaning)`.
#:
#: This is the single source of truth for that header. `test_field_doc_covers_every_field`
#: fails if a field is added to the tuples above without an entry here, which is what
#: stops the logs drifting away from the parser the way documentation usually does.
FIELD_DOC: dict[str, tuple[str, str, str]] = {
    "seq":   ("-",      "%lu",  "packet counter since VEHICLE boot. Restarts at 1 on reboot. "
                                "Not a loss metric by itself - see the link stats"),
    "ms":    ("ms",     "%lu",  "millis() since vehicle boot, at the moment of sampling. This is "
                                "the clock to derive rates from; arrival time carries link jitter"),
    "temp":  ("degC",   "%.2f", "BME280 air temperature"),
    "hum":   ("%RH",    "%.1f", "BME280 relative humidity. Sensor accuracy is +/-3%, so the "
                                "first decimal is precision, not accuracy"),
    "pres":  ("hPa",    "%.2f", "BME280 barometric pressure. The altitude source"),
    "alt":   ("m",      "%.1f", "RELATIVE TO BOOT ALTITUDE, not above sea level. Zeroed during "
                                "startup calibration, so negative values are normal"),
    "ax":    ("g",      "%.3f", "MPU6050 accel X, +/-8 g range"),
    "ay":    ("g",      "%.3f", "MPU6050 accel Y"),
    "az":    ("g",      "%.3f", "MPU6050 accel Z. +z runs along the can's LONG AXIS, so this "
                                "reads ~1 g sitting upright. Reads ~0.92 on the current unit"),
    "gx":    ("deg/s",  "%.2f", "MPU6050 gyro X, +/-500 deg/s range"),
    "gy":    ("deg/s",  "%.2f", "MPU6050 gyro Y"),
    "gz":    ("deg/s",  "%.2f", "MPU6050 gyro Z"),
    "lat":   ("deg",    "%.5f", "GPS latitude, ~1.1 m resolution. 0.00000 means NO FIX - it is "
                                "a sentinel, not a position in the Gulf of Guinea"),
    "lng":   ("deg",    "%.5f", "GPS longitude. Same sentinel"),
    "spd":   ("km/h",   "%.1f", "GPS ground speed. 0.0 when there is no fix"),
    "sat":   ("count",  "%d",   "satellites tracked. A PROXY for accuracy - hdop is the "
                                "measurement. 4 is the minimum for a 3D fix"),
    "chute": ("count",  "%d",   "0 = armed, N = eject commands received. This means COMMANDED, "
                                "never deployed: nothing on board can sense the canopy"),
    "ul":    ("count",  "%lu",  "GEN3.1. Total uplink commands received, pings + ejects. "
                                "Non-zero is proof the two-way link has worked at least once"),
    "hdop":  ("-",      "%.1f", "GEN3.1. Horizontal dilution of precision, LOWER IS BETTER. "
                                "0.0 means NOT REPORTED - it is never a perfect fix"),
    "fixq":  ("-",      "%d",   "GEN3.1. The receiver's own verdict: -1 not reported, "
                                "0 invalid, 1 GPS fix, 2 DGPS fix"),
    "rssi":  ("dBm",    "%.1f", "Measured by the GROUND STATION as the packet arrived, and "
                                "appended AFTER the checksum - so it is not covered by the CRC "
                                "and is absent from anything read off the vehicle's SD card"),
    "snr":   ("dB",     "%.1f", "Ground station signal-to-noise. Same caveats as rssi"),
}

#: Fields that are conceptually integers. Everything else is a float.
_INT_FIELDS = frozenset({"sat", "chute", "seq", "ms", "ul", "fixq"})

#: Plausibility bounds. Deliberately wider than the simulator clamps — those are
#: properties of the simulator, not of physics. Real logged data already sits outside
#: them (altitude reached -14.3 m in CANSAT_DATA, being relative to boot).
#: Violations are warnings and never suppress a frame.
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
    "chute": (0.0, 255.0),
    "ul": (0.0, 100000.0),
    # 0 is the "not reported" sentinel; a real HDOP runs from ~0.5 (superb) to 50 (junk).
    "hdop": (0.0, 100.0),
    "fixq": (-1.0, 8.0),      # NMEA defines up to 8; this receiver emits 0, 1 or 2
    "rssi": (-150.0, 0.0),
    "snr": (-30.0, 30.0),
}

#: The ground unit interleaves its own status messages with telemetry on the same
#: serial stream, e.g. "[GCS] Timeout - no packet". They are reported, never parsed.
_STATUS_PREFIX = "["

Kind = Literal["frame", "status", "empty"]
Generation = Literal["GEN1", "GEN2", "GEN3"]


@dataclass(slots=True)
class ParseResult:
    kind: Kind
    ok: bool
    raw: str
    frame: dict[str, float | int | None] | None = None
    error: str | None = None
    warnings: list[str] = field(default_factory=list)
    generation: Generation | None = None

    # GEN3 only. None for older generations, which carry none of these.
    team: str | None = None
    seq: int | None = None
    vehicle_ms: int | None = None
    #: True/False for GEN3, None when the generation has no checksum at all.
    crc_ok: bool | None = None


def crc16_ccitt(data: bytes) -> int:
    """CRC16/CCITT-FALSE — poly 0x1021, init 0xFFFF, no reflection, no final xor.

    Must agree byte for byte with the flight unit's `crc16Ccitt()` in Packet.ino and
    with firmware/tests/verify_gen3.py, which is the reference. Verified against real
    hardware output: 5/5 over the air, and 1013/1013 across the two SD captures in
    tests/fixtures/ — checksums the firmware computed, not ones generated here.

    Pinned by tests/test_parser_gen3.py, which will fail the moment this stops agreeing.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def parse_line(line: str) -> ParseResult:
    """Classify and parse a single line from the ground unit or an SD log."""
    raw = line.strip()

    if not raw:
        return ParseResult(kind="empty", ok=True, raw=raw)

    if raw.startswith(_STATUS_PREFIX):
        return ParseResult(kind="status", ok=True, raw=raw)

    # A '#' line is an SD log header, not telemetry.
    if raw.startswith("#"):
        return ParseResult(kind="status", ok=True, raw=raw)

    if raw.startswith("$"):
        return _parse_gen3(raw)

    return _parse_legacy(raw)


# --------------------------------------------------------------------------- GEN3


def _parse_gen3(raw: str) -> ParseResult:
    star = raw.rfind("*")
    if star == -1:
        return ParseResult(kind="frame", ok=False, raw=raw, generation="GEN3",
                           error="GEN3 packet has no '*' checksum marker")

    if len(raw) - star - 1 < 4:
        return ParseResult(kind="frame", ok=False, raw=raw, generation="GEN3",
                           error="GEN3 checksum is truncated")

    checksum_text = raw[star + 1:star + 5]
    try:
        expected = int(checksum_text, 16)
    except ValueError:
        return ParseResult(kind="frame", ok=False, raw=raw, generation="GEN3",
                           error=f"GEN3 checksum is not hexadecimal: {checksum_text!r}")

    body = raw[1:star]
    actual = crc16_ccitt(body.encode("utf-8", errors="surrogateescape"))
    crc_ok = actual == expected

    tokens = body.split(",")
    team = tokens[0] if tokens else None

    if not crc_ok:
        # Rejected, but the generation and team are still reported so the raw feed can
        # say whose corrupted packet this was. Sequence data is NOT reported: trusting a
        # sequence number from a failed checksum is how a phantom gap or a false restart
        # gets into the link statistics.
        return ParseResult(
            kind="frame", ok=False, raw=raw, generation="GEN3", team=team, crc_ok=False,
            error=f"checksum mismatch: computed {actual:04X}, packet says {expected:04X}",
        )

    # tokens = team + either the 17 GEN3.0 vehicle fields or the 20 of GEN3.1.
    #
    # Two exact shapes, not "17 or more". A packet with 18 or 19 fields is a truncation
    # that happened to survive its checksum, or a firmware mid-edit, and either way the
    # right answer is to say so rather than to parse whatever prefix fits.
    n_vehicle = len(tokens) - 1
    if n_vehicle == len(GEN3_VEHICLE_FIELDS):
        vehicle_names = GEN3_VEHICLE_FIELDS
    elif n_vehicle == len(GEN3_VEHICLE_FIELDS) + len(GEN3_EXTENDED_FIELDS):
        vehicle_names = (*GEN3_VEHICLE_FIELDS, *GEN3_EXTENDED_FIELDS)
    else:
        return ParseResult(
            kind="frame", ok=False, raw=raw, generation="GEN3", team=team, crc_ok=True,
            error=(f"expected {len(GEN3_VEHICLE_FIELDS) + 1} (GEN3.0) or "
                   f"{len(GEN3_VEHICLE_FIELDS) + len(GEN3_EXTENDED_FIELDS) + 1} (GEN3.1) "
                   f"comma-separated values before the checksum, got {len(tokens)}"),
        )

    # Link quality, appended by the ground station after the checksum. Absent when the
    # line came from the SD card, which is the same file a replay source reads.
    tail = raw[star + 5:]
    link_values: list[str] = []
    if tail:
        if not tail.startswith(","):
            return ParseResult(
                kind="frame", ok=False, raw=raw, generation="GEN3", team=team, crc_ok=True,
                error=f"unexpected text after the checksum: {tail!r}",
            )
        link_values = tail[1:].split(",")
        if len(link_values) != len(GEN3_LINK_FIELDS):
            return ParseResult(
                kind="frame", ok=False, raw=raw, generation="GEN3", team=team, crc_ok=True,
                error=(f"expected {len(GEN3_LINK_FIELDS)} link-quality values after the "
                       f"checksum, got {len(link_values)}"),
            )

    names = (*vehicle_names, *GEN3_LINK_FIELDS[:len(link_values)])
    values = (*tokens[1:], *link_values)

    frame, error = _convert(names, values)
    if error is not None:
        return ParseResult(kind="frame", ok=False, raw=raw, generation="GEN3",
                           team=team, crc_ok=True, error=error)

    # Link quality is absent rather than zero when reading from SD. Zero would render as
    # a real -0 dBm reading, which is a fabricated measurement.
    for name in GEN3_LINK_FIELDS:
        frame.setdefault(name, None)

    # Same rule for the GEN3.1 fields on a GEN3.0 packet: None means "this firmware
    # never told us", which is not the same as ul=0 ("the uplink has never worked") or
    # fixq=0 ("the receiver says the fix is invalid"). Both of those are real claims and
    # must not be manufactured on behalf of firmware that made neither.
    for name in GEN3_EXTENDED_FIELDS:
        frame.setdefault(name, None)

    seq = frame.pop("seq")
    vehicle_ms = frame.pop("ms")

    return ParseResult(
        kind="frame", ok=True, raw=raw, frame=frame, generation="GEN3",
        team=team, seq=int(seq), vehicle_ms=int(vehicle_ms), crc_ok=True,
        warnings=_check_ranges(frame),
    )


# ------------------------------------------------------------------ GEN1 / GEN2


def _parse_legacy(raw: str) -> ParseResult:
    parts = raw.split(",")

    if len(parts) == len(GEN2_FIELDS):
        names, generation = GEN2_FIELDS, "GEN2"
    elif len(parts) == len(GEN1_FIELDS):
        names, generation = GEN1_FIELDS, "GEN1"
    else:
        return ParseResult(
            kind="frame", ok=False, raw=raw,
            error=(
                f"expected a GEN3 packet starting with '$', "
                f"or {len(GEN2_FIELDS)} fields (GEN2) "
                f"or {len(GEN1_FIELDS)} (GEN1), got {len(parts)}"
            ),
        )

    frame, error = _convert(names, parts)
    if error is not None:
        return ParseResult(kind="frame", ok=False, raw=raw,
                           generation=generation, error=error)

    # GEN1 carries no chute field. Absent is not the same as "not deployed", so it is
    # None rather than 0 — the UI must show "unknown", never a reassuring "armed".
    if generation == "GEN1":
        frame["chute"] = None

    # Neither older generation carries the GEN3.1 fields. Set them explicitly rather
    # than leaving the keys missing, so every frame this module emits has the same shape
    # whatever produced it — a consumer should never have to ask which generation it is
    # holding to know which keys are safe to read.
    for name in GEN3_EXTENDED_FIELDS:
        frame[name] = None

    return ParseResult(kind="frame", ok=True, raw=raw, frame=frame,
                       generation=generation, warnings=_check_ranges(frame))


# ----------------------------------------------------------------------- shared


def _convert(names, values) -> tuple[dict, str | None]:
    frame: dict[str, float | int | None] = {}
    for name, token in zip(names, values):
        token = token.strip()
        if name == "chute":
            token = _strip_chute_prefix(token)
        try:
            value = float(token)
        except ValueError:
            return {}, f"field {name!r} is not numeric: {token!r}"
        if not math.isfinite(value):
            return {}, f"field {name!r} is not finite: {token!r}"
        frame[name] = int(value) if name in _INT_FIELDS else value
    return frame, None


def _strip_chute_prefix(token: str) -> str:
    """``CHUTE:1`` -> ``1``. Bare ``0``/``1`` passes through untouched.

    GEN2 emitted the prefixed form (ISS-07). GEN3 emits a bare integer, and the value is
    now a *count* of eject commands received rather than a flag — 0 armed, N commanded.
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
