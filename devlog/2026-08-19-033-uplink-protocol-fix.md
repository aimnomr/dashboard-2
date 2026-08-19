# 033 · Uplink protocol fix — EJECT never worked

**Date** 2026-08-19
**Type** fix
**Refs** ISS-02

## What

Found while checking whether the dashboard is ready for a real hardware session. It was
not: **the eject command could never have worked.**

| | |
|---|---|
| Dashboard sent | `EJECT\n` |
| Ground station compares against | `CMD:EJECT` (`Config.h:44`) |
| Result at the pad | `[GCS] unknown command: EJECT`, and a button that does nothing |

Fixed:

- `api.py` — `ALLOWED_COMMANDS` (a set) became `UPLINK_COMMANDS` (a mapping): UI name →
  the exact bytes the firmware compares. `EJECT` → `CMD:EJECT`, `PING` → `CMD:PING`.
- `devtools/mock_source.py` — accepts the firmware's spelling and **rejects everything
  else**, including the string it used to accept.
- `panels/EjectPanel.tsx` — retitled **Uplink**, and gained a Ping control.
- `styles/panels.css` — `.uplink__test`, and a 2×2 attitude readout (below).
- `backend/tests/test_uplink_protocol.py` — 13 tests.

## Why

**The mock is why this survived.** `MockSource.send_command` matched on `"EJECT"` —
which is what the dashboard happened to send. The mock agreed with the *dashboard* rather
than with the *firmware*, so both halves of every test shared one wrong assumption and
confirmed each other. A lenient mock is worse than no mock: it converts a loud failure
into a silent one and moves the discovery to launch day.

The replay source could not have caught it either. A capture has no uplink.

So the tests **read `Config.h` directly** rather than hardcoding the expected strings. A
test asserting `"CMD:EJECT" == "CMD:EJECT"` would drift with exactly the same comfortable
silence the mock did; one that parses the flashed firmware's header fails loudly when the
two diverge.

**PING is now reachable.** The firmware has had `CMD:PING` all along and the dashboard
never exposed it. It fires nothing and is confirmed on the vehicle's OLED — its UL counter
resets — which makes it the only way to test the uplink *without deploying a parachute to
test it*. Deliberately not arm-guarded: guarding a control that fires nothing would
discourage the pre-launch check it exists for.

## Result

Verified end to end against the mock, which now rejects the old spelling — so this is a
real test rather than a restatement:

```
MOCK: CMD:PING received — no downlink effect, as on hardware
uplink PING (wire 'CMD:PING') -> transmitted=True
```

165 tests pass — 118 backend (15 new), 47 frontend.

Also fixed, and worth recording because it was **introduced two entries ago**: adding yaw
made the attitude readout a stack of four, which outgrew its panel and pushed **spin** out
of view. On that panel specifically, spin is the figure that says whether the horizon
above it can be believed — so the overflow hid the reliability cue rather than a
decoration. Now a 2×2 grid. Caught by screenshot; the typecheck and the tests were all
green.

Left open:

- **`ISS-02` is effectively resolved for GEN3** — the ground unit firmware that receives
  commands exists and works. Not marked here; the issue is written about GEN2 and
  deserves its own reading.
- **The uplink has still never been exercised against real hardware.** Only the mock's
  agreement has been fixed, and the mock is now merely *correct*, not *proof*.
