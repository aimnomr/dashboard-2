"""GEN3 parser tests, against real captured hardware output.

`test_parser.py` covers GEN1 and GEN2 with hand-written lines. Those prove the parser
handles input we imagined. This file proves it handles input the flight unit actually
produced: two SD captures, 1013 packets, every checksum computed by the firmware rather
than by Python.

That distinction matters most for `crc16_ccitt`. Checking it against CRCs this module
generated itself would only prove it agrees with itself. Checking it against 1013 CRCs
that came off the vehicle proves it agrees with `crc16Ccitt()` in Packet.ino, which is
the thing that actually has to be true.

The corpus is pure happy path — no gaps, no corruption, no restarts. That is a property
of a bench run on a clean link, not a limitation to work around here: sequence gaps,
duplicates and vehicle restarts belong to `linkstats.py` and are exercised by the
fault-injecting mock source, because no captured file contains them.

Design rules referenced below (S1, S6, S8) are from wiki/decisions/dashboard-gen3-plan.md.
"""

from __future__ import annotations

import pytest

from dashboard.parser import (
    GEN3_EXTENDED_FIELDS,
    GEN3_VEHICLE_FIELDS,
    crc16_ccitt,
    parse_line,
)

from conftest import Corpus

# --------------------------------------------------------------------------- helpers


def body_of(line: str) -> str:
    """The checksummed span: everything between the leading '$' and the '*'."""
    return line[1:line.rfind("*")]


def reframe(body: str) -> str:
    """Rebuild a packet around `body` with a checksum that is correct *for that body*.

    Only used where a test needs to reach a code path that sits BEHIND the checksum
    gate — malformed shape, wrong field count. Damaging a line without recomputing the
    CRC would be rejected at the gate and the test would pass without ever reaching what
    it meant to check.

    This uses the function under test to build its own input, which would be circular in
    isolation. It is not, because `test_every_packet_carries_a_checksum_the_firmware_
    computed` independently pins `crc16_ccitt` against the vehicle's own output.
    """
    return f"${body}*{crc16_ccitt(body.encode('utf-8')):04X}"


# -------------------------------------------------------------------- corpus sweep


def test_every_packet_carries_a_checksum_the_firmware_computed(corpus: Corpus):
    """The headline regression: 1013 CRCs from the vehicle, all reproduced here.

    Any change to crc16_ccitt, to the checksummed span, or to field order breaks this
    immediately — which is the point. It is the only test in the suite whose expected
    values were produced by the hardware rather than by us.
    """
    assert corpus.failures == [], [r.error for r in corpus.failures[:3]]
    assert corpus.frames, "corpus contains no telemetry"
    assert all(r.crc_ok is True for r in corpus.frames)
    assert all(r.generation == "GEN3" for r in corpus.frames)
    assert all(r.team == "MRC" for r in corpus.frames)


def test_header_lines_are_status_not_frames(corpus: Corpus):
    """The SD log opens with two '#' comment lines. They must not reach the charts."""
    headers = [r for r in corpus.status_lines if r.raw.startswith("#")]
    assert len(headers) == 2
    assert all(r.ok and r.frame is None for r in headers)


def test_sequence_numbers_are_contiguous(corpus: Corpus):
    """Recorded straight to SD, so every packet the vehicle sent is present.

    This pins the parser's reporting of `seq`, and documents that the corpus has no gaps
    to find — loss arithmetic cannot be tested from this data.
    """
    seqs = [r.seq for r in corpus.frames]
    assert seqs == list(range(seqs[0], seqs[0] + len(seqs)))
    assert seqs[0] == 1


def test_vehicle_clock_advances_exactly_one_second(corpus: Corpus):
    """The 1000 ms cadence claim (devlog 021), as an executable assertion.

    Both a firmware property and a parser one: if `ms` were ever parsed as a float, or
    swapped with `seq`, the interval would stop being exact.
    """
    stamps = [r.vehicle_ms for r in corpus.frames]
    intervals = {b - a for a, b in zip(stamps, stamps[1:])}
    assert intervals == {1000}


