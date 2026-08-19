"""The packet contract, as the JSON sidecar written beside every raw log.

A `.log` outlives the session that produced it, and usually the person who produced it.
Six weeks later someone opens one and finds 400 lines of comma-separated numbers with no
indication of what column 14 is, whether `0.00000` is a coordinate or a sentinel, or which
fields the checksum actually covers. Answering that from the codebase means finding the
right firmware revision, which is a worse problem than it sounds once the format has
changed twice.

So the answer travels with the file, in `<log>.meta.json`.

**Scope is deliberately narrow.** Current wire format only — no GEN1/GEN2 field lists, no
plausibility bounds, no hardware caveats. Those all belong somewhere that can be corrected;
this file is written once when the log opens and never revisited, so anything transient
in it rots quietly and is believed anyway. What stays is what is true of the bytes.

The field table is generated from `parser.FIELD_DOC`, not written here. A contract
maintained separately from the parser is wrong within two revisions.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from .parser import (
    FIELD_DOC,
    GEN3_EXTENDED_FIELDS,
    GEN3_LINK_FIELDS,
    GEN3_VEHICLE_FIELDS,
    _INT_FIELDS,
)

#: Bumped when the wire format changes, so a reader can tell which contract a given file
#: was written under without inferring it from the field count.
CONTRACT_VERSION = "GEN3.1"

#: Wire order. Vehicle fields carry indices 1..20; the ground station appends 21 and 22
#: after the checksum, so they are listed but flagged as outside it.
_ORDER = (*GEN3_VEHICLE_FIELDS, *GEN3_EXTENDED_FIELDS, *GEN3_LINK_FIELDS)


def _field_entry(index: int, name: str) -> dict:
    doc = FIELD_DOC[name]
    entry: dict = {
        "i": index,
        "name": name,
        "type": "int" if name in _INT_FIELDS else "float",
        "unit": doc["unit"],
        "fmt": doc["fmt"],
        "desc": doc["desc"],
    }
    if "sentinel" in doc:
        value, means = doc["sentinel"]
        entry["sentinel"] = {"value": value, "means": means}
    if "note" in doc:
        entry["note"] = doc["note"]
    return entry


def contract(source_name: str, started_at: datetime | None = None) -> dict:
    """The sidecar as a dict. Serialise with `dumps()` to get the compact layout."""
    started = started_at or datetime.now(timezone.utc)
    n_vehicle = len(GEN3_VEHICLE_FIELDS) + len(GEN3_EXTENDED_FIELDS)

    return {
        "contract": CONTRACT_VERSION,
        "source": source_name,
        "started_at": started.isoformat(),
        "file": {
            "note": (
                "Every line received, written before anything tried to interpret it. "
                "Malformed lines and foreign packets are kept on purpose."
            ),
            "line_kinds": {
                "$": "telemetry packet",
                "[": "ground station status message, never parsed",
            },
        },
        "packet": {
            "shape": f"$MRC,<{n_vehicle} fields>*<CRC16>[,rssi,snr]",
            "separator": ",",
            "vehicle_fields": n_vehicle,
            "checksum": {
                "algorithm": "CRC16/CCITT-FALSE",
                "poly": "0x1021",
                "init": "0xFFFF",
                "reflect": False,
                "xorout": "0x0000",
                "covers": "bytes between $ and *",
                "encoding": "4 uppercase hex digits following *",
                "on_failure": "frame rejected; the raw line is still logged",
            },
            "outside_checksum": list(GEN3_LINK_FIELDS),
        },
        "fields": [_field_entry(i, name) for i, name in enumerate(_ORDER, start=1)],
    }


def dumps(data: dict) -> str:
    """Pretty-print, but keep each field object on ONE line.

    `json.dumps(indent=2)` explodes 22 field objects into ~150 lines and buries the table
    it is meant to be. One line per field makes it read like the table it is, and stays
    valid JSON either way — this only changes whitespace.
    """
    fields = data.get("fields", [])
    placeholder = "@@FIELDS@@"
    shell = json.dumps({**data, "fields": placeholder}, indent=2, ensure_ascii=False)

    rendered = ",\n".join(
        "    " + json.dumps(f, ensure_ascii=False, separators=(", ", ": "))
        for f in fields
    )
    block = "[\n" + rendered + "\n  ]" if fields else "[]"
    return shell.replace(f'"{placeholder}"', block) + "\n"
