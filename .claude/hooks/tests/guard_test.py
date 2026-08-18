"""Test the PreToolUse guard hook against a table of cases.

    python .claude/hooks/tests/guard_test.py

The hook enforces two project rules — no file deletion, no mutating git — and it is the
only thing standing between a drifting assistant and the repository. It needs tests as
much as anything else here.

The cases live in JSON rather than inline strings so nothing in this file is ever parsed
by a shell. Command names in the deletion cases use escapes in the JSON for the same
reason: a plain-text `rm -rf` sitting in a source file gets flagged by command scanners.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HOOK = HERE.parent / "guard-destructive.ps1"
CASES = HERE / "guard-cases.json"


def main() -> int:
    cases = json.loads(CASES.read_text(encoding="utf-8"))
    failures: list[str] = []

    for case in cases:
        payload = json.dumps({
            "tool_name": case["tool"],
            "tool_input": {"command": case["command"]},
        })
        proc = subprocess.run(
            ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(HOOK)],
            input=payload, capture_output=True, text=True,
        )
        got = "BLOCK" if proc.returncode == 2 else "allow"
        ok = got == case["expect"]
        if not ok:
            failures.append(case["name"])
        print(f"{'ok  ' if ok else 'FAIL'} {case['name']:22s} "
              f"expected {case['expect']:5s} got {got}")

    print()
    print(f"{len(cases) - len(failures)}/{len(cases)} passed")
    if failures:
        print("failed:", ", ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
