"""A model of the GEN4 flight unit's auto-eject trigger.

⚠ THIS IS A MODEL, NOT THE FIRMWARE. It re-implements `apogeeUpdate()` and
`apogeeTick()` from `firmware/MRC_FlightUnit_GEN4/Apogee.ino` in Python, because
`arduino-cli` is not on this machine and none of that C has ever been compiled here.
**It can drift from the code it models.** When the trigger changes, this changes with it.

`status.md` Next 6 has asked for exactly this for three sessions: *"Nothing pins the
apogee state machine. Sessions 5, 7 and now 8 each found real errors in it without a
committed test — the RESET re-arming behaviour, the eject latch interaction, and now the
NaN path. That is three times. The trace should be committed."* This is that trace.

Auto-eject has still never fired on hardware. Nothing here is evidence that it will.

Mirrors, and where they live:

    AUTO_EJECT_ARM_ALT_M   30.0   Config.h
    AUTO_EJECT_DROP_M      10.0   Config.h
    AUTO_EJECT_CONFIRM_N   3      Config.h   (SAMPLES since devlog 071, not cycles)
    AUTO_EJECT_SAMPLE_MS   125    Config.h
    ALT_ZERO_MIN_M/-MAX_M  -500 / 5000
"""

from __future__ import annotations

import math
import re
from pathlib import Path

import pytest

CONFIG_H = (
    Path(__file__).resolve().parents[2]
    / "firmware" / "MRC_FlightUnit_GEN4" / "Config.h"
)


def firmware_number(name: str) -> float:
    """Read a numeric `#define` straight out of the firmware header.

    Hardcoding these would defeat the point. `test_uplink_protocol.py` says it best —
    the firmware source is the authority, and a test carrying its own copy of a constant
    drifts with the same comfortable silence the mock did. CLAUDE.md's standing trap is
    that mirrored constants drift silently; `CHUTE_PIN`, the sync word, the GPS pins and
    `VEHICLE_DEFAULT_REPEAT` have each cost a session.
    """
    text = CONFIG_H.read_text(encoding="utf-8")
    match = re.search(rf"^#define\s+{name}\s+(-?[0-9.]+)f?\s*(?:/\*|$)", text, re.M)
    assert match is not None, f"{name} not found in {CONFIG_H}"
    return float(match.group(1))


ARM_ALT_M = firmware_number("AUTO_EJECT_ARM_ALT_M")
DROP_M = firmware_number("AUTO_EJECT_DROP_M")
CONFIRM_N = int(firmware_number("AUTO_EJECT_CONFIRM_N"))
SAMPLE_MS = int(firmware_number("AUTO_EJECT_SAMPLE_MS"))
ALT_ZERO_MIN_M = firmware_number("ALT_ZERO_MIN_M")
ALT_ZERO_MAX_M = firmware_number("ALT_ZERO_MAX_M")

NAN = float("nan")


def test_the_flown_defaults_are_what_this_file_models():
    """If these move, every expectation below is describing a vehicle nobody is flying."""
    assert ARM_ALT_M == 30.0
    assert DROP_M == 10.0
    assert CONFIRM_N == 3
    assert SAMPLE_MS == 125
    assert (ALT_ZERO_MIN_M, ALT_ZERO_MAX_M) == (-500.0, 5000.0)


class Trigger:
    """`apogeeUpdate()` and the state it owns."""

    def __init__(self, *, enabled=True, arm_alt_m=ARM_ALT_M,
                 drop_m=DROP_M, confirm_n=CONFIRM_N) -> None:
        self.enabled = enabled
        self.arm_alt_m = arm_alt_m
        self.drop_m = drop_m
        self.confirm_n = confirm_n

        self.apogee_alt = 0.0
        self.armed = False
        self.descent_samples = 0
        self.fired = False
        self.rejected = 0
        self.chute_is_fired = False     # the ground got there first

    def update(self, alt: float) -> bool:
        # devlog 071: reject the sample and HOLD state. Nothing below runs.
        if not math.isfinite(alt):
            self.rejected += 1
            return False

        if alt > self.apogee_alt:
            self.apogee_alt = alt

        if not self.armed and alt >= self.arm_alt_m:
            self.armed = True

        if not self.enabled or not self.armed or self.fired or self.chute_is_fired:
            return False

        drop = self.apogee_alt - alt

        if drop < self.drop_m:
            self.descent_samples = 0
            return False

        self.descent_samples += 1
        if self.descent_samples < self.confirm_n:
            return False

        self.fired = True
        return True


