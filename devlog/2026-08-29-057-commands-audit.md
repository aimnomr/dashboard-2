# 057 · `CHUTE_PIN` reconciled, and the command reference audited against the code

**Date** 2026-08-29
**Type** change
**Refs** 054, 055, 056

Entry 055 recorded `CHUTE_PIN` as `D3` "in both files". It was `D3` in GEN4 and `3` in
GEN3, and only GEN4 could fail to compile.

## What

**`MRC_FlightUnit_GEN4/Config.h:81` is `3`.** All five `CHUTE_*` defines are now identical
between generations — `USE_SERVO 1 · PIN 3 · ARMED 90 · RELEASE 160 · HOLD_MS 1000`. No
`D3` remains anywhere in `firmware/`.

**`COMMANDS-QUICK.md`**, ~110 lines. Every command with the prose stripped out, for
someone who has read `COMMANDS.md` once and wants to copy a line rather than retype it.
Comments carry only what cannot be guessed: value ranges, and the two commands that are
dangerous. It ends with the launch-day sequence in order. Cross-linked both ways, and
from the README.

**`COMMANDS.md` audited against the code rather than reread.** Every CLI's `--help` was
captured and compared, and `dashboard.api`'s command tables were diffed against the
document programmatically. Six gaps and two errors:

| | |
|---|---|
| counts | 141/73/214 → **145/94/239** |
| §0 | activating the venv in **every** terminal, and what the failure looks like |
| intro | `--help` exists on all four entry points, and outranks this file |
| §4 | `EJECT`, `RESET`, `SET:ARM`, `SET:AUTO` were in the table but not the copyable block |
| §6 | narrowing a test run — one file, `-k`, `-x`, and the frontend equivalent |
| §8 | `ServoEjectTest` was **absent entirely**; `/api/session` and the 503 check added |

**Two errors, not omissions.** `RESET:CHUTE` was documented as "clear the chute counter",
which is the one thing it does not do — `chuteCommands` is deliberately untouched by both
resets (`Apogee.ino:212`). It clears the *fire latch*. And `RESET` was listed without the
warning that it re-bases the trigger rather than cancelling it.

**A markdown bug.** `` `SET:AUTO:<0|1>` `` carried an unescaped pipe inside a table cell.
Backticks do not protect pipes in GFM tables, so that row had been rendering as four
columns. Now `\|`.

## Why

**The `CHUTE_PIN` divergence was invisible from either file alone.** Each is internally
consistent; only diffing the pair shows it. It surfaced from a question about what
changes when flashing GEN4 — which is the same shape as entry 055's own finding about
the servo angles, and the argument for GEN3 and GEN4 being diffed rather than read.

**A reference nobody checks against the code decays into a reference that is believed and
wrong.** `COMMANDS.md` was four days old and already carried two false statements about
the most dangerous command in the system. The check that found them is cheap and
repeatable: capture `--help`, import the command tables, compare. It is worth re-running
whenever the grammar moves.

**The quick sheet exists because the full one answers a different question.** §4 spends
fifty lines on why `ul` rising does not prove a value was applied. That is the right
content and the wrong shape for someone at a laptop who needs the `SET:DROP` line. Two
files, two jobs, and the quick one opens by saying read the other first.

## Result

**The quick sheet carries no claims that can go stale** — no test counts, no behavioural
notes, just invocations. That is deliberate: it is the file most likely to be copied and
least likely to be reread, so it holds nothing that could quietly become false. Everything
that can rot stayed in `COMMANDS.md`.

**Every command in it was executed before it was written.** `pytest -k`, `-x`, the single
file form, `npm run test -- packet`, `curl /api/session` — all run, all confirmed. The
`wiki/decisions/pre-launch-checklist.md` link resolves.

**The bench sketch's pin is deliberately different, and this entry first said otherwise.**
`ServoEjectTest.ino:38` uses `servoPin = 18` against `CHUTE_PIN 3`, and the block at
`ServoEjectTest.ino:27` already explains why: the rig is a classic ESP32, the flight unit
an ESP32-S3, and it says outright "do not copy these numbers into Config.h". The ANGLES
are the shared pair and they agree; the PIN was never meant to. Recorded because the
mistake was made twice in one session — the file was flagged as drift on the strength of a
grep that did not include the comment twelve lines above the value.

Matching it to `3` would also have broken the sketch: GPIO 3 is `U0RXD` on a classic
ESP32, and the sketch reports through `Serial` at 115200 on that same UART.

**Two questions the reconciliation did not settle.** `servoPin = 18` collides with
`OLED_SCL 18` if the rig is ever moved onto a spare Heltec V3 — harmless today, a trap
later. And GPIO 3 is a strapping pin on the ESP32-S3 (JTAG source select), which is part
of why entry 055 preferred 47. Both generations now agreeing on `3` is not evidence that
`3` is right.

**Still not compiled.** The `CHUTE_PIN` change makes GEN4 *able* to compile where `D3` may
have stopped it; it does not prove GPIO 3 is right. Same gate as 052, 053 and 055 — no
Arduino toolchain here, and GEN3 remains the only pair proven on hardware.
