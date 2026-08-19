"""Link accounting tests — rules S1–S5.

Every case here is one the obvious implementation gets wrong, which is why the module is
separate and pure. No capture contains a reboot, a duplicate or a dropout, so none of
this could be tested against real data; it has to be constructed.

The through-line: a *confident wrong* loss figure is worse than no loss figure. 0% when
nothing is measurable, or 1.8 million lost the instant the vehicle reboots, are both
numbers an operator would act on.
"""

from __future__ import annotations

import pytest

from dashboard.linkstats import DEFAULT_WINDOW, LinkTracker


def feed(tracker: LinkTracker, seqs, crc_ok: bool | None = True) -> None:
    for seq in seqs:
        tracker.observe(seq=seq, crc_ok=crc_ok)


# ----------------------------------------------------------- S5 · not derivable


def test_no_counter_means_no_figure_not_zero():
    """S5. GEN1 and GEN2 carry no sequence number.

    None must reach the UI so it can say "unavailable". A 0% would be a fabricated
    measurement — the same failure as deriving loss from `rx_index`, which is exactly
    what `seq` exists to replace.
    """
    tracker = LinkTracker()
    for _ in range(5):
        assert tracker.observe(seq=None, crc_ok=None).kind == "unnumbered"
    assert tracker.snapshot() is None


def test_a_fresh_tracker_reports_nothing():
    assert LinkTracker().snapshot() is None


# ------------------------------------------------------------- S2 · the baseline


def test_the_baseline_is_the_first_seq_this_session_saw():
    """S2. Started mid-flight at seq=412, it must not open by reporting 411 lost.

    Nobody was listening for those packets. Counting them would put the dashboard's
    worst-looking number on screen at the moment it starts, every time.
    """
    tracker = LinkTracker()
    feed(tracker, [412, 413, 414])

    stats = tracker.snapshot()
    assert stats.baseline_seq == 412
    assert stats.expected == 3
    assert stats.received == 3
    assert stats.lost == 0
    assert stats.loss_pct == 0.0


def test_a_clean_run_reports_no_loss():
    tracker = LinkTracker()
    feed(tracker, range(1, 101))

    stats = tracker.snapshot()
    assert (stats.expected, stats.received, stats.lost) == (100, 100, 0)


def test_a_dropout_is_counted_as_loss():
    # 1,2,3 then 7: packets 4, 5 and 6 were sent and not received.
    tracker = LinkTracker()
    feed(tracker, [1, 2, 3, 7])

    stats = tracker.snapshot()
    assert stats.expected == 7
    assert stats.received == 4
    assert stats.lost == 3
    assert stats.loss_pct == pytest.approx(42.86, abs=0.01)


# ---------------------------------------------------------------- S1 · checksum


def test_a_failed_checksum_never_touches_sequence_arithmetic():
    """S1, and the rule with the nastiest failure mode.

    A corrupted frame's `seq` is a corrupted number. Feeding it in would invent a gap or
    a restart — and it would do so when the link is worst, which is exactly when the
    figure is being relied on.
    """
    tracker = LinkTracker()
    feed(tracker, [1, 2, 3])
    # A garbage sequence number arrives on a frame that failed its CRC.
    assert tracker.observe(seq=999_999, crc_ok=False).kind == "crc_failed"
    feed(tracker, [4])

    stats = tracker.snapshot()
    assert stats.last_seq == 4
    assert stats.expected == 4
    assert stats.lost == 0
    # Counted separately, because RF corruption is link quality, not an absence.
    assert stats.crc_failed == 1


def test_crc_failures_are_counted_before_any_packet_is_numbered():
    tracker = LinkTracker()
    tracker.observe(seq=None, crc_ok=False)
    # Still nothing to report: a corrupt frame establishes no baseline.
    assert tracker.snapshot() is None


# ----------------------------------------------------------------- S4 · restart


def test_a_backwards_seq_is_a_restart_not_catastrophic_loss():
    """S4. The vehicle rebooted and `seq` returned to 1.

    Naive arithmetic reports 1801 lost packets at the exact moment someone is trying to
    work out what just happened.
    """
    tracker = LinkTracker()
    feed(tracker, [1800, 1801, 1802])
    observation = tracker.observe(seq=1, crc_ok=True)

    assert observation.kind == "restart"
    assert observation.restart.previous_seq == 1802
    assert observation.restart.new_seq == 1

    stats = tracker.snapshot()
    assert stats.baseline_seq == 1
    assert stats.expected == 1
    assert stats.received == 1
    assert stats.lost == 0


