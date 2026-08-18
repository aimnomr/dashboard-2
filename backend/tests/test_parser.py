"""Parser tests.

The parser is the one place where a silent mistake corrupts the whole flight record,
so the cases here are mostly about what must NOT happen: no silent drops, no
plausible-looking output from damaged input, no reassuring defaults for missing data.
"""

from __future__ import annotations

import pytest

from dashboard.parser import GEN1_FIELDS, GEN2_FIELDS, parse_line

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
    assert set(result.frame) == set(GEN2_FIELDS)
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
    assert set(result.frame) == set(GEN1_FIELDS) | {"chute"}


def test_gen1_chute_is_none_not_zero():
    """Absent is not 'not deployed'. The UI must show unknown, never a calm ARMED."""
    assert parse_line(GEN1_LINE).frame["chute"] is None


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


def test_non_finite_values_are_rejected():
    for token in ("nan", "inf", "-inf"):
        result = parse_line(GEN2_LINE.replace("32.50", token, 1))
        assert not result.ok, token
