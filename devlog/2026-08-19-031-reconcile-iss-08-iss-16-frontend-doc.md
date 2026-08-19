# 031 · Reconcile ISS-08 and ISS-16, and correct frontend.md

**Date** 2026-08-19
**Type** decision
**Refs** ISS-08, ISS-16

## What

Recorded state that the last four entries had made true but never written down.

**`wiki/issues.md`**

| Issue | Was | Now |
|---|---|---|
| ISS-08 — no packet counter, timestamp, or checksum | 🟡 Deferred | 🟢 Resolved |
| ISS-16 — replay not built | 🟡 Deferred | 🟢 Resolved |

Both index rows and both detail entries updated, with **Resolution** filled in. The original
deferral reasoning is kept in place beneath each, per the tracker's own rule that resolved
issues stay as the record of *why*.

**`wiki/decisions/frontend.md`** — four passages that GEN3 had invalidated:

1. *"Hard constraint: link health cannot show packet loss"* — rewritten. The superseded rule
   is quoted rather than removed, since its reasoning is unchanged.
2. Chute state row — `ARMED / DEPLOYED` corrected to `ARMED / COMMANDED ×N / UNKNOWN` (S8),
   marked as decided but not yet implemented.
3. Attitude row — now mentions integrated yaw.
4. New section on the third axis, and a status note on the 3D-pose revert condition.

## Why

ISS-08 was the root of all of it. It said the vehicle sent no counter, no timestamp and no
checksum; GEN3 added all three, and entries 019–022, 027, 028 and 030 carried them end to
end. Everything downstream that had been *designed around* the absence was still documented
as if the absence held.

That is the specific danger of a deferred issue: the workarounds get written into the
design docs, and when the issue resolves the workarounds stay behind as rules. Someone
reading `frontend.md` today would have concluded that a loss figure was forbidden outright,
when the real position is that it is now required — computed in the backend, on GEN3 only.

ISS-16 was simply delivered in entry 029 and never marked.

**The 3D-pose revert stands.** Entry 014 asked that it not be re-proposed without new
information, and set a condition: solve the unmeasured-rotation problem first. That
condition is now *partially* met — yaw can be integrated — which is exactly the situation in
which a rejected idea quietly creeps back. It is recorded as partially met and **not
enough**: a drifting, boot-relative estimate is a weak foundation for a pose someone would
look at to make a decision. Yaw was added as a number, deliberately not as a rendering.

## Result

No code changed. Documentation now matches what is built.

Two gaps are now recorded rather than latent:

- **`lib/link.ts` still renders "Deployed"**, which S8 forbids. The plan has said so since
  entry 018; it is step 6 and remains unbuilt. Writing the corrected label into
  `frontend.md` without flagging this would have created a doc that contradicted shipped
  code, so the row states both the decision and the current state.
- **No loss figure is displayed for any generation**, because `linkstats.py` (step 2) does
  not exist. That is correct behaviour and not a leftover of ISS-08 — but it is now the
  only thing standing between `seq` and a real loss percentage.

ISS-15 (SQLite store) remains 🟡 Deferred and genuinely so — nothing has been built toward
it. Its note that the schema should carry `seq`, `vehicle_ms` and `crc_ok` is now more
pressing, since those fields exist and flow through the envelope.
