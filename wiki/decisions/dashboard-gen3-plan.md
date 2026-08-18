# Dashboard GEN3 Support — Plan

Proposed 2026-08-19. **Not yet agreed.**

The dashboard currently parses GEN1 (16 fields) and GEN2 (17). GEN3 is a different
*shape*, not another field count — start marker, checksum, vehicle clock, packet counter.

---

## What GEN3 unlocks

This is the point of the work. Three things the dashboard currently has to **refuse to
show**, because the data could not support them:

| Capability | Blocked by | Unblocked by |
|---|---|---|
| **Real packet loss** | no counter — `rx_index` counts arrivals, not sends | `seq` |
| **True time axis** | `pc_time` is arrival, not sampling | `ms` |
| **Rejecting corruption** | range checks and hope | `CRC16` |

Everything else is bookkeeping.

## The design questions worth settling first

### 1. Loss accounting belongs in the backend

The browser may connect late, or reconnect mid-flight, and its history buffer is capped.
A client-side loss figure would be wrong for anyone who did not watch from the first
packet.

The backend has seen the whole session. It owns the counters and ships them in the
envelope, so every client — first or fifth, early or late — sees the same truth.

### 2. Two clocks, used for different things

They are not interchangeable and mixing them up would be subtle and wrong:

| Clock | Source | Used for |
|---|---|---|
| **Vehicle `ms`** | onboard `millis()` | chart x-axis, descent rate, phase timing |
| **PC arrival** | `datetime.now()` | time-since-last-packet, staleness, link health |

The vehicle clock cannot measure silence — when the link drops there are no packets, so
its clock stops advancing from the dashboard's point of view. Staleness must stay on the
PC clock. Everything derived *from* the data should move to the vehicle clock.

### 3. A `seq` that goes backwards means a reboot, not 1.8 million lost packets

If the vehicle resets, `seq` returns to 1. Naive gap arithmetic would report a catastrophic
loss figure at exactly the moment someone is trying to work out what happened.

Detect `seq < previous_seq` as a **session restart**: reset the counters, emit a marker,
and tell the UI so it can break the chart line rather than draw a horizontal streak back
to the origin.

### 4. `chute` is a count now, and it does not mean deployed

GEN3 merged `ack` into `chute`: `0` = armed, `N ≥ 1` = commanded, and `N` is how many
eject commands reached the vehicle.

**The UI must relabel "Deployed" to "Commanded".** The chute is servo-driven with no
feedback sensor, so nothing on the vehicle can confirm the canopy opened. The current
label asserts something no sensor supports, at exactly the moment someone is deciding
whether to trust it.

### 5. CRC failure is link-quality information, not just an error

A packet that parses but fails its checksum is a *different* event from a malformed line:
it means RF corruption, which is arguably a better link-quality signal than RSSI. Count it
separately, show it separately.

## Concrete solutions

Eight rules. Each exists because the obvious implementation produces a specific wrong
answer.

### S1 · Only CRC-valid frames feed link accounting

A corrupted `seq` would trigger a phantom gap or a false restart. GEN3 carries a checksum
precisely so this can be gated: frames failing CRC are counted as `crc_failed` and
**excluded from sequence arithmetic entirely**.

Without this, the loss display gets least trustworthy exactly when the link is worst —
when corruption is most likely.

### S2 · The baseline is the first `seq` this backend session saw

If the dashboard is started mid-flight and the first packet is `seq=412`, expected counts
from 412 — not from 1. Otherwise it opens by reporting 411 lost packets that nobody was
listening for.

```
expected = last_seq - baseline_seq + 1
lost     = expected - received
```

### S3 · Two loss figures, because they answer different questions

| Figure | Window | Answers |
|---|---|---|
| **Rolling** | last 60 packets | "is the link healthy *right now*" |
| Session | since baseline | "how much of this flight did we get" |

Rolling is the actionable one during descent and gets prominence; session is the record.
A single cumulative figure hides a link that has just collapsed behind twenty good
minutes.

### S4 · Restart is `seq < previous_seq`, and nothing else

- `seq < previous` → **vehicle reset**. Reset baseline and counters, emit
  `vehicle_restart`, UI breaks the chart line.
- `seq == previous` → duplicate. Ignore, do not count twice.
- `seq >> previous` → a dropout, not a restart. Count the gap as loss.

Naive arithmetic on a reset reports catastrophic loss at exactly the moment someone is
trying to understand what just happened.

### S5 · No `seq` means no loss figure — and the UI must say so