def test_counting_resumes_cleanly_after_a_restart():
    tracker = LinkTracker()
    feed(tracker, [50, 51, 52, 1, 2, 3])

    stats = tracker.snapshot()
    assert (stats.expected, stats.received, stats.lost) == (3, 3, 0)
    assert stats.restarts == 1


def test_a_restart_does_not_erase_the_session_diagnostics():
    """Loss counters reset; evidence does not.

    Zeroing the corruption and reboot counts on a reboot would destroy the record at the
    precise moment it becomes interesting.
    """
    tracker = LinkTracker()
    feed(tracker, [10, 11])
    tracker.observe(seq=12, crc_ok=False)
    feed(tracker, [1, 2])

    stats = tracker.snapshot()
    assert stats.crc_failed == 1
    assert stats.restarts == 1


def test_repeated_restarts_are_each_counted():
    tracker = LinkTracker()
    feed(tracker, [5, 1, 5, 1])
    assert tracker.snapshot().restarts == 2


# --------------------------------------------------------------- S4 · duplicate


def test_a_duplicate_is_ignored_rather_than_counted_twice():
    """S4. Received twice would push `received` above `expected` and make loss negative."""
    tracker = LinkTracker()
    feed(tracker, [1, 2, 3])
    assert tracker.observe(seq=3, crc_ok=True).kind == "duplicate"

    stats = tracker.snapshot()
    assert stats.expected == 3
    assert stats.received == 3
    assert stats.lost == 0
    assert stats.duplicates == 1


def test_loss_can_never_go_negative():
    tracker = LinkTracker()
    feed(tracker, [1, 1, 1, 1, 1])
    assert tracker.snapshot().lost == 0


# ----------------------------------------------------------------- S3 · rolling


def test_rolling_and_session_answer_different_questions():
    """S3. A single cumulative figure hides a link that has just collapsed behind
    twenty good minutes — and the collapse is the actionable half."""
    tracker = LinkTracker(window=10)
    feed(tracker, range(1, 101))          # 100 clean packets
    feed(tracker, [120])                  # then 19 in a row lost

    stats = tracker.snapshot()
    # The session barely moves: 19 lost out of 120.
    assert stats.lost == 19
    assert stats.loss_pct == pytest.approx(15.83, abs=0.01)
    # The rolling window shows what just happened.
    assert stats.rolling.window == 10
    assert stats.rolling.expected == 10
    assert stats.rolling.received == 1
    assert stats.rolling.loss_pct == pytest.approx(90.0, abs=0.01)


def test_the_rolling_window_never_reaches_back_past_the_baseline():
    # Three packets in, the window cannot claim to have expected sixty.
    tracker = LinkTracker(window=60)
    feed(tracker, [1, 2, 3])

    rolling = tracker.snapshot().rolling
    assert rolling.expected == 3
    assert rolling.received == 3
    assert rolling.lost == 0


def test_the_rolling_window_recovers_when_the_link_does():
    """The point of a rolling figure: it must come back down.

    A window that stayed pessimistic after the link recovered would be as misleading as
    one that stayed optimistic while it failed.
    """
    tracker = LinkTracker(window=10)
    feed(tracker, [1, 20])                    # a large gap
    assert tracker.snapshot().rolling.loss_pct > 50

    feed(tracker, range(21, 41))               # twenty clean packets
    assert tracker.snapshot().rolling.loss_pct == 0.0


def test_the_default_window_is_sixty_packets():
    # One minute at 1 Hz, per S3.
    assert DEFAULT_WINDOW == 60
    assert LinkTracker().window == 60


def test_a_window_smaller_than_one_packet_is_rejected():
    with pytest.raises(ValueError):
        LinkTracker(window=0)


# ---------------------------------------------------------------- serialisation


def test_the_dict_carries_both_figures_and_the_diagnostics():
    tracker = LinkTracker(window=5)
    feed(tracker, [1, 2, 4])
    tracker.observe(seq=99, crc_ok=False)

    payload = tracker.snapshot().as_dict()

    assert payload["expected"] == 4
    assert payload["received"] == 3
    assert payload["lost"] == 1
    assert payload["crc_failed"] == 1
    assert payload["rolling"]["window"] == 5
    assert set(payload) == {
        "expected", "received", "lost", "loss_pct", "rolling",
        "crc_failed", "duplicates", "restarts", "baseline_seq", "last_seq",
    }
