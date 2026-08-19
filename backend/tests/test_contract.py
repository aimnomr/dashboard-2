"""The self-describing log header.

A `.log` outlives its session and usually the person who made it. These tests are mostly
about the header staying TRUE — a contract that drifts from the parser is worse than no
contract, because it is believed.
"""

from __future__ import annotations

from datetime import datetime, timezone

import pytest

from dashboard.contract import CONTRACT_VERSION, packet_contract
from dashboard.parser import (
    FIELD_DOC,
    GEN3_EXTENDED_FIELDS,
    GEN3_LINK_FIELDS,
    GEN3_VEHICLE_FIELDS,
    parse_line,
)

ALL_FIELDS = (*GEN3_VEHICLE_FIELDS, *GEN3_EXTENDED_FIELDS, *GEN3_LINK_FIELDS)


@pytest.fixture(scope="module")
def header() -> str:
    return packet_contract("serial", datetime(2026, 8, 20, 12, 0, tzinfo=timezone.utc))


def test_field_doc_covers_every_field():
    """The drift guard, and the reason the header is generated rather than written.

    Adding a field to the wire format without documenting it fails here, so the logs
    cannot quietly fall behind the parser the way documentation usually does.
    """
    missing = [f for f in ALL_FIELDS if f not in FIELD_DOC]
    assert not missing, f"undocumented fields: {missing}"


def test_field_doc_has_no_fields_that_do_not_exist():
    """The other direction: a field removed from the wire must leave the docs too."""
    extra = set(FIELD_DOC) - set(ALL_FIELDS)
    assert not extra, f"documented but not on the wire: {extra}"


def test_every_line_is_a_comment(header: str):
    """The whole reason this is safe to put inside the log."""
    for ln in header.splitlines():
        assert ln.startswith("#"), f"non-comment line in header: {ln!r}"


def test_the_parser_skips_the_entire_header(header: str):
    """Replay must read straight through it — nothing to strip, nothing rejected."""
    for ln in header.splitlines():
        result = parse_line(ln)
        assert result.kind == "status", f"header line parsed as {result.kind}: {ln!r}"
        assert result.ok


def test_header_names_every_field(header: str):
    for name in ALL_FIELDS:
        assert f" {name} " in header or f" {name:<6}" in header, f"{name} missing"


def test_header_carries_the_contract_version(header: str):
    assert CONTRACT_VERSION in header


def test_header_warns_about_the_sentinels_that_look_like_data(header: str):
    """The traps someone reading raw numbers cannot infer, and would get wrong."""
    assert "0.00000" in header and "NO FIX" in header      # not a position off Africa
    assert "never 0" in header or "never a perfect fix" in header   # hdop 0
    assert "-1" in header                                   # fixq not reported


def test_header_does_not_claim_the_chute_confirms_deployment(header: str):
    """S8. The one statement this project must never make, on any surface.

    Checked as a property rather than by banning the word: the header is *required* to
    say "never deployed", so a naive substring ban would fail on the very disclaimer it
    exists to enforce. What must not appear is the word standing alone as a claim.
    """
    lowered = header.lower()
    assert "commanded" in lowered and "never deployed" in lowered

    for ln in lowered.splitlines():
        if "deployed" in ln:
            assert "never deployed" in ln, f"unqualified deployment claim: {ln!r}"
        if "deployment" in ln:
            assert "not deployment" in ln, f"unqualified deployment claim: {ln!r}"


def test_header_is_written_once_and_before_any_telemetry(tmp_path):
    from dashboard.rawlog import RawLog

    log = RawLog.create(tmp_path, "serial")
    log.write("$MRC,1,1000,20.0,50.0,1013.0,0.0,0,0,1,0,0,0,0,0,0,0,0,0,0.0,-1*0000")
    log.close()

    text = log.path.read_text(encoding="utf-8")
    lines = text.splitlines()

    comment_count = sum(1 for ln in lines if ln.startswith("#"))
    first_data = next(i for i, ln in enumerate(lines) if not ln.startswith("#"))

    # Every comment line precedes every data line: the header is a block at the top,
    # not something interleaved per write.
    assert all(lines[i].startswith("#") for i in range(first_data))
    assert not any(ln.startswith("#") for ln in lines[first_data:])
    assert comment_count == first_data


def test_sidecar_still_exists_and_names_the_contract(tmp_path):
    """The header is for people; the sidecar is for programs. Both, not either."""
    import json

    from dashboard.rawlog import RawLog

    log = RawLog.create(tmp_path, "mock")
    log.close()

    meta = json.loads(log.path.with_suffix(".meta.json").read_text(encoding="utf-8"))
    assert meta["contract"] == CONTRACT_VERSION
    assert meta["source"] == "mock"


def test_a_written_log_replays_cleanly(tmp_path):
    """End to end: header plus real packets, read back through the parser."""
    from dashboard.parser import crc16_ccitt
    from dashboard.rawlog import RawLog

    body = ("MRC,7,7000,29.73,75.1,1013.35,-0.2,0.002,-0.001,0.911,"
            "-0.10,0.21,-0.01,2.92717,101.76009,1.6,9,0,4,1.2,1")
    packet = f"${body}*{crc16_ccitt(body.encode()):04X}"

    log = RawLog.create(tmp_path, "serial")
    log.write(packet)
    log.write("[GCS] EJECT armed")
    log.close()

    frames, statuses = 0, 0
    for ln in log.path.read_text(encoding="utf-8").splitlines():
        result = parse_line(ln)
        assert result.ok, f"line rejected on replay: {ln!r}"
        if result.kind == "frame":
            frames += 1
        elif result.kind == "status":
            statuses += 1

    assert frames == 1
    assert statuses > 1        # the header plus the [GCS] line


def test_a_header_never_consumes_replay_cadence(tmp_path):
    """A 167-line header must not delay a replay, in EITHER pacing mode.

    Under fixed-interval pacing this cost 167 intervals before the first packet —
    nearly three minutes of an empty dashboard at `--interval 1`. The clock-paced
    branch was already correct; the interval branch was not.
    """
    from dashboard.sources.file_source import FileSource

    src = FileSource.__new__(FileSource)
    src.speed = 1.0

    header_line = "# contract   GEN3.1 (2026-08-20)"
    gcs_line = "[GCS] EJECT armed"

    for interval in (None, 1.0):
        src.interval = interval
        for line in (header_line, gcs_line):
            delay, _ = src._delay_before(line, previous_ms=5000)
            assert delay == 0.0, f"{line!r} delayed {delay}s at interval={interval}"

    # A real frame still paces normally under a fixed interval.
    src.interval = 1.0
    body = "MRC,2,6000,20,50,1013,0,0,0,1,0,0,0,0,0,0,0,0,0,0.0,-1"
    from dashboard.parser import crc16_ccitt
    packet = f"${body}*{crc16_ccitt(body.encode()):04X}"
    delay, _ = src._delay_before(packet, previous_ms=5000)
    assert delay == 1.0