GEN1 and GEN2 carry no counter, so `link` is `null` and the panel reads **"loss:
unavailable"**.

It must not read 0%. That would be a fabricated number, which is the same failure as
deriving loss from `rx_index`.

### S6 · A failed checksum rejects the frame

`ok: false`, `frame: null`, `error: "checksum mismatch"`. The raw line is still forwarded
and still shown in the feed.

Identical treatment to a malformed line, for the same reason: **a corrupt packet must
never move the altitude trace.** Seeing it in the raw feed is useful; letting it into the
charts is not.

### S7 · The chart x-axis is chosen once, at the first frame

| Generation | Axis |
|---|---|
| GEN3 | vehicle `ms` elapsed — true sampling time |
| GEN1 / GEN2 | PC arrival elapsed — the only clock available |

Chosen once and labelled on the panel, so which clock is in use is never ambiguous. Never
mixed within a session.

### S8 · `chute` renders as three states, none of them "Deployed"

| Value | UI |
|---|---|
| `0` | **ARMED** |
| `N ≥ 1` | **COMMANDED ×N** |
| `null` (GEN1) | **UNKNOWN** |

No feedback sensor exists, so "Deployed" is a claim the hardware cannot support.

## Proposed envelope

```jsonc
{
  "type": "frame",
  "rx_index": 42,              // lines arrived. Unchanged, still not a loss metric
  "seq": 1800,                 // vehicle counter. null for GEN1/GEN2
  "vehicle_ms": 1800000,       // vehicle uptime. null for GEN1/GEN2
  "pc_time": "2026-08-19T...",
  "raw": "$MRC,...*3E1A,-69.0,12.8",
  "ok": true,
  "crc_ok": true,              // null when the generation carries no checksum
  "generation": "GEN3",
  "frame": { "...": "..." },
  "link": {                    // backend-owned, correct for late joiners
    "expected": 1800,
    "received": 1794,
    "lost": 6,
    "loss_pct": 0.33
  }
}
```

Session restart is announced as its own message so the UI can break the trace:

```jsonc
{ "type": "vehicle_restart", "previous_seq": 1802, "new_seq": 1, "pc_time": "..." }
```

## Work, in dependency order

**1 · Parser** — `backend/dashboard/parser.py`
GEN3 branch: strip `$`, split on `*`, verify CRC16/CCITT-FALSE against
`firmware/tests/verify_gen3.py`, parse 17 + 2 fields. GEN1 and GEN2 stay.
Tests against the **real captured packets** in `logs/raw/FLIGHT22.CSV`, not invented ones.

**2 · Link accounting** — new `backend/dashboard/linkstats.py`
Owns `seq` tracking, gap arithmetic, restart detection, CRC counters. Pure and unit
testable, with no I/O.

**3 · Pipeline and envelope** — `pipeline.py`
Wire the above in; emit `vehicle_restart`.

**4 · Mock source** — `devtools/mock_source.py`
Emit GEN3, including a correct CRC. Gains the ability to inject deliberate `seq` gaps and
CRC failures, so the loss display can be tested without waiting for a bad link.

**5 · Frontend types and hook** — `types/telemetry.ts`, `useTelemetry.ts`
`seq`/`ms` nullable so GEN1 and GEN2 still render. Consume backend `link` stats rather
than recomputing.

**6 · Panels**

| Panel | Change |
|---|---|
| Link health | show **real loss %** and gaps; keep time-since-last on the PC clock |
| Altitude, speed, environment | x-axis from vehicle `ms` |
| Speed | vertical rate from vehicle time — removes the arrival-jitter error |
| Eject | `chute` as a count; **"Deployed" → "Commanded"** |
| Raw feed | CRC failures marked distinctly from malformed lines |

**7 · Tests** — parser, link stats, frontend logic. The captured hardware log is the
fixture.

## Deliberately out of scope, pending a decision

**SQLite store.** Still unbuilt, and the schema should carry `seq`, `vehicle_ms` and
`crc_ok`, so doing it now avoids a migration later. Separable, but this is the cheap
moment.

**Replay.** Nearly free now: the SD card holds framed GEN3 packets, so a `file_source`
pointed at `FLIGHT22.CSV` would replay a real flight through the real pipeline with no
conversion. The seam was designed for this. Still deferred unless wanted.

## What does not change

The raw-log-first ordering, the source seam, dev/launch separation, and `rx_index`
continuing to mean "lines arrived". GEN1 and GEN2 tolerance stays — bench firmware is
GEN1, and so is `CANSAT_DATA`.
