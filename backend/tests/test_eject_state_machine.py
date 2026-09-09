"""A model of the GEN4 ground station's EJECT confirmation latch.

⚠ THIS IS A MODEL, NOT THE FIRMWARE. It re-implements the state machine in
`firmware/MRC_GroundStation_GEN4/` — `handleCommand()`, `fireEjectBurst()` and the
confirmation block in `radioPoll()` — in Python, because `arduino-cli` is not on this
machine and the C is neither compiled nor flashed here. **It can drift from the code it
models.** When the firmware's eject path changes, this file changes with it or it is
worse than nothing.

It is committed anyway, for a reason the devlog keeps recording. Devlog 058 diagnosed the
first appearance of the baseline bug with exactly this kind of model and did not commit
it; 063 and 065 then found the same bug twice more, each time on hardware. `status.md`
Next 6 makes the same complaint about the apogee machine — three sessions, three real
errors, no committed test. Every fault in this area has been found by a bench log, and a
bench log costs a flight unit, a ground unit and an afternoon.

What is modelled, and where it lives in the firmware:

    ejectConfirmed         MRC_GroundStation_GEN4.ino:56
    ejectAwaitingConfirm   MRC_GroundStation_GEN4.ino:84
    chuteBaseline          MRC_GroundStation_GEN4.ino:96
    assumedRepeat          MRC_GroundStation_GEN4.ino:78
    confirmation decision  Radio.ino, inside the CRC-valid branch of radioPoll()
    burst                  Uplink.ino, fireEjectBurst()
    timeout                Uplink.ino, uplinkPoll()

What is NOT modelled: the radio, timing other than a monotonic clock the test advances by
hand, `ul`, and the vehicle. `chute` is moved by the test, standing in for whatever the
vehicle did.
"""

from __future__ import annotations

import pytest

#: Mirrors EJECT_ATTEMPTS / EJECT_RETRY_MS in MRC_GroundStation_GEN4/Config.h.
EJECT_ATTEMPTS = 5
EJECT_RETRY_MS = 300
#: Mirrors EJECT_REARM_MS and EJECT_CONFIRM_TIMEOUT_MS in the same file.
EJECT_REARM_MS = 3000
EJECT_CONFIRM_TIMEOUT_MS = 5000


class GroundStation:
    """The ground station's eject latch, and nothing else."""

    def __init__(self, *, assumed_repeat: bool = True) -> None:
        self.now = 0
        self.eject_confirmed = False
        self.eject_confirmed_ms = 0
        self.eject_awaiting_confirm = False
        self.eject_awaiting_confirm_ms = 0
        self.chute_baseline = 0
        self.last_chute = -1          # -1 = nothing received yet
        self.assumed_repeat = assumed_repeat
        self.transmitted = 0          # EJECT tokens actually put on the air
        self.console: list[str] = []
        #: Packets scheduled to arrive at an absolute time, so a confirmation can land
        #: INSIDE a burst — fireEjectBurst() polls the radio between attempts, and that
        #: is the whole reason its early exit exists.
        self._inbound: list[tuple[int, int, dict]] = []

    def deliver_at(self, when_ms: int, chute: int, **kwargs) -> None:
        self._inbound.append((when_ms, chute, kwargs))

    # ---- Uplink.ino :: handleCommand(), CMD_EJECT branch --------------------

    def command_eject(self) -> None:
        if self.eject_confirmed:
            if not self.assumed_repeat:
                self.console.append("ignored - SINGLE")
                return
            if self.now - self.eject_confirmed_ms < EJECT_REARM_MS:
                self.console.append("ignored - still re-arming")
                return
            self.eject_confirmed = False
            self.chute_baseline = max(self.last_chute, 0)
            self.console.append(f"re-armed after cooldown, baseline {self.chute_baseline}")
        self._fire_eject_burst()

    # ---- Uplink.ino :: fireEjectBurst() ------------------------------------

    def _fire_eject_burst(self) -> None:
        for i in range(EJECT_ATTEMPTS):
            if self.eject_confirmed:
                self.console.append(f"burst stopped after {i} attempt(s) - already confirmed")
                return

            self.transmitted += 1
            self.eject_awaiting_confirm = True
            self.eject_awaiting_confirm_ms = self.now

            # The inter-attempt wait polls the radio. Time passes; packets that the
            # test has queued for this window arrive inside it.
            self.advance(EJECT_RETRY_MS)

        if self.eject_confirmed:
            self.console.append("confirmed during the final attempt")
            return
        self.console.append("burst complete - awaiting confirmation")

    # ---- Uplink.ino :: handleCommand(), CMD_RESET_CHUTE branch --------------

    def command_reset_chute(self, *, vehicle_confirms: bool = True) -> None:
        baseline_at_reset = self.last_chute
        if not vehicle_confirms:
            self.console.append("still latched here - the vehicle did not confirm")
            return
        self.eject_confirmed = False
        self.chute_baseline = max(baseline_at_reset, 0)
        if self.eject_awaiting_confirm:
            self.eject_awaiting_confirm = False
            self.console.append("pending confirmation discarded by RESET:CHUTE")
        self.console.append(f"re-armed at ground, baseline {self.chute_baseline}")

    # ---- Radio.ino :: radioPoll() ------------------------------------------

    def receive_packet(self, chute: int, *, crc_ok: bool = True, ul_fell: bool = False) -> None:
        if not crc_ok:
            # lastChute is deliberately left alone on a bad checksum.
            return

        if ul_fell:
            self.assumed_repeat = True  # VEHICLE_DEFAULT_REPEAT
            if self.eject_awaiting_confirm or self.chute_baseline != 0:
                self.eject_awaiting_confirm = False
                self.chute_baseline = 0
                self.console.append("vehicle restarted - baseline and pending cleared")

        self.last_chute = chute

        if self.eject_awaiting_confirm and self.last_chute > self.chute_baseline:
            self.eject_awaiting_confirm = False
            self.eject_confirmed = True
            self.eject_confirmed_ms = self.now
            self.console.append(f"EJECT confirmed, chute {self.chute_baseline} -> {chute}")

    # ---- Uplink.ino :: uplinkPoll(), the ageing tick -----------------------

    def advance(self, ms: int) -> None:
        target = self.now + ms

        # radioPoll() runs throughout the wait, so anything due in this span arrives
        # in order before the clock reaches the far end.
        while True:
            due = sorted(p for p in self._inbound if p[0] <= target)
            if not due:
                break
            when, chute, kwargs = due[0]
            self._inbound.remove(due[0])
            self.now = max(self.now, when)
            self.receive_packet(chute, **kwargs)

        self.now = target
        if (self.eject_awaiting_confirm
                and self.now - self.eject_awaiting_confirm_ms > EJECT_CONFIRM_TIMEOUT_MS):
            self.eject_awaiting_confirm = False
            self.console.append("confirmation timed out")


