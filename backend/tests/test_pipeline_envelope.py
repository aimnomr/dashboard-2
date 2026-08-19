"""Envelope tests — what the browser actually receives.

The envelope is the contract between the backend and every panel. The cases that matter
are the ones where an absent value could be mistaken for a measured one: a GEN1 packet
has no sequence number and no checksum, and reporting either as 0/True would be a
fabricated fact rather than a missing one.
"""

from __future__ import annotations

import asyncio

import pytest

from dashboard.hub import Hub
from dashboard.pipeline import Pipeline
from dashboard.sources.base import TelemetrySource

from conftest import FIXTURES

GEN1_LINE = (
    "31.5,70.4,1010.0,-0.9,0.92,0.38,-0.05,-0.4,-0.3,4.1,"
    "0.00000,0.00000,0.0,0,-11.0,12.25"
)


class _NullLog:
    """Stands in for RawLog. The pipeline writes before it parses, and that ordering is
    tested elsewhere — here the durability layer is only in the way."""

    count = 0

    def write(self, line: str) -> None:
        self.count += 1

    def close(self) -> None:
        pass


class _Source(TelemetrySource):
    name = "test"
    simulated = True

    async def lines(self):
        return
        yield  # pragma: no cover - never reached, makes this an async generator


def envelope_for(line: str) -> dict:
    pipeline = Pipeline(_Source(), _NullLog(), Hub())
    return pipeline._envelope(line)


def first_gen3_line() -> str:
    text = (FIXTURES / "FLIGHT22.CSV").read_text(encoding="utf-8")
    return next(ln for ln in text.splitlines() if ln.startswith("$"))


# ------------------------------------------------------------------------- GEN3


def test_gen3_envelope_carries_the_vehicle_clock():
    """`vehicle_ms` is the moment of sampling, free of link and scheduling jitter.

    Everything derived from the data — descent rate, integrated yaw, the chart x-axis —
    belongs on this clock rather than on arrival time.
    """
    envelope = envelope_for(first_gen3_line())

    assert envelope["seq"] == 1
    assert envelope["vehicle_ms"] == 7859
    assert envelope["crc_ok"] is True
    assert envelope["generation"] == "GEN3"
    assert envelope["ok"] is True


def test_pc_time_is_still_carried_alongside_the_vehicle_clock():
    """Both, because they answer different questions.

    The vehicle clock cannot measure silence: when the link drops there are no packets,
    so it stops advancing from the dashboard's point of view. Staleness must stay on the
    PC clock.
    """
    envelope = envelope_for(first_gen3_line())
    assert envelope["pc_time"]
    assert envelope["vehicle_ms"] is not None


def test_a_corrupt_gen3_packet_reports_no_sequence_data():
    """S1 again, now at the envelope boundary.

    The parser withholds `seq` on a failed checksum; this proves the pipeline does not
    reintroduce it. A fabricated sequence number reaching the browser would produce a
    phantom gap the moment loss accounting is built on top.
    """
    damaged = first_gen3_line().replace("32.51", "82.51", 1)
    envelope = envelope_for(damaged)

    assert envelope["ok"] is False
    assert envelope["crc_ok"] is False
    assert envelope["seq"] is None
    assert envelope["vehicle_ms"] is None
    assert envelope["frame"] is None
    # Still forwarded, so the raw feed can show the corruption.
    assert envelope["raw"] == damaged


# ------------------------------------------------------------------ older packets


def test_gen1_envelope_reports_absent_rather_than_zero():
    """A missing counter is not counter zero, and no checksum is not a passing checksum.

    This is the whole reason these fields are nullable. GEN1 hardware is still in use on
    the bench, so this path is live, not hypothetical.
    """
    envelope = envelope_for(GEN1_LINE)

    assert envelope["ok"] is True
    assert envelope["generation"] == "GEN1"
    assert envelope["seq"] is None
    assert envelope["vehicle_ms"] is None
    assert envelope["crc_ok"] is None


@pytest.mark.parametrize("field", ["seq", "vehicle_ms", "crc_ok"])
def test_the_new_fields_are_always_present_as_keys(field):
    """Present-but-null, never absent.

    An absent key and a null one are indistinguishable in JavaScript, but only one of
    them survives a schema check — and the frontend branches on null, so the key has to
    exist for every generation.
    """
    assert field in envelope_for(GEN1_LINE)
    assert field in envelope_for(first_gen3_line())


def test_rx_index_still_counts_arrivals_not_packets():
    """Unchanged by any of this. It counts lines that arrived, and a gap in it means
    nothing was received — not that something was sent and lost."""
    pipeline = Pipeline(_Source(), _NullLog(), Hub())
    for _ in range(3):
        pipeline._envelope(GEN1_LINE)
    assert pipeline.rx_index == 3

    # Malformed lines still advance it: they arrived.
    pipeline._envelope("nonsense")
    assert pipeline.rx_index == 4


