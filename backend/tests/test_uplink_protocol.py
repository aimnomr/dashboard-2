"""Uplink protocol tests.

These exist because the dashboard shipped a broken eject command and nothing caught it.
The dashboard sent `EJECT`; the ground station's `handleCommand()` compares against
`CMD:EJECT`, so every eject would have been answered `[GCS] unknown command` — with the
vehicle on the pad and the operator watching a control that did nothing.

It survived because `MockSource` matched on the dashboard's spelling. The mock agreed
with the dashboard rather than with the firmware, so both halves of every test shared one
wrong assumption and confirmed each other.

The fix is not just the mapping. It is that **the firmware source is the authority**, and
these tests read it. A test that hardcoded the expected strings could drift with the same
comfortable silence as the mock did.
"""

from __future__ import annotations

import asyncio
import re
from pathlib import Path

import pytest

from dashboard.api import ALLOWED_COMMANDS, UPLINK_COMMANDS
from devtools.mock_source import CMD_EJECT, CMD_PING, MockSource

CONFIG_H = (
    Path(__file__).resolve().parents[2]
    / "firmware" / "MRC_GroundStation_GEN3" / "Config.h"
)


def firmware_command(name: str) -> str:
    """Read a `#define CMD_x "..."` straight out of the flashed firmware's header."""
    match = re.search(
        rf'^#define\s+{name}\s+"([^"]+)"', CONFIG_H.read_text(encoding="utf-8"), re.M
    )
    assert match is not None, f"{name} not found in {CONFIG_H}"
    return match.group(1)


# ------------------------------------------------------- the firmware is authority


@pytest.mark.parametrize("ui_name, define", [("EJECT", "CMD_EJECT"), ("PING", "CMD_PING")])
def test_the_wire_command_matches_the_firmware_header(ui_name, define):
    """The bytes the dashboard sends must be the bytes the ground station compares.

    Read from Config.h rather than hardcoded here, so that changing the firmware without
    changing the dashboard fails loudly instead of at the pad.
    """
    assert UPLINK_COMMANDS[ui_name] == firmware_command(define)


def test_the_mock_speaks_the_same_protocol_as_the_firmware():
    """The specific thing that was wrong.

    If the mock ever drifts back to accepting the dashboard's spelling, the whole class
    of bug returns: a green suite and a dead button.
    """
    assert CMD_EJECT == firmware_command("CMD_EJECT")
    assert CMD_PING == firmware_command("CMD_PING")


def test_the_dashboard_does_not_send_the_bare_verb():
    """Regression on the exact defect. `EJECT` is what the ground station rejects."""
    assert UPLINK_COMMANDS["EJECT"] != "EJECT"
    assert all(wire.startswith("CMD:") for wire in UPLINK_COMMANDS.values())


# -------------------------------------------------------------------- the allowlist


def test_the_uplink_is_an_allowlist_not_a_passthrough():
    """It fires a parachute. Arbitrary strings must not reach the radio."""
    assert ALLOWED_COMMANDS == {"EJECT", "PING"}
    assert "CMD:EJECT" not in ALLOWED_COMMANDS  # the wire form is not a UI name


def test_ping_is_reachable():
    """Without it, the only way to test the uplink is to deploy the parachute."""
    assert "PING" in ALLOWED_COMMANDS


# ------------------------------------------------------------------- the mock obeys


def send(source: MockSource, command: str) -> bool:
    return asyncio.run(source.send_command(command))


def test_mock_accepts_the_firmware_spelling():
    assert send(MockSource(), CMD_EJECT) is True
    assert send(MockSource(), CMD_PING) is True


@pytest.mark.parametrize("rejected", ["EJECT", "eject", "PING", "CMD:FIRE", ""])
def test_mock_rejects_what_the_ground_station_would_reject(rejected):
    """A lenient mock converts a loud failure into a silent one, and moves the discovery
    to launch day. `EJECT` here is the exact string that used to be accepted."""
    assert send(MockSource(), rejected) is False


def test_mock_eject_sets_the_chute_flag():
    source = MockSource()
    assert send(source, CMD_EJECT) is True
    assert source._chute_deployed is True


def test_mock_ping_fires_nothing():
    """A ping must not move the chute. That is the entire point of having one."""
    source = MockSource()
    assert send(source, CMD_PING) is True
    assert source._chute_deployed is False


def test_a_replay_reports_failure_for_every_command():
    """A capture has no uplink. Reporting success would teach the operator that the
    control works, in the one situation where it provably did nothing."""
    from dashboard.sources.file_source import FileSource

    from conftest import FIXTURES

    source = FileSource(FIXTURES / "FLIGHT22.CSV")
    assert asyncio.run(source.send_command(CMD_EJECT)) is False
    assert asyncio.run(source.send_command(CMD_PING)) is False
