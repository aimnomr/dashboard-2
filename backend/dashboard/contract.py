"""The packet contract, rendered as a comment block for the head of a raw log.

A `.log` file outlives the session that produced it, and usually the person who
produced it. Six weeks later someone opens one and finds 400 lines of comma-separated
numbers with no indication of what column 14 is, whether `0.00000` is a coordinate or a
sentinel, or which fields the checksum actually covers. Answering that from the codebase
means finding the right firmware revision, which is a worse problem than it sounds once
the format has changed twice.

So the log explains itself. Every field, every unit, every sentinel, and the traps that
are not guessable from the numbers.

**Every line here starts with `#`.** `parse_line()` classifies those as status lines, so
a header costs a replay nothing: `file_source` reads them, the parser skips them, and the
pipeline reports them the same way it reports a `[GCS]` line. That is what makes putting
this *inside* the log safe, where the original design deliberately kept it outside.

The field table is generated from `parser.FIELD_DOC`, not written here. A contract that
is maintained separately from the parser is a contract that is wrong within two revisions.
"""

from __future__ import annotations

from datetime import datetime, timezone

from .parser import (
    FIELD_DOC,
    GEN1_FIELDS,
    GEN2_FIELDS,
    GEN3_EXTENDED_FIELDS,
    GEN3_LINK_FIELDS,
    GEN3_VEHICLE_FIELDS,
    _PLAUSIBLE,
)

#: Bumped when the wire format changes, so a reader can tell which contract a file
#: was written under even if the header text itself is later reworded.
CONTRACT_VERSION = "GEN3.1 (2026-08-20)"


def _wrap(text: str, width: int, indent: str) -> list[str]:
    words, lines, cur = text.split(), [], ""
    for w in words:
        if cur and len(cur) + 1 + len(w) > width:
            lines.append(cur)
            cur = w
        else:
            cur = f"{cur} {w}".strip()
    if cur:
        lines.append(cur)
    return [lines[0]] + [indent + ln for ln in lines[1:]] if lines else [""]