# ---------------------------------------------------------------------------
# devlog 065 fault 4 — the dangerous one
# ---------------------------------------------------------------------------

def test_an_armed_vehicle_reading_nan_does_not_deploy():
    """THE regression test. Before devlog 071 this fired.

    Every comparison against NaN is false, and the rule read those falses in opposite
    directions: `drop < dropM` false meant `descentCycles` was never reset, so it climbed
    to confirmN and deployed the vehicle wherever it happened to be.
    """
    t = Trigger()
    t.update(50.0)                      # climb, arm
    assert t.armed

    for _ in range(100):
        assert t.update(NAN) is False

    assert not t.fired, "fault 4 is back - a NaN altitude fired the trigger"
    assert t.rejected == 100


def test_a_nan_holds_state_rather_than_resetting_it():
    """"Reject the sample and hold" - a descent in progress is not derailed by a glitch."""
    t = Trigger()
    t.update(100.0)
    t.update(88.0)                      # drop 12 >= 10, sample 1
    assert t.descent_samples == 1

    t.update(NAN)                       # neither counted nor cleared
    assert t.descent_samples == 1

    t.update(86.0)                      # sample 2
    assert t.descent_samples == 2
    assert t.update(84.0) is True       # sample 3 fires


def test_a_nan_cannot_arm_the_trigger():
    """Why the pad was safe even before the guard."""
    t = Trigger()
    for _ in range(50):
        t.update(NAN)
    assert not t.armed
    assert not t.fired


def test_infinity_is_rejected_too():
    t = Trigger()
    t.update(50.0)
    for _ in range(10):
        t.update(float("inf"))
        t.update(float("-inf"))
    assert not t.fired
    assert t.rejected == 20


def test_a_permanently_failed_sensor_leaves_the_trigger_inert():
    """Holding is not recovering, and this records that plainly.

    A barometer that never comes back means the rule never advances again. It will not
    fire wrongly and it will not fire at all - the uplink is the backup for that case.
    """
    t = Trigger()
    t.update(200.0)
    t.update(150.0)                     # a real descent had begun
    assert t.descent_samples == 1

    for _ in range(1000):
        t.update(NAN)

    assert not t.fired
    assert t.descent_samples == 1, "state held, exactly as designed - and stuck"


# ---------------------------------------------------------------------------
# The rule itself
# ---------------------------------------------------------------------------

def test_it_fires_on_the_nth_qualifying_sample_not_after_n():
    t = Trigger()
    t.update(100.0)
    assert t.update(85.0) is False      # 1
    assert t.update(84.0) is False      # 2
    assert t.update(83.0) is True       # 3 - fires
    assert t.descent_samples == CONFIRM_N


def test_drop_is_cumulative_from_apogee_not_per_sample():
    """A big fall in one sample counts ONCE.

    `drop` is measured against a frozen apogee, so falling 20 m in one sample increments
    the counter by one, exactly as falling 10 m would. The counter counts samples, never
    multiples of dropM.
    """
    t = Trigger()
    t.update(100.0)
    t.update(80.0)                      # fell 20 m at once - drop 20
    assert t.descent_samples == 1, "a 2x drop must not count as two confirmations"


def test_once_the_threshold_is_passed_it_stays_passed():
    """Which is why confirmN delays rather than confirms, during a real descent.

    apogeeAlt is frozen and alt only falls, so `drop` grows monotonically. Recorded as a
    property because it is the reason the reset branch is effectively dead in a genuine
    fall - the filtering it buys is against a single glitch, not a sustained one.
    """
    t = Trigger()
    t.update(100.0)
    drops = []
    for alt in (85.0, 80.0, 75.0):
        drops.append(t.apogee_alt - alt)
        t.update(alt)
    assert drops == sorted(drops), "drop must be monotonic during a descent"


