"""Replay source tests.

The property that matters most here is that replay changes *nothing* about the data. It
is worth having a dedicated file for, because a replay source that quietly tidies its
input is worse than no replay source at all: every test run against it would be a test
against a feed the real link never produces.

Tests that care about *content* run with `interval=0`, which emits with no sleeping at
all. Raising `speed` instead does not work: the delay stays greater than zero, and
Windows rounds every sub-15 ms sleep up to its timer granularity, so a 229-line capture
costs several seconds however fast it is nominally played.

Tests that care about *pacing* assert on the computed delay rather than on wall-clock
time. Sleeping accurately enough to measure would make them both slow and flaky, and the
arithmetic is one division — there is nothing a real sleep would additionally prove.
"""

from __future__ import annotations

import asyncio

import pytest

from dashboard.parser import crc16_ccitt, parse_line
from dashboard.sources.file_source import DEFAULT_INTERVAL, FileSource

from conftest import FIXTURES

CAPTURE = FIXTURES / "FLIGHT22.CSV"


def packet(seq: int, ms: int) -> str:
    """A well-formed GEN3 packet with a correct checksum.

    Pacing reads the clock through the parser, which rejects anything failing CRC — so a
    hand-written line with a made-up checksum never reaches the code under test. It has
    to be a packet the parser accepts, or the test silently exercises the corrupt path
    instead of the one it names.
    """
    body = (f"MRC,{seq},{ms},30.00,70.0,1009.00,0.0,"
            "0.000,0.000,1.000,0.00,0.00,0.00,"
            "0.00000,0.00000,0.0,0,0")
    return f"${body}*{crc16_ccitt(body.encode('utf-8')):04X}"


async def collect(source: FileSource, limit: int | None = None) -> list[str]:
    out: list[str] = []
    async for line in source.lines():
        out.append(line)
        if limit is not None and len(out) >= limit:
            await source.aclose()
            break
    return out


# ------------------------------------------------------------------- fidelity


def test_replay_emits_the_file_unchanged():
    """Byte for byte, including the two '#' headers.

    A source that filtered its own input would be testing the dashboard against a feed
    that cannot occur — and the headers in particular exercise the status path, which is
    exactly the kind of line a tidying replay would swallow.
    """
    emitted = asyncio.run(collect(FileSource(CAPTURE, interval=0)))
    expected = [ln for ln in CAPTURE.read_text(encoding="utf-8").splitlines() if ln.strip()]
    assert emitted == expected


def test_replayed_packets_still_pass_their_checksum():
    """End-to-end proof that nothing was rewritten in transit: if replay touched a single
    byte, the firmware's CRC would stop matching."""
    emitted = asyncio.run(collect(FileSource(CAPTURE, interval=0)))
    frames = [parse_line(ln) for ln in emitted if ln.startswith("$")]
    assert len(frames) == 227
    assert all(r.ok and r.crc_ok for r in frames)


def test_link_quality_is_absent_not_invented():
    """The decisive property of replaying an SD capture.

    These packets crossed no radio, so no RSSI was measured. Synthesising one would put
    a fabricated dBm figure in front of the operator, which is the failure this whole
    project is organised against.
    """
    emitted = asyncio.run(collect(FileSource(CAPTURE, interval=0)))
    frames = [parse_line(ln) for ln in emitted if ln.startswith("$")]
    assert all(r.frame["rssi"] is None for r in frames)
    assert all(r.frame["snr"] is None for r in frames)


# --------------------------------------------------------------------- pacing


def test_pacing_follows_the_vehicle_clock():
    """Gaps come from the capture's own `ms` field, not from an interval we chose.

    The capture is a flat 1000 ms cadence, so at speed 500 each gap is 2 ms. Asserted on
    the computed delay rather than on wall-clock time: sleeping accurately enough to
    measure would make the test both slow and flaky.
    """
    source = FileSource(CAPTURE, speed=500)
    lines = [ln for ln in CAPTURE.read_text(encoding="utf-8").splitlines() if ln.strip()]

    previous = None
    delays = []
    for raw in lines:
        delay, previous = source._delay_before(raw, previous)
        delays.append(delay)

    packet_delays = delays[3:]      # headers, then the first packet, set the baseline
    assert packet_delays
    assert all(d == pytest.approx(1.0 / 500) for d in packet_delays)


def test_a_fixed_interval_overrides_the_vehicle_clock():
    source = FileSource(CAPTURE, speed=2, interval=4.0)
    delay, carried = source._delay_before(packet(5, 5000), None)
    assert delay == pytest.approx(2.0)
    # Nothing is carried forward: fixed pacing never consults the clock again.
    assert carried is None


def test_lines_without_a_usable_clock_do_not_consume_a_slot():
    """Headers, [GCS] status lines and corrupt packets go out immediately.

    On a real link they arrive *between* packets, not instead of them. Delaying them
    would stretch the cadence by one interval every time the link glitched — so the
    replay would slow down exactly when a live feed does not.
    """
    source = FileSource(CAPTURE, speed=1)
    for raw in ("# MRC CanSat GEN3 flight log", "[GCS] Timeout - no packet", "garbage"):
        delay, carried = source._delay_before(raw, 5000)
        assert delay == 0.0
        # The clock is carried across untouched, so the NEXT real packet still measures
        # its gap from the last real packet rather than restarting from nothing.
        assert carried == 5000


@pytest.mark.parametrize(
    "previous_ms, current_ms",
    [
        (200_000, 1_000),      # backwards: the capture contains a reset
        (1_000, 9_000_000),    # enormous: trimmed rows, or a long dropout
    ],
    ids=["backwards", "enormous"],
)
def test_implausible_gaps_fall_back_to_the_default_interval(previous_ms, current_ms):
    """A replay that honoured a 40-minute jump literally would look like a hung process.

    Reproducing the gap is not the goal — reproducing the *data* is. Timing here is a
    presentation choice, unlike the packets themselves.
    """
    source = FileSource(CAPTURE, speed=1)
    delay, _ = source._delay_before(packet(1, current_ms), previous_ms)
    assert delay == pytest.approx(DEFAULT_INTERVAL)


# ------------------------------------------------------------------ behaviour


def test_a_capture_cannot_receive_commands():
    """During a replay the EJECT button must report failure.

    A source that returned True would teach the operator that the control works, in the
    one situation where it provably did nothing.
    """
    source = FileSource(CAPTURE)
    assert asyncio.run(source.send_command("EJECT")) is False


def test_replay_is_flagged_as_simulated():
    """Real data, but not a live link — and the banner is driven off this flag.

    Arguably the easiest thing in the project to get wrong: the data IS genuine, which
    is precisely why somebody could mistake a replay for a flight.
    """
    assert FileSource(CAPTURE).simulated is True
    assert FileSource(CAPTURE).name == "replay"


def test_a_missing_capture_fails_immediately():
    with pytest.raises(FileNotFoundError):
        FileSource(FIXTURES / "NOSUCHFILE.CSV")


def test_a_nonpositive_speed_is_rejected():
    # Would divide every gap into zero or negative time and flood the pipeline.
    with pytest.raises(ValueError):
        FileSource(CAPTURE, speed=0)


def test_closing_stops_the_replay_early():
    source = FileSource(CAPTURE, interval=0)
    assert len(asyncio.run(collect(source, limit=5))) == 5