# ---------------------------------------------------------------------------
# The bug this whole change exists for — devlog 065 fault 1, third appearance of
# the absolute-test bug after 058 and 063.
# ---------------------------------------------------------------------------

def test_a_confirmation_arriving_after_the_burst_does_not_swallow_the_next_eject():
    """The exact sequence from `logs/raw/20260907-195122-serial.log`.

    A burst runs out of attempts. The release DID happen, but its packet lands after
    the burst gave up. Before devlog 070 that left `chuteBaseline` stale with
    `ejectConfirmed` false, and the next EJECT reported "confirmed after 0 attempt(s)"
    and transmitted nothing at all.
    """
    gs = GroundStation()
    gs.receive_packet(chute=0)

    gs.command_eject()
    assert gs.transmitted == EJECT_ATTEMPTS
    assert gs.eject_awaiting_confirm, "the burst is over, the wait is not"
    assert not gs.eject_confirmed

    # The confirming packet arrives a beat late — the case that used to poison.
    gs.advance(1000)
    gs.receive_packet(chute=1)
    assert gs.eject_confirmed
    assert not gs.eject_awaiting_confirm

    # ...and the NEXT eject, past the cooldown, actually transmits.
    sent_before = gs.transmitted
    gs.advance(EJECT_REARM_MS)
    gs.command_eject()
    assert gs.transmitted > sent_before, "the next EJECT was swallowed - fault 1 is back"


def test_a_stale_chute_rise_cannot_confirm_a_burst_that_was_never_sent():
    """`lastChute > chuteBaseline` is no longer sufficient on its own.

    This is the property that actually kills the bug: confirmation now requires a burst
    to be outstanding, so a counter left high by an earlier release cannot confirm
    anything by itself.
    """
    gs = GroundStation()
    gs.receive_packet(chute=7)          # a vehicle that released long ago
    assert gs.last_chute > gs.chute_baseline
    assert not gs.eject_confirmed, "an unrequested rise must not read as a confirmation"


def test_the_burst_stops_early_when_the_vehicle_confirms_mid_burst():
    """The early exit's remaining job: stop wasting airtime, not record state.

    A confirmation landing in the second inter-attempt wait should end the burst well
    short of EJECT_ATTEMPTS.
    """
    gs = GroundStation()
    gs.receive_packet(chute=0)
    gs.deliver_at(EJECT_RETRY_MS + 50, chute=1)     # inside the second wait

    gs.command_eject()

    assert gs.eject_confirmed
    assert gs.transmitted < EJECT_ATTEMPTS, "the burst kept transmitting after confirming"
    assert any("burst stopped after" in line for line in gs.console)