def test_frame_carries_every_field_and_absent_link_quality_is_none(corpus: Corpus):
    """`seq` and `ms` are lifted out of the frame; rssi/snr are absent, not zero.

    A packet read from SD never travelled over the air, so there is no RSSI to report.
    Defaulting it to 0 would render as a real -0 dBm reading — a measurement nobody took.
    """
    # The corpus is GEN3.0, so the GEN3.1 fields are present as keys but None —
    # which is what keeps these captures usable as a regression corpus after the
    # format bump. See devlog 048.
    expected = ((set(GEN3_VEHICLE_FIELDS) - {"seq", "ms"})
                | {"rssi", "snr"} | set(GEN3_EXTENDED_FIELDS))
    for result in corpus.frames:
        assert set(result.frame) == expected
        assert result.frame["rssi"] is None
        assert result.frame["snr"] is None


def test_no_range_warnings_across_the_corpus(corpus: Corpus):
    """Real bench data sits inside the plausibility bounds, so nothing should warn.

    Guards the bounds from being tightened until they fire on ordinary data, which would
    train the operator to ignore the warning channel.
    """
    noisy = [(r.seq, r.warnings) for r in corpus.frames if r.warnings]
    assert noisy == []


def test_numeric_types_are_stable(corpus: Corpus):
    """Counts are ints, measurements are floats. `chute` being a float would be a bug
    that only shows up when it reaches the UI as '1.0 commands received'."""
    for result in corpus.frames:
        assert isinstance(result.seq, int)
        assert isinstance(result.vehicle_ms, int)
        assert isinstance(result.frame["sat"], int)
        assert isinstance(result.frame["chute"], int)
        assert isinstance(result.frame["temp"], float)
        assert isinstance(result.frame["alt"], float)


def test_gps_is_reported_untouched(corpus: Corpus):
    """ISS-14: the GPS has never worked, so the whole corpus reads 0,0 with 0 satellites.

    The parser does not judge fix validity — filtering is the display's job, so the zeros
    stay visible in the raw feed rather than being quietly turned into nulls upstream.
    """
    for result in corpus.frames:
        assert result.frame["lat"] == 0.0
        assert result.frame["lng"] == 0.0
        assert result.frame["sat"] == 0


def test_chute_is_armed_throughout_and_is_a_count(corpus: Corpus):
    """S8: GEN3 `chute` counts eject commands received. 0 means armed, not 'not deployed'.

    Distinct from GEN1, where the field is absent entirely and parses to None. Both render
    differently in the UI (ARMED vs UNKNOWN), so they must not collapse into each other.
    """
    assert {r.frame["chute"] for r in corpus.frames} == {0}


def test_the_corpus_is_the_size_it_claims_to_be(all_corpora: list[Corpus]):
    """Both files are committed fixtures, so these totals are fixed.

    If this fails, the corpus was replaced rather than the parser broken — and every
    figure quoted from it (in parser.py's docstring, in the devlog) needs revisiting.
    """
    sizes = {c.name: len(c.frames) for c in all_corpora}
    assert sizes == {"FLIGHT21.CSV": 786, "FLIGHT22.CSV": 227}
    assert sum(sizes.values()) == 1013


# ---------------------------------------------------------------- damaged packets


