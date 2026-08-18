# Issue Tracker

Open questions and conflicts that need resolving. One line per issue in the index; full detail
below. Referenced from the wiki by ID (`ISS-01`).

**Status:** 🔴 Open · 🟡 Deferred · 🟢 Resolved
**Updating:** change the status in both the index and the detail entry, and fill in
**Resolution**. Resolved issues stay in the document — they are the record of *why* a thing is
the way it is. Actions taken go in the devlog; this file tracks state.

---

## Index

| ID | Issue | Status | Owner | Blocks |
|:--|:--|:--|:--|:--|
| [ISS-01](#iss-01) | Which packet generation is canonical | 🟢 Resolved | — | — |
| [ISS-02](#iss-02) | GEN2 ground unit firmware missing | 🟡 Deferred | Firmware member | Two-way testing |
| [ISS-03](#iss-03) | Heltec V3 vs V4 labelling | 🟢 Resolved | — | — |
| [ISS-04](#iss-04) | RA-01 cannot operate at 919 MHz | 🟡 Deferred | Firmware member | Ground station BOM |
| [ISS-05](#iss-05) | COM port assignments contradict | 🟡 Deferred | — | Ingest config |
| [ISS-06](#iss-06) | Competition requirements unknown | 🔴 Open | Aiman | Scope, mandatory displays |
| [ISS-07](#iss-07) | `CHUTE:` field is not numeric | 🔴 Open | Firmware member | Parser design |
| [ISS-08](#iss-08) | No packet counter, timestamp, or checksum | 🔴 Open | Firmware member | Loss detection, replay |
| [ISS-09](#iss-09) | Raw source files no longer present | 🔴 Open | Aiman | Test data availability |
| [ISS-10](#iss-10) | Ground unit output buffer smaller than payload | 🔴 Open | Firmware member | Packet integrity |

---

<a id="iss-01"></a>
## ISS-01 — Which packet generation is canonical

**Status** 🟢 Resolved · **Raised** 2026-08-18 · **Resolved** 2026-08-18

**Problem.** GEN1 emits 14 fields (16 at the PC); GEN2 emits 15 (17 at the PC). The v1 Python
bridge accepts only 16 and would reject every GEN2 packet.

**Resolution.** **GEN2 is the canonical packet format.** GEN1 is the one-way build; GEN2 is the
two-way build and its packet is GEN1's with the chute flag appended and precision raised. The
dashboard targets the 17-field GEN2 form. Documented in `source/firmware/packet-format.md`.

---

<a id="iss-02"></a>
## ISS-02 — GEN2 ground unit firmware missing

**Status** 🟡 Deferred · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** `MRC_TwoWay_Flowchart.html` describes a ground unit that polls serial every 10 ms,
matches `EJECT`, calls `fireEject()` and transmits the command 5× at 300 ms spacing. The only
ground unit firmware in the source set (`14.GROUND_919MHz.ino`) does none of this — it uses a
blocking `radio.receive()` and never reads from serial.

**Impact.** The uplink path cannot be exercised end to end. The dashboard's EJECT control has
nothing to talk to until this firmware exists.

**Needed to resolve.** Locate or write the GEN2 ground unit firmware; confirm the serial command
wire format (currently `EJECT\n`) and whether any acknowledgement is returned.

---

<a id="iss-03"></a>
## ISS-03 — Heltec V3 vs V4 labelling

**Status** 🟢 Resolved · **Raised** 2026-08-18 · **Resolved** 2026-08-18

**Problem.** Source files disagree on board revision — V3 in `Pins_Assignment.md` and
`13.CANSAT_919MHZ.ino`, V4 in `MRC_FlightUnit_V7.ino`.

**Resolution.** Naming error in the files. Not a hardware difference. LoRa pinout is identical
either way. No action.

---

<a id="iss-04"></a>
## ISS-04 — RA-01 cannot operate at 919 MHz

**Status** 🟡 Deferred · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** `Pins_Assignment.md` describes an Arduino Nano wired to an **RA-01** transceiver.
The RA-01 carries an SX1278, a 433 MHz part. The link runs at **919 MHz**. That band needs an
RA-01H / SX1276 variant. No firmware in the source set targets this combination.

**Needed to resolve.** Confirm whether the Nano + RA-01 section is obsolete, a spare/alternate
unit, or a planned change. If planned, the part number needs correcting.

---

<a id="iss-05"></a>
## ISS-05 — COM port assignments contradict

**Status** 🟡 Deferred · **Raised** 2026-08-18

**Problem.**

| Source | COM5 | COM12 |
|---|---|---|
| `serial_to_mqtt_V3.py` | — | "new ground unit port" |
| `MRC_DualDebug_Flow.json` | Ground Unit | Flight Unit |

**Impact.** Low — port numbers are machine- and USB-slot-specific and change on re-enumeration.
The real lesson is that the v2 ingest should **not hardcode a port**; it should enumerate and
either auto-detect or let the operator pick.

**Needed to resolve.** Confirm which unit is normally connected during a live run, and whether
both are ever connected at once outside bench testing.

---

<a id="iss-06"></a>
## ISS-06 — Competition requirements unknown

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Aiman

**Problem.** `wiki/source/competition/` is empty. Nothing in the source set names the
competition, its rules, mandatory telemetry, required displays, submission format, or dates.

**Impact.** Scope risk. A mandated field, display, or export format discovered late is a rework
of the packet contract and the UI.

**Needed to resolve.** Add the competition rulebook or the relevant extract to
`wiki/source/competition/`. Even the competition's name and date would narrow it.

---

<a id="iss-07"></a>
## ISS-07 — `CHUTE:` field is not numeric

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** GEN2 field 15 is the literal string `CHUTE:0` / `CHUTE:1`, not a bare digit. It is
the only non-numeric field in an otherwise uniform numeric CSV, so every parser needs a special
case, and `float()` on it raises.

**Decision needed.** Either:

- **(a)** Firmware drops the prefix, emitting `0` / `1` — uniform packet, one-line firmware
  change, but breaks anything already reading the `CHUTE:` form; or
- **(b)** Dashboard strips the prefix on ingest — no firmware change, permanent special case.

Worth settling alongside `ISS-08`, since both are packet-contract changes and are cheaper made
together than separately.

---

<a id="iss-08"></a>
## ISS-08 — No packet counter, timestamp, or checksum

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** Neither generation includes any of the three.

| Missing | Consequence |
|---|---|
| Packet counter | Packet loss is invisible. A dropped packet is indistinguishable from a dead transmitter — and with LoRa, loss is normal. |
| Onboard timestamp | Time is stamped on PC arrival, at 1 s resolution, local timezone. True sampling time is unrecoverable. |
| Checksum | LoRa's CRC covers the RF hop only. Nothing covers the USB serial hop or a truncated `Serial.println`. |

**Evidence.** Visible in the v1 data: `CANSAT_DATA` has 5,481 readings with no way to tell how
many packets never arrived, and 989 readings missing their `signal_data` row with no diagnostic.

**Impact.** Link-health display, gap detection, and any accurate time axis all depend on this.
A monotonic counter is the single highest-value addition.

**Needed to resolve.** Agree the additions with the firmware member. Cost is a few bytes per
packet at 1 Hz — airtime is not a constraint here.

---

<a id="iss-09"></a>
## ISS-09 — Raw source files no longer present

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Aiman

**Problem.** The raw files removed from `wiki/source/` root are not present anywhere under
`D:\MRCC`, and were never committed to git — the initial commit contained only `.claude/`,
`.gitattributes` and `README.md`. The extracted wiki documents are currently the only record of
their contents in this repo.

**Most significant loss: `CANSAT_DATA`** — 5,481 rows of real logged telemetry, the only genuine
flight data in the project. Its schema and statistics are preserved in
`source/previous-system/telemetry-database.md`, but **the rows themselves are not**. That
dataset is the natural test input for the v2 parser and lets the UI be built and validated with
no hardware on the desk.

**Needed to resolve.** Confirm the originals exist elsewhere (backup, another machine, the
firmware member). If `CANSAT_DATA` can be recovered, it is worth keeping — the `.ino` and
Node-RED files matter less now that they are documented.

---

<a id="iss-10"></a>
## ISS-10 — Ground unit output buffer smaller than flight payload

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** The GEN2 flight unit builds its packet in a **180-byte** buffer. The ground unit
re-emits it plus RSSI and SNR through a **160-byte** buffer:

```c
char buf[180];     // MRC_FlightUnit_V7.ino  — payload
char output[160];  // 14.GROUND_919MHz.ino   — payload + ",%.1f,%.2f"
```

`snprintf` truncates silently rather than overflowing, so a long packet would reach the PC
**short of its trailing fields** — losing SNR, RSSI, or the chute flag with no error raised.

**Impact.** Unlikely at typical field widths, but it fails silently and it fails at the end of
the packet, where the link-health and chute-state fields live. Worst case the dashboard shows a
stale chute state during descent.

**Needed to resolve.** Size the ground unit buffer at or above 180 + RSSI/SNR width, and confirm
worst-case packet length against the GEN2 format specifiers.