def test_status_lines_carry_no_frame_fields():
    envelope = envelope_for("[GCS] Timeout - no packet")
    assert envelope["type"] == "status"
    assert "seq" not in envelope


# ------------------------------------------------------------- link accounting


def test_gen3_envelope_carries_backend_owned_link_stats():
    """Computed here, not in the browser.

    A client that connects on packet 500 must read the same loss figure as one that
    watched from packet 1. Only the backend has seen the whole session.
    """
    envelope = envelope_for(first_gen3_line())
    link = envelope["link"]

    assert link["expected"] == 1
    assert link["received"] == 1
    assert link["lost"] == 0
    assert link["loss_pct"] == 0.0
    assert link["rolling"]["window"] == 60


def test_gen1_envelope_reports_link_as_null():
    """S5. No counter, so no figure — and null rather than a reassuring zero."""
    assert envelope_for(GEN1_LINE)["link"] is None


def test_a_corrupt_frame_still_reports_link_stats():
    """A checksum failure is link-quality information (S1), so the panel must update.

    The frame is rejected; the *fact of* the rejection is not.
    """
    pipeline = Pipeline(_Source(), _NullLog(), Hub())
    pipeline._envelope(first_gen3_line())
    envelope = pipeline._envelope(first_gen3_line().replace("32.51", "82.51", 1))

    assert envelope["ok"] is False
    assert envelope["link"]["crc_failed"] == 1
    # The corrupt frame's sequence number took no part in the arithmetic.
    assert envelope["link"]["expected"] == 1


def test_an_ordinary_line_produces_exactly_one_message():
    pipeline = Pipeline(_Source(), _NullLog(), Hub())
    assert len(pipeline._messages(first_gen3_line())) == 1


def test_a_reboot_is_announced_before_the_frame_that_revealed_it():
    """Order matters. Sent the other way round, the chart draws a segment across the
    discontinuity before being told there was one."""
    pipeline = Pipeline(_Source(), _NullLog(), Hub())

    text = (FIXTURES / "FLIGHT22.CSV").read_text(encoding="utf-8")
    packets = [ln for ln in text.splitlines() if ln.startswith("$")]

    pipeline._messages(packets[5])          # seq 6
    messages = pipeline._messages(packets[0])   # seq 1 — backwards

    assert [m["type"] for m in messages] == ["vehicle_restart", "frame"]
    assert messages[0]["previous_seq"] == 6
    assert messages[0]["new_seq"] == 1
    assert messages[0]["pc_time"]
    # The frame itself is still delivered, now counting from the new baseline.
    assert messages[1]["link"]["baseline_seq"] == 1
    assert messages[1]["link"]["restarts"] == 1


def test_a_clean_capture_reports_zero_loss_end_to_end():
    """227 contiguous packets through the real pipeline. The figure has to be 0%.

    This is the case that would look right whatever the arithmetic did, which is why it
    is worth pinning: it catches an off-by-one in `expected` that the awkward cases in
    test_linkstats.py would not.
    """
    from dashboard.sources.file_source import FileSource

    async def collect():
        source = FileSource(FIXTURES / "FLIGHT22.CSV", interval=0)
        pipeline = Pipeline(source, _NullLog(), Hub())
        out = []
        async for line in source.lines():
            out.extend(pipeline._messages(line))
        return out

    messages = asyncio.run(collect())
    frames = [m for m in messages if m["type"] == "frame"]
    link = frames[-1]["link"]

    assert not [m for m in messages if m["type"] == "vehicle_restart"]
    assert link["expected"] == 227
    assert link["received"] == 227
    assert link["lost"] == 0
    assert link["loss_pct"] == 0.0
    assert link["crc_failed"] == 0
    assert link["rolling"]["loss_pct"] == 0.0


def test_replayed_capture_produces_a_monotonic_vehicle_clock():
    """End to end over a real capture: 227 envelopes, clock strictly increasing.

    Guards the ordering property every time-derived figure depends on. A clock that went
    backwards mid-flight would silently invert every rate calculation built on it.
    """
    from dashboard.sources.file_source import FileSource

    async def collect():
        source = FileSource(FIXTURES / "FLIGHT22.CSV", interval=0)
        pipeline = Pipeline(source, _NullLog(), Hub())
        out = []
        async for line in source.lines():
            envelope = pipeline._envelope(line)
            if envelope and envelope["type"] == "frame":
                out.append(envelope)
        return out

    envelopes = asyncio.run(collect())
    stamps = [e["vehicle_ms"] for e in envelopes]

    assert len(stamps) == 227
    assert all(b > a for a, b in zip(stamps, stamps[1:]))
    assert [e["seq"] for e in envelopes] == list(range(1, 228))