class TestDamagedPackets:
    """Every case starts from a real captured line and breaks one thing about it.

    Inventing a broken packet risks passing for the wrong reason — because the undamaged
    remainder was also invented. Here the remainder is exactly what the firmware emits.
    """

    def test_a_flipped_byte_fails_the_checksum(self, real_line: str):
        damaged = real_line.replace("30.23", "80.23", 1)
        result = parse_line(damaged)

        assert result.ok is False
        assert result.crc_ok is False
        assert result.frame is None
        # The error names both figures, so a mismatch can be diagnosed from the feed
        # alone rather than by re-deriving the CRC by hand.
        assert "computed" in result.error and "packet says" in result.error

    def test_a_corrupt_sequence_number_is_never_reported(self, real_line: str):
        """S1, and the subtlest rule in the plan.

        A failed checksum means every field is suspect, `seq` included. Reporting it
        anyway would feed a fabricated sequence number into link accounting, producing a
        phantom gap or a false vehicle restart at exactly the moment the link is worst
        and the numbers are being relied on most.
        """
        damaged = real_line.replace("$MRC,1,", "$MRC,4242,", 1)
        result = parse_line(damaged)

        assert result.crc_ok is False
        assert result.seq is None
        assert result.vehicle_ms is None
        # The number is plainly there in the text — and still not reported.
        assert "4242" in result.raw
        # Team survives, so the raw feed can say whose corrupted packet this was.
        assert result.team == "MRC"

    def test_a_truncated_checksum_is_rejected_not_guessed(self, real_line: str):
        result = parse_line(real_line[:real_line.rfind("*") + 3])
        assert result.ok is False
        assert "truncated" in result.error

    def test_a_non_hexadecimal_checksum_is_rejected(self, real_line: str):
        result = parse_line(real_line[:real_line.rfind("*") + 1] + "ZZZZ")
        assert result.ok is False
        assert "hexadecimal" in result.error

    def test_a_missing_checksum_marker_is_rejected(self, real_line: str):
        result = parse_line(real_line.replace("*", "", 1))
        assert result.ok is False
        assert "'*'" in result.error

    def test_field_count_is_checked_behind_the_checksum_gate(self, real_line: str):
        """Shape is only worth checking once the bytes are known to be intact.

        Built with a recomputed CRC precisely to prove the ordering: this reaches the
        field-count branch, which a genuinely corrupt packet never would.
        """
        short = ",".join(body_of(real_line).split(",")[:-1])
        result = parse_line(reframe(short))

        assert result.crc_ok is True
        assert result.ok is False
        assert "18" in result.error and "17" in result.error

    def test_junk_after_the_checksum_is_rejected(self, real_line: str):
        result = parse_line(real_line + "garbage")
        assert result.ok is False
        assert "after the checksum" in result.error

    def test_partial_link_quality_is_rejected(self, real_line: str):
        """One value where two are expected. Guessing which one arrived would put an
        SNR figure on the RSSI panel."""
        result = parse_line(real_line + ",-69.0")
        assert result.ok is False
        assert "link-quality" in result.error

    @pytest.mark.parametrize(
        "damage",
        [
            lambda ln: ln.replace("30.23", "80.23", 1),       # checksum mismatch
            lambda ln: ln[:ln.rfind("*") + 3],                # truncated checksum
            lambda ln: ln.replace("*", "", 1),                # no marker
            lambda ln: ln + "garbage",                        # junk tail
        ],
        ids=["mismatch", "truncated", "no-marker", "junk-tail"],
    )
    def test_the_original_text_always_survives(self, real_line: str, damage):
        """Nothing is silently dropped. Corruption must reach the operator as corruption,
        rather than as a dashboard that has quietly stopped moving."""
        broken = damage(real_line)
        result = parse_line(broken)
        assert result.ok is False
        assert result.raw == broken.strip()
        assert result.error


# ------------------------------------------------------- the ground-station shape


def test_a_ground_station_line_is_the_same_packet_plus_link_quality(real_line: str):
    """The same bytes reach the dashboard by two routes, and both must parse.

    Over the air the ground station appends rssi/snr after the checksum; read from SD
    they are absent. The checksum covers only the vehicle's own span, so appending them
    leaves it valid — which is what makes a replay source pointed at this very file
    possible without converting anything.
    """
    from_sd = parse_line(real_line)
    over_air = parse_line(real_line + ",-69.0,12.8")

    assert over_air.ok and over_air.crc_ok is True
    assert over_air.seq == from_sd.seq
    assert over_air.vehicle_ms == from_sd.vehicle_ms
    assert over_air.frame["rssi"] == pytest.approx(-69.0)
    assert over_air.frame["snr"] == pytest.approx(12.8)

    # Identical in every respect except the two appended fields.
    assert {k: v for k, v in over_air.frame.items() if k not in ("rssi", "snr")} == \
           {k: v for k, v in from_sd.frame.items() if k not in ("rssi", "snr")}