def packet_contract(source_name: str, started_at: datetime | None = None) -> str:
    """The full header block. Returns text; every line begins with '#'."""
    started = (started_at or datetime.now(timezone.utc)).isoformat()
    out: list[str] = []

    def line(s: str = "") -> None:
        out.append(("# " + s).rstrip())

    def rule(ch: str = "-") -> None:
        line(ch * 76)

    rule("=")
    line("MRC CanSat telemetry - RAW LOG")
    rule("=")
    line()
    line(f"contract   {CONTRACT_VERSION}")
    line(f"source     {source_name}")
    line(f"started    {started}")
    line()
    line("Every line the PC received, written before anything tried to interpret it.")
    line("Malformed lines, foreign packets and [GCS] status lines are all preserved on")
    line("purpose: this file is the record, not a cleaned-up view of one.")
    line()
    line("Lines starting with '#' are this header. The parser classifies them as status")
    line("lines, so the file can be replayed as-is with nothing to strip.")
    line()

    rule()
    line("WIRE FORMAT")
    rule()
    line()
    line("GEN3.1  $MRC,<20 vehicle fields>*<CRC16>[,rssi,snr]      <- current")
    line("GEN3.0  $MRC,<17 vehicle fields>*<CRC16>[,rssi,snr]      <- before 2026-08-20")
    line("GEN2    <17 comma-separated fields, no marker, no CRC>")
    line("GEN1    <16 comma-separated fields, no chute field>")
    line()
    line("The three GEN3.1 fields are APPENDED, so every GEN3.0 index is unchanged and")
    line("both shapes parse. Generation is detected per line, not per file - a log may")
    line("contain more than one if firmware was reflashed mid-session.")
    line()
    line("CRC16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor,")
    line("computed over everything BETWEEN '$' and '*'. A failed checksum rejects the")
    line("frame outright - a corrupt packet must never move the altitude trace.")
    line()
    line("rssi and snr are appended by the ground station AFTER the '*' checksum, so")
    line("they are NOT covered by it, and they are absent from vehicle SD captures.")
    line()

    rule()
    line("FIELDS - GEN3, in wire order after the $MRC marker")
    rule()
    line()
    line(f"  {'#':>3}  {'name':<6} {'fmt':<5} {'unit':<7} meaning")
    line(f"  {'':>3}  {'-'*6} {'-'*5} {'-'*7} {'-'*40}")

    def emit(names: tuple[str, ...], start: int, tag: str = "") -> int:
        idx = start
        for name in names:
            unit, fmt, meaning = FIELD_DOC[name]
            wrapped = _wrap(meaning, 44, " " * 26)
            line(f"  {idx:>3}  {name:<6} {fmt:<5} {unit:<7} {wrapped[0]}")
            for extra in wrapped[1:]:
                line(f"  {extra}")
            idx += 1
        if tag:
            line(f"      {tag}")
        return idx

    nxt = emit(GEN3_VEHICLE_FIELDS, 1)
    line()
    line("      --- appended by GEN3.1 firmware, absent before 2026-08-20 ---")
    nxt = emit(GEN3_EXTENDED_FIELDS, nxt)
    line()
    line("      --- appended by the ground station, after the checksum ---")
    emit(GEN3_LINK_FIELDS, nxt)
    line()

    rule()
    line("SENTINELS - values that mean 'no data', not a measurement")
    rule()
    line()
    line("  lat/lng  0.00000   no GPS fix. NOT a position off the coast of Africa")
    line("  spd      0.0       no fix (also a genuine reading when stationary)")
    line("  sat      0         nothing tracked")
    line("  hdop     0.0       not reported by the receiver. A real HDOP is never 0")
    line("  fixq     -1        the receiver never sent the field at all")
    line("  rssi/snr absent    read from SD, so no radio measured it. Never rendered as 0")
    line()

    rule()
    line("TRAPS - things the numbers do not tell you")
    rule()
    line()
    for text in [
        "alt is relative to BOOT, not sea level. Negative values are normal.",
        "chute counts commands RECEIVED, not deployments. Nothing on the vehicle can "
        "sense whether the canopy opened, so no reading here ever confirms it.",
        "seq restarts at 1 when the vehicle reboots. Subtracting two seq values across "
        "a restart produces a nonsense loss figure.",
        "ms is the vehicle's clock and stops when it reboots. It cannot measure silence: "
        "a dropped packet carries no timestamp with it.",
        "A GEN3.1 vehicle may report lat/lng of 0 while sat is non-zero. That is the "
        "fix-age check refusing to transmit a position it can no longer confirm.",
        "az reads about 0.92 g at rest on the current unit, not 1.00. Suspected scale "
        "or bias error, unresolved as of 2026-08-20.",
        "The gyro emits occasional single-sample spikes of 70-190 deg/s while the unit "
        "is stationary. Treat one-sample excursions with suspicion.",
    ]:
        wrapped = _wrap(text, 72, "    ")
        line(f"  * {wrapped[0]}")
        for extra in wrapped[1:]:
            line(f"  {extra}")
    line()

    rule()
    line("PLAUSIBILITY BOUNDS - warnings only, never rejections")
    rule()
    line()
    line("A value outside these is flagged and kept. A genuinely anomalous flight must")
    line("still reach the screen; suppressing it would hide the interesting part.")
    line()
    for name in (*GEN3_VEHICLE_FIELDS, *GEN3_EXTENDED_FIELDS, *GEN3_LINK_FIELDS):
        bounds = _PLAUSIBLE.get(name)
        if bounds:
            line(f"  {name:<6} {bounds[0]:>10g} .. {bounds[1]:<10g}")
    line()

    rule()
    line("LEGACY GENERATIONS")
    rule()
    line()
    line(f"  GEN2  {len(GEN2_FIELDS)} fields: " + ",".join(GEN2_FIELDS))
    line("        chute arrives as 'CHUTE:n'; the prefix is stripped by the parser.")
    line()
    line(f"  GEN1  {len(GEN1_FIELDS)} fields: " + ",".join(GEN1_FIELDS))
    line("        No chute field at all. Absent is reported as unknown, never as armed.")
    line()

    rule("=")
    line("Telemetry begins on the next non-# line.")
    rule("=")

    return "\n".join(out)
