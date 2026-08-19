"""Packet loss accounting.

Implements rules S1–S5 of wiki/decisions/dashboard-gen3-plan.md. Pure: no I/O, no clock,
no sockets. Every awkward case — a vehicle reboot, a duplicate, a dashboard started
mid-flight — is reachable from a unit test without hardware, which is the whole reason it
is a separate module rather than a few lines inside the pipeline.

**This lives in the backend, and that is a decision, not an accident.** A browser may
connect late or reconnect mid-flight, and its history buffer is capped. A loss figure
computed client-side would be wrong for everyone who did not watch from the first packet,
and two operators looking at the same flight would read different numbers off the same
screen. The backend has seen the whole session, so it owns the counters.

The failure this module exists to prevent is not "no loss figure". It is a *confident
wrong* loss figure — 0% when nothing can be measured, or 1.8 million lost packets the
instant the vehicle reboots. Both are worse than showing nothing.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from typing import Literal

#: Packets in the rolling window (S3). At 1 Hz this is the last minute.
DEFAULT_WINDOW = 60

ObservationKind = Literal["counted", "duplicate", "restart", "crc_failed", "unnumbered"]


@dataclass(frozen=True, slots=True)
class Restart:
    """A vehicle reboot, detected as the sequence number going backwards."""

    previous_seq: int
    new_seq: int


@dataclass(frozen=True, slots=True)
class Observation:
    """What the tracker made of one packet."""

    kind: ObservationKind
    #: Set only when `kind == "restart"`, so the caller can announce it.
    restart: Restart | None = None


@dataclass(frozen=True, slots=True)
class Window:
    """Loss over a bounded span of recent packets."""

    window: int
    expected: int
    received: int
    lost: int
    loss_pct: float

    def as_dict(self) -> dict:
        return {
            "window": self.window,
            "expected": self.expected,
            "received": self.received,
            "lost": self.lost,
            "loss_pct": self.loss_pct,
        }


@dataclass(frozen=True, slots=True)
class LinkStats:
    """The full picture, as shipped in the frame envelope."""

    expected: int
    received: int
    lost: int
    loss_pct: float
    rolling: Window
    crc_failed: int
    duplicates: int
    restarts: int
    baseline_seq: int
    last_seq: int

    def as_dict(self) -> dict:
        # Flat fields are the session figures, matching the envelope shape in
        # dashboard-gen3-plan.md. `rolling` is additive, per S3.
        return {
            "expected": self.expected,
            "received": self.received,
            "lost": self.lost,
            "loss_pct": self.loss_pct,
            "rolling": self.rolling.as_dict(),
            "crc_failed": self.crc_failed,
            "duplicates": self.duplicates,
            "restarts": self.restarts,
            "baseline_seq": self.baseline_seq,
            "last_seq": self.last_seq,
        }


def _pct(lost: int, expected: int) -> float:
    if expected <= 0:
        return 0.0
    return round(lost / expected * 100, 2)


class LinkTracker:
    """Sequence bookkeeping for one backend session.

    One tracker per session. It is never reset from outside — a restart is something it
    detects, not something it is told.
    """

    def __init__(self, window: int = DEFAULT_WINDOW) -> None:
        if window < 1:
            raise ValueError("window must be at least 1 packet")

        self.window = window

        self._baseline: int | None = None
        self._last: int | None = None
        self._received = 0
        #: Sequence numbers inside the rolling window, oldest first.
        self._recent: deque[int] = deque()

        # Cumulative session diagnostics. Deliberately NOT reset by a restart: they
        # record what this session has seen, and zeroing them on a reboot would erase
        # the evidence at the exact moment someone is working out what went wrong.
        self._crc_failed = 0
        self._duplicates = 0
        self._restarts = 0

    # ------------------------------------------------------------------ observing

    def observe(self, *, seq: int | None, crc_ok: bool | None) -> Observation:
        """Account for one packet. Returns what was made of it.

        S1 — a frame that failed its checksum is counted as `crc_failed` and takes no
        part in sequence arithmetic. Its `seq` field is a corrupted number: trusting it
        would invent a gap, or a restart, precisely when the link is worst and the
        figures are being relied on most.
        """
        if crc_ok is False:
            self._crc_failed += 1
            return Observation(kind="crc_failed")

        # S5 — no counter, so nothing can be accounted. GEN1 and GEN2 land here, and
        # `snapshot()` keeps returning None so the UI shows "unavailable" rather than 0%.
        if seq is None:
            return Observation(kind="unnumbered")

        # S2 — the baseline is the first seq THIS session saw. Starting the dashboard
        # mid-flight at seq=412 must not open by reporting 411 packets lost; nobody was
        # listening for them.
        if self._baseline is None or self._last is None:
            self._start_at(seq)
            return Observation(kind="counted")

        # S4 — backwards means the vehicle reset, and nothing else does. Naive gap
        # arithmetic here would report catastrophic loss at the exact moment someone is
        # trying to understand what just happened.
        if seq < self._last:
            previous = self._last
            self._restarts += 1
            self._start_at(seq)
            return Observation(
                kind="restart", restart=Restart(previous_seq=previous, new_seq=seq)
            )

        # S4 — the same packet twice. Counting it would inflate `received` above
        # `expected` and produce a negative loss figure.
        if seq == self._last:
            self._duplicates += 1
            return Observation(kind="duplicate")

        # Forward. Any skip is a real dropout: those packets were sent and not received.
        self._last = seq
        self._received += 1
        self._recent.append(seq)
        self._trim()
        return Observation(kind="counted")

    def _start_at(self, seq: int) -> None:
        """Begin accounting from `seq`, discarding any previous baseline."""
        self._baseline = seq
        self._last = seq
        self._received = 1
        self._recent.clear()
        self._recent.append(seq)

    def _trim(self) -> None:
        low = self._window_low()
        while self._recent and self._recent[0] < low:
            self._recent.popleft()

    def _window_low(self) -> int:
        assert self._baseline is not None and self._last is not None
        # Never reaches back past the baseline: the window cannot expect packets from
        # before this session started listening.
        return max(self._baseline, self._last - self.window + 1)

    # ------------------------------------------------------------------- reporting

    def snapshot(self) -> LinkStats | None:
        """Current figures, or None when loss is not derivable at all (S5).

        None is not zero. It means the question cannot be asked of this data, and the UI
        must say "unavailable" rather than display a reassuring 0%.
        """
        if self._baseline is None or self._last is None:
            return None

        expected = self._last - self._baseline + 1
        lost = expected - self._received

        low = self._window_low()
        rolling_expected = self._last - low + 1
        rolling_received = len(self._recent)
        rolling_lost = rolling_expected - rolling_received

        return LinkStats(
            expected=expected,
            received=self._received,
            lost=lost,
            loss_pct=_pct(lost, expected),
            rolling=Window(
                window=self.window,
                expected=rolling_expected,
                received=rolling_received,
                lost=rolling_lost,
                loss_pct=_pct(rolling_lost, rolling_expected),
            ),
            crc_failed=self._crc_failed,
            duplicates=self._duplicates,
            restarts=self._restarts,
            baseline_seq=self._baseline,
            last_seq=self._last,
        )