def test_a_commanded_chute_stays_an_integer_count(real_line: str):
    """S8: `N >= 1` is 'COMMANDED xN', so the count itself has to survive intact.

    Collapsing it to a boolean would lose how many eject commands actually reached the
    vehicle, which is the only evidence available that any of them did — the servo has
    no feedback sensor.
    """
    body = body_of(real_line)
    commanded = reframe(body[:body.rfind(",")] + ",3")
    result = parse_line(commanded)

    assert result.ok
    assert result.frame["chute"] == 3
    assert isinstance(result.frame["chute"], int)


# --------------------------------------------------------------- GEN3.1, appended fields


def _framed(body: str) -> str:
    """Wrap a comma body in the `$…*CRC` envelope with a real checksum."""
    return f"${body}*{crc16_ccitt(body.encode()):04X}"


_GEN30_BODY = (
    "MRC,42,42000,29.73,75.1,1013.35,-0.2,"
    "0.002,-0.001,0.911,-0.10,0.21,-0.01,"
    "2.92717,101.76009,1.6,9,0"
)
_GEN31_BODY = _GEN30_BODY + ",4,1.2,1"


def test_gen31_packet_parses_with_all_three_new_fields():
    result = parse_line(_framed(_GEN31_BODY))
    assert result.ok and result.crc_ok
    assert result.frame["ul"] == 4
    assert result.frame["hdop"] == pytest.approx(1.2)
    assert result.frame["fixq"] == 1
    assert result.warnings == []


def test_gen30_packet_still_parses_after_the_bump():
    """The whole reason the fields were appended rather than inserted.

    FLIGHT21/22.CSV are GEN3.0 and are what pins this parser against real firmware
    output. A parser that only accepted the new shape would have discarded its own
    regression corpus on the day the format changed.
    """
    result = parse_line(_framed(_GEN30_BODY))
    assert result.ok and result.crc_ok
    assert result.frame["chute"] == 0
    assert all(result.frame[n] is None for n in GEN3_EXTENDED_FIELDS)


def test_appending_did_not_move_any_existing_field():
    """Every GEN3.0 value must read identically out of a GEN3.1 packet."""
    old = parse_line(_framed(_GEN30_BODY)).frame
    new = parse_line(_framed(_GEN31_BODY)).frame
    for name, value in old.items():
        if name in GEN3_EXTENDED_FIELDS:
            continue
        assert new[name] == value, f"{name} moved: {value!r} -> {new[name]!r}"


def test_a_half_extended_packet_is_rejected_not_guessed():
    """18 or 19 vehicle fields is a truncation that survived its checksum, or firmware
    caught mid-edit. Parsing whatever prefix fits would put real numbers in the wrong
    columns, which is worse than refusing."""
    for partial in (",4", ",4,1.2"):
        result = parse_line(_framed(_GEN30_BODY + partial))
        assert not result.ok
        assert "GEN3.0" in result.error and "GEN3.1" in result.error


def test_fixq_minus_one_is_carried_not_clamped():
    """-1 means "the receiver never reported", which is not 0 ("it says invalid")."""
    body = _GEN30_BODY + ",0,0.0,-1"
    result = parse_line(_framed(body))
    assert result.ok
    assert result.frame["fixq"] == -1
    assert result.frame["hdop"] == 0.0
    assert result.warnings == []


def test_checksum_still_covers_the_new_fields():
    """The CRC is computed over the whole body, so corrupting a new field must reject."""
    good = _framed(_GEN31_BODY)
    corrupted = good.replace(",4,1.2,1*", ",9,1.2,1*")
    assert parse_line(good).ok
    assert not parse_line(corrupted).ok
    assert "checksum mismatch" in parse_line(corrupted).error
