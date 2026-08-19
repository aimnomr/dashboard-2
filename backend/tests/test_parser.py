"""Parser tests.

The parser is the one place where a silent mistake corrupts the whole flight record,
so the cases here are mostly about what must NOT happen: no silent drops, no
plausible-looking output from damaged input, no reassuring defaults for missing data.
"""

from __future__ import annotations

import pytest

from dashboard.parser import GEN1_FIELDS, GEN2_FIELDS, GEN3_EXTENDED_FIELDS, parse_line

GEN2_LINE = (
    "32.50,78.0,1007.90,0.00,"
    "0.012,-0.008,1.003,"
    "0.31,-0.22,0.10,"
    "3.078301,101.712204,0.42,9,CHUTE:0,"
    "-55.6,9.33"
)

GEN1_LINE = (
    "32.5,78.0,1007.9,0.0,"
    "0.01,-0.01,1.00,"
    "0.3,-0.2,0.1,"
    "3.07830,101.71220,0.4,9,"
    "-55.6,9.33"
)


def test_gen2_line_parses_all_seventeen_fields():
    result = parse_line(GEN2_LINE)
    assert result.ok
    assert result.kind == "frame"
    assert result.generation == "GEN2"
    # Plus the GEN3.1 fields as None. Every frame the parser emits carries the same
    # keys whatever produced it, so a consumer never has to ask which generation it
    # is holding before deciding which keys are safe to read.
    assert set(result.frame) == set(GEN2_FIELDS) | set(GEN3_EXTENDED_FIELDS)
    assert all(result.frame[n] is None for n in GEN3_EXTENDED_FIELDS)
    assert result.frame["temp"] == pytest.approx(32.50)
    assert result.frame["rssi"] == pytest.approx(-55.6)
    assert result.warnings == []


def test_chute_prefix_is_stripped():
    """ISS-07: the CHUTE: prefix is removed here, not in firmware."""
    assert parse_line(GEN2_LINE).frame["chute"] == 0
    deployed = GEN2_LINE.replace("CHUTE:0", "CHUTE:1")
    assert parse_line(deployed).frame["chute"] == 1


def test_bare_chute_value_also_accepted():
    """If firmware later drops the prefix (ISS-07 option a), nothing here breaks."""
    assert parse_line(GEN2_LINE.replace("CHUTE:0", "0")).frame["chute"] == 0


def test_gen1_line_still_parses():
    result = parse_line(GEN1_LINE)
    assert result.ok
    assert result.generation == "GEN1"
    assert set(result.frame) == set(GEN1_FIELDS) | {"chute"} | set(GEN3_EXTENDED_FIELDS)


def test_gen1_chute_is_none_not_zero():
    """Absent is not 'not deployed'. The UI must show unknown, never a calm ARMED."""
    assert parse_line(GEN1_LINE).frame["chute"] is None


def test_legacy_extended_fields_are_none_not_zero():
    """The same rule, for the GEN3.1 fields on firmware that predates them.

    `ul=0` is a claim: "the uplink has never worked". `fixq=0` is a claim: "the receiver
    says the fix is invalid". Neither may be manufactured on behalf of firmware that
    said nothing at all — the difference is exactly the one an operator would act on.
    """
    for name in GEN3_EXTENDED_FIELDS:
        assert parse_line(GEN1_LINE).frame[name] is None
        assert parse_line(GEN2_LINE).frame[name] is None


def test_gps_precision_is_preserved():
    """GEN2 carries two more decimals than GEN1 - about 1.1 m to 0.11 m."""
    assert parse_line(GEN2_LINE).frame["lat"] == pytest.approx(3.078301, abs=1e-9)


def test_integer_fields_are_integers():
    frame = parse_line(GEN2_LINE).frame
    assert isinstance(frame["sat"], int)
    assert isinstance(frame["chute"], int)


@pytest.mark.parametrize(
    "line",
    [
        "32.5,78.0,1007.9",                       # truncated
        GEN2_LINE + ",99",                        # extra field
        GEN2_LINE.replace("32.50", "��"),  # garbled bytes from errors="replace"
        GEN2_LINE.replace("CHUTE:0", "CHUTE:X"),  # damaged chute flag
    ],
)
def test_malformed_lines_fail_without_raising(line):
    result = parse_line(line)
    assert not result.ok
    assert result.frame is None
    assert result.error
    # The original text always survives, so the operator sees corruption rather than
    # a dashboard that has quietly stopped moving.
    assert result.raw == line.strip()


def test_field_count_error_names_both_generations():
    assert "17" in parse_line("1,2,3").error
    assert "16" in parse_line("1,2,3").error


def test_ground_unit_status_lines_are_not_parsed():
    result = parse_line("[GCS] Timeout - no packet")
    assert result.kind == "status"
    assert result.ok
    assert result.frame is None


def test_blank_lines_are_ignored():
    assert parse_line("   ").kind == "empty"


def test_implausible_values_warn_but_still_parse():
    """A strange flight must still reach the screen. Range checks never reject."""
    line = GEN2_LINE.replace("32.50", "180.00", 1)
    result = parse_line(line)
    assert result.ok
    assert result.frame["temp"] == pytest.approx(180.0)
    assert any("temp" in w for w in result.warnings)


def test_negative_altitude_does_not_warn():
    """CANSAT_DATA reached -14.3 m: altitude is relative to boot and drifts."""
    line = GEN2_LINE.replace(",0.00,", ",-14.30,", 1)
    result = parse_line(line)
    assert result.ok
    assert result.warnings == []


class TestCapturedHardwarePacket:
    """A real GEN1 line captured from the test firmware on 2026-08-18.

    Captured output beats invented fixtures: this one happens to exercise the no-fix
    filter and the absent-chute path at the same time, both of which are places where a
    plausible-looking wrong answer would reach the operator.
    """

    LINE = (
        "31.5,70.4,1010.0,-0.9,0.92,0.38,-0.05,-0.4,-0.3,4.1,"
        "0.00000,0.00000,0.0,0,-11.0,12.25"
    )

    def test_parses_as_gen1_without_warnings(self):
        result = parse_line(self.LINE)
        assert result.ok
        assert result.generation == "GEN1"
        assert result.warnings == []

    def test_negative_altitude_is_accepted(self):
        # -0.9 m: altitude is relative to boot and drifts with barometric pressure.
        assert parse_line(self.LINE).frame["alt"] == pytest.approx(-0.9)

    def test_absent_chute_is_none(self):
        assert parse_line(self.LINE).frame["chute"] is None

    def test_invalid_gps_survives_parsing_as_zeros(self):
        # The parser does not judge fix validity — it reports what arrived. Filtering
        # 0,0 is the display's job, so that the raw value stays visible in the feed.
        frame = parse_line(self.LINE).frame
        assert frame["lat"] == 0.0
        assert frame["lng"] == 0.0
        assert frame["sat"] == 0

    def test_very_strong_link_is_not_flagged_implausible(self):
        # -11 dBm is hotter than anything in CANSAT_DATA (max -14) because the units
        # were adjacent on a bench. Real, and must not be treated as corruption.
        result = parse_line(self.LINE)
        assert result.frame["rssi"] == pytest.approx(-11.0)
        assert result.frame["snr"] == pytest.approx(12.25)
        assert result.warnings == []


def test_non_finite_values_are_rejected():
    for token in ("nan", "inf", "-inf"):
        result = parse_line(GEN2_LINE.replace("32.50", token, 1))
        assert not result.ok, token