def test_a_non_qualifying_sample_resets_the_count():
    t = Trigger()
    t.update(100.0)
    t.update(85.0)
    assert t.descent_samples == 1
    t.update(95.0)                      # drop 5 < 10
    assert t.descent_samples == 0


def test_it_cannot_arm_below_the_floor_however_far_it_dips():
    """The interlock that stops it firing on the pad."""
    t = Trigger()
    for alt in (0.1, 0.4, 0.0, -0.3, 0.2, -0.5):
        assert t.update(alt) is False
    assert not t.armed


def test_it_is_one_shot():
    t = Trigger()
    t.update(100.0)
    t.update(85.0)
    t.update(84.0)
    assert t.update(83.0) is True
    for alt in (70.0, 60.0, 50.0):
        assert t.update(alt) is False, "the trigger fired twice"


def test_a_commanded_release_stops_the_trigger_claiming_it():
    t = Trigger()
    t.update(100.0)
    t.chute_is_fired = True             # the ground got there first
    for alt in (85.0, 84.0, 83.0, 82.0):
        assert t.update(alt) is False


# ---------------------------------------------------------------------------
# devlog 071 — what the faster sampling actually buys
# ---------------------------------------------------------------------------

def test_three_confirmations_now_cost_250ms_not_2000ms():
    """The point of decoupling the trigger from the telemetry cadence.

    confirmN is unchanged at 3 - the confidence is the same three independent
    conversions it always was. Only the clock changed.
    """
    assert (CONFIRM_N - 1) * SAMPLE_MS == 250
    assert (CONFIRM_N - 1) * 1000 == 2000, "what it used to cost, for the record"


def test_the_sample_interval_clears_the_barometer_conversion_time():
    """Sampling faster than the sensor converts would count one measurement twice.

    The BME280 at the Adafruit default (16x oversampling on all three, FILTER_OFF) takes
    ~98 ms typical and ~113 ms worst case, and in MODE_NORMAL the registers only update
    at that rate. AUTO_EJECT_SAMPLE_MS must stay above the worst case or two
    confirmations can be satisfied by a single physical measurement.
    """
    BME280_CONVERSION_MAX_MS = 113
    assert SAMPLE_MS > BME280_CONVERSION_MAX_MS


# ---------------------------------------------------------------------------
# devlog 065 fault 3, code half — the altitude baseline
# ---------------------------------------------------------------------------

def zero_altitude(readings: list[float]) -> tuple[bool, float]:
    """`sensorsCalibrate()`'s baseline capture, with the devlog 071 band."""
    for candidate in readings:
        if math.isfinite(candidate) and ALT_ZERO_MIN_M <= candidate <= ALT_ZERO_MAX_M:
            return True, candidate
    return False, 0.0


def test_the_observed_corrupt_baseline_is_rejected():
    """The value devlog 065 actually captured: about -1740 m, beside a sane pressure."""
    zeroed, _ = zero_altitude([-1739.1])
    assert not zeroed


def test_a_nan_baseline_is_rejected():
    zeroed, _ = zero_altitude([NAN, NAN])
    assert not zeroed


def test_it_retries_and_takes_the_first_plausible_reading():
    zeroed, base = zero_altitude([NAN, -1739.1, 41.3, 41.4])
    assert zeroed
    assert base == pytest.approx(41.3)


def test_a_failed_zeroing_leaves_the_trigger_unable_to_arm():
    """Failing closed: alt reports 0.0, which can never pass the 30 m arming floor.

    The vehicle still flies, still reports, and can still be fired from the ground.
    """
    zeroed, _ = zero_altitude([NAN, -1739.1, NAN, -1740.0, NAN])
    assert not zeroed

    t = Trigger()
    for _ in range(200):
        t.update(0.0)               # what sensorsAltitude() returns when not zeroed
    assert not t.armed
    assert not t.fired


def test_real_launch_sites_are_not_rejected():
    """The band is a sanity check, not a site survey."""
    for site in (-400.0, 0.0, 41.3, 1500.0, 4800.0):
        zeroed, base = zero_altitude([site])
        assert zeroed, f"{site} m is a plausible launch site and was rejected"
        assert base == pytest.approx(site)
