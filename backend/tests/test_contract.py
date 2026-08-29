"""The `.meta.json` packet contract written beside every raw log.

A `.log` outlives its session and usually the person who made it. These tests are mostly
about the contract staying TRUE — one that drifts from the parser is worse than none,
because it is believed.

They also pin the SCOPE, which was a deliberate decision rather than an oversight: current
wire format only. See `test_scope_is_the_current_wire_format_only`.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

import pytest

from dashboard.contract import CONTRACT_VERSION, contract, dumps
from dashboard.parser import (
    FIELD_DOC,
    GEN3_EXTENDED_FIELDS,
    GEN3_LINK_FIELDS,
    GEN3_VEHICLE_FIELDS,
    crc16_ccitt,
    parse_line,
)

ALL_FIELDS = (*GEN3_VEHICLE_FIELDS, *GEN3_EXTENDED_FIELDS, *GEN3_LINK_FIELDS)


@pytest.fixture(scope="module")
def meta() -> dict:
    return contract("serial", datetime(2026, 8, 20, 12, 0, tzinfo=timezone.utc))


# ------------------------------------------------------------------ drift guards


def test_field_doc_covers_every_field():
    """Adding a field to the wire without documenting it fails here.

    The reason the contract is generated rather than written: documentation maintained
    separately from the parser is wrong within two revisions.
    """
    missing = [f for f in ALL_FIELDS if f not in FIELD_DOC]
    assert not missing, f"undocumented fields: {missing}"


def test_field_doc_has_no_fields_that_do_not_exist():
    """The other direction: a field removed from the wire must leave the docs too."""
    extra = set(FIELD_DOC) - set(ALL_FIELDS)
    assert not extra, f"documented but not on the wire: {extra}"


# ------------------------------------------------------------------ shape


def test_it_is_valid_json_and_round_trips(meta: dict):
    assert json.loads(dumps(meta)) == meta


def test_fields_are_in_wire_order_with_contiguous_indices(meta: dict):
    for offset, entry in enumerate(meta["fields"]):
        assert entry["i"] == offset + 1
        assert entry["name"] == ALL_FIELDS[offset]


def test_a_reader_can_zip_the_contract_against_a_real_packet(meta: dict):
    """The access pattern the array ordering was chosen for.

    This is the whole promise of the sidecar: split a line, zip it against `fields`, and
    every value has a name, a unit and a meaning without opening the codebase.
    """
    body = ("MRC,7,7000,29.73,75.1,1013.35,-0.2,0.002,-0.001,0.911,"
            "-0.10,0.21,-0.01,2.92717,101.76009,1.6,9,0,4,1.2,1")
    packet = f"${body}*{crc16_ccitt(body.encode()):04X},-33.0,12.5"

    values = packet.split("*")[0].split(",")[1:] + packet.split("*")[1].split(",")[1:]
    named = {f["name"]: v for f, v in zip(meta["fields"], values)}

    assert len(values) == len(meta["fields"])
    assert named["lat"] == "2.92717"
    assert named["hdop"] == "1.2"
    assert named["chute"] == "0"
    assert named["snr"] == "12.5"


def test_sentinels_are_attached_to_the_fields_that_have_them(meta: dict):
    by_name = {f["name"]: f for f in meta["fields"]}
    assert by_name["lat"]["sentinel"] == {"value": 0.0, "means": "no fix"}
    assert by_name["fixq"]["sentinel"]["value"] == -1
    assert by_name["hdop"]["sentinel"]["value"] == 0.0
    # A field with nothing to disclaim carries no sentinel key at all.
    assert "sentinel" not in by_name["temp"]


def test_link_fields_are_marked_as_outside_the_checksum(meta: dict):
    assert meta["packet"]["outside_checksum"] == list(GEN3_LINK_FIELDS)
    by_name = {f["name"]: f for f in meta["fields"]}
    for name in GEN3_LINK_FIELDS:
        assert "after the checksum" in by_name[name]["note"]


def test_checksum_is_described_precisely_enough_to_reimplement(meta: dict):
    crc = meta["packet"]["checksum"]
    assert crc["poly"] == "0x1021"
    assert crc["init"] == "0xFFFF"
    assert crc["reflect"] is False
    assert crc["xorout"] == "0x0000"
    assert "between $ and *" in crc["covers"]


def test_contract_version_is_present(meta: dict):
    assert meta["contract"] == CONTRACT_VERSION


# ------------------------------------------------------------------ scope


def test_scope_is_the_current_wire_format_only(meta: dict):
    """Pins four deliberate exclusions, so none is re-added by reflex.

    The sidecar is written once at open and never revisited. Anything transient in it is
    frozen at that moment and quietly wrong forever after — which is worse than absent,
    because a file that looks authoritative gets believed.
    """
    blob = dumps(meta)
    assert "GEN1" not in blob and "GEN2" not in blob   # legacy field lists
    assert "range" not in blob                          # plausibility bounds: UI policy
    assert "caveat" not in blob                         # hardware faults: they get fixed
    assert "observed" not in blob                       # close-time stats: never rewritten


def test_no_field_note_describes_a_transient_hardware_fault(meta: dict):
    """`note` explains the encoding, never the state of one airframe.

    "az reads ~0.92 g on the current unit" was in an earlier draft. It belongs in the
    devlog, where it can be corrected when the sensor is.
    """
    for entry in meta["fields"]:
        note = entry.get("note", "").lower()
        for banned in ("current unit", "spike", "unresolved", "suspected", "faulty"):
            assert banned not in note, f"{entry['name']}: transient claim {note!r}"


def test_the_chute_note_never_claims_deployment(meta: dict):
    """S8. The one statement this project must not make, on any surface."""
    note = next(f for f in meta["fields"] if f["name"] == "chute")["note"].lower()
    assert "commanded" in note
    assert "never confirms deployment" in note


# ------------------------------------------------------------------ on disk


def test_the_log_itself_stays_byte_faithful(tmp_path):
    """No header, no comments, nothing but what arrived.

    A header block was tried on 2026-08-20 and reverted the same day: the log's entire
    value is being a faithful byte record, and prose is the wrong shape for the job.
    """
    from dashboard.rawlog import RawLog

    packet = "$MRC,1,1000,20,50,1013,0,0,0,1,0,0,0,0,0,0,0,0,0,0.0,-1*0000"
    log = RawLog.create(tmp_path, "serial")
    log.write(packet)
    log.close()

    assert log.path.read_text(encoding="utf-8") == packet + "\n"


def test_the_sidecar_lands_next_to_the_log_and_parses(tmp_path):
    from dashboard.rawlog import RawLog

    log = RawLog.create(tmp_path, "mock")
    log.close()

    sidecar = log.path.with_suffix(".meta.json")
    assert sidecar.exists()

    data = json.loads(sidecar.read_text(encoding="utf-8"))
    assert data["source"] == "mock"
    assert data["contract"] == CONTRACT_VERSION
    assert len(data["fields"]) == len(ALL_FIELDS)


def test_the_sidecar_is_not_rewritten_when_the_log_closes(tmp_path):
    """Written once at open. No close-time rewrite means no half-written state."""
    from dashboard.rawlog import RawLog

    log = RawLog.create(tmp_path, "serial")
    sidecar = log.path.with_suffix(".meta.json")
    before = sidecar.read_bytes()

    log.write("$MRC,1,1000,20,50,1013,0,0,0,1,0,0,0,0,0,0,0,0,0,0.0,-1*0000")
    log.close()

    assert sidecar.read_bytes() == before


def test_status_lines_never_consume_replay_cadence():
    """Independent of the sidecar, and worth keeping from the reverted header work.

    A `[GCS]` line arrives BETWEEN packets on a real link, never instead of one, so it
    must not occupy a slot in the cadence. The clock-paced branch already knew this; the
    fixed-interval branch did not, and paced every line equally.
    """
    from dashboard.sources.file_source import FileSource

    src = FileSource.__new__(FileSource)
    src.speed = 1.0

    for interval in (None, 1.0):
        src.interval = interval
        delay, _ = src._delay_before("[GCS] EJECT armed", previous_ms=5000)
        assert delay == 0.0, f"status line delayed {delay}s at interval={interval}"

    src.interval = 1.0
    body = "MRC,2,6000,20,50,1013,0,0,0,1,0,0,0,0,0,0,0,0,0,0.0,-1"
    packet = f"${body}*{crc16_ccitt(body.encode()):04X}"
    delay, _ = src._delay_before(packet, previous_ms=5000)
    assert delay == 1.0


# --------------------------------------------------- the same table, over the wire

# The Channels packet readout formats and labels the live packet from the field table
# delivered in the session message. It is the same `field_table()` the sidecar is built
# from, and these pin that it stays the same one — a frontend holding its own copy is
# the drift the sidecar exists to prevent, moved one layer out.


def test_field_table_is_exactly_what_the_sidecar_carries(meta: dict):
    from dashboard.contract import field_table

    assert field_table() == meta["fields"]


def test_the_session_message_carries_the_field_table():
    from types import SimpleNamespace

    from dashboard.api import _session_message

    message = _session_message(
        SimpleNamespace(name="serial", simulated=False),
        SimpleNamespace(rx_index=7),
    )

    assert message["contract"] == CONTRACT_VERSION
    assert [f["name"] for f in message["fields"]] == list(ALL_FIELDS)
    # 1-based and contiguous, so a reader can count across a raw line to find field 14.
    assert [f["i"] for f in message["fields"]] == list(range(1, len(ALL_FIELDS) + 1))


def test_every_field_the_readout_renders_has_a_precision_and_a_label():
    """The readout formats from `fmt` and labels from `unit`/`desc`.

    A field arriving without `fmt` would be rendered at whatever precision JavaScript
    felt like, which for a latitude is the difference between 1 m and 100 km.
    """
    from dashboard.contract import field_table

    for entry in field_table():
        assert entry["fmt"], f"{entry['name']} has no format"
        assert entry["desc"], f"{entry['name']} has no description"
        assert entry["type"] in ("int", "float")


def test_the_link_fields_are_flagged_as_outside_the_checksum():
    from types import SimpleNamespace

    from dashboard.api import _session_message

    message = _session_message(
        SimpleNamespace(name="mock", simulated=True),
        SimpleNamespace(rx_index=0),
    )
    assert message["outside_checksum"] == list(GEN3_LINK_FIELDS)