def test_a_confirmation_in_the_final_wait_is_still_caught():
    """The post-loop re-check, copied from fireConfigBurst().

    The loop's own test last ran before the final attempt, so without a check after the
    loop a confirmation arriving in that last wait would be reported as an unconfirmed
    burst — technically harmless now, but it would tell the operator the wrong thing.
    """
    gs = GroundStation()
    gs.receive_packet(chute=0)
    gs.deliver_at(EJECT_RETRY_MS * EJECT_ATTEMPTS - 20, chute=1)

    gs.command_eject()

    assert gs.eject_confirmed
    assert gs.transmitted == EJECT_ATTEMPTS
    assert "confirmed during the final attempt" in gs.console[-1]


# ---------------------------------------------------------------------------
# The three rules the stress test added
# ---------------------------------------------------------------------------

def test_a_vehicle_reboot_clears_the_pending_wait_and_the_baseline():
    gs = GroundStation()
    gs.receive_packet(chute=4)
    gs.chute_baseline = 4
    gs.command_eject()
    assert gs.eject_awaiting_confirm

    gs.receive_packet(chute=0, ul_fell=True)
    assert not gs.eject_awaiting_confirm
    assert gs.chute_baseline == 0, "a stale baseline outlives the counter it described"
    assert not gs.eject_confirmed


def test_reset_chute_discards_a_pending_confirmation():
    gs = GroundStation()
    gs.receive_packet(chute=1)
    gs.command_eject()
    assert gs.eject_awaiting_confirm

    gs.command_reset_chute()
    assert not gs.eject_awaiting_confirm

    # A delayed packet arriving now must NOT print "confirmed" moments after the
    # operator was told the vehicle is freshly re-armed.
    gs.receive_packet(chute=2)
    assert not gs.eject_confirmed


def test_a_confirmation_that_never_arrives_ages_out():
    """SINGLE mode: the vehicle's latch never expires, so `chute` never rises."""
    gs = GroundStation(assumed_repeat=False)
    gs.receive_packet(chute=1)
    gs.command_eject()
    assert gs.eject_awaiting_confirm

    gs.advance(EJECT_CONFIRM_TIMEOUT_MS + 1)
    assert not gs.eject_awaiting_confirm
    assert not gs.eject_confirmed, "timing out is the NOT-confirmed answer"

    # And it blocks nothing: the next EJECT is an ordinary first attempt.
    sent_before = gs.transmitted
    gs.command_eject()
    assert gs.transmitted > sent_before


def test_a_stuck_flag_would_have_misattributed_a_later_rise():
    """Why the timeout exists at all, stated as a test rather than a comment."""
    gs = GroundStation()
    gs.receive_packet(chute=1)
    gs.command_eject()
    gs.advance(EJECT_CONFIRM_TIMEOUT_MS + 1)
    assert not gs.eject_awaiting_confirm

    # An unrelated release, much later. Nothing is waiting on it, so nothing claims it.
    gs.receive_packet(chute=2)
    assert not gs.eject_confirmed


# ---------------------------------------------------------------------------
# Properties that must not regress
# ---------------------------------------------------------------------------

def test_a_bad_crc_packet_cannot_confirm_anything():
    gs = GroundStation()
    gs.receive_packet(chute=0)
    gs.command_eject()
    gs.receive_packet(chute=1, crc_ok=False)
    assert not gs.eject_confirmed
    assert gs.eject_awaiting_confirm, "the wait survives a packet we could not read"


def test_single_mode_refuses_a_second_eject_rather_than_reporting_one():
    gs = GroundStation(assumed_repeat=False)
    gs.receive_packet(chute=0)
    gs.command_eject()
    gs.advance(1000)
    gs.receive_packet(chute=1)
    assert gs.eject_confirmed

    sent_before = gs.transmitted
    gs.advance(EJECT_REARM_MS * 10)     # no amount of waiting helps in SINGLE
    gs.command_eject()
    assert gs.transmitted == sent_before
    assert "ignored - SINGLE" in gs.console[-1]


@pytest.mark.xfail(
    reason="`chute` cannot say WHY it rose. An auto-eject release moves the same "
           "counter an operator EJECT does, and GEN3.1 carries no field that would "
           "separate them. The pre-070 in-burst test had the identical ambiguity. "
           "Recorded as a known limitation, not a regression - resolving it needs a "
           "packet-format change, declined twice on other grounds.",
    strict=True,
)
def test_an_auto_eject_release_is_not_mistaken_for_a_commanded_one():
    gs = GroundStation()
    gs.receive_packet(chute=0)
    gs.command_eject()                  # heard by nobody; the vehicle is out of range
    gs.receive_packet(chute=1)          # ...but it auto-ejected on its own
    assert not gs.eject_confirmed
