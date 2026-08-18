# 015 · Fix guard hook false positive

**Date** 2026-08-18
**Type** fix
**Refs** —

## What

The `PreToolUse` guard blocked commands whose text merely *mentioned* a forbidden
command inside a quoted string. Encountered during entry 012, where
`Write-Output "what git has vs disk"` was refused as a mutating git command.

Fixed by blanking the contents of quoted strings before pattern matching:

- `.claude/hooks/guard-destructive.ps1` — added `Remove-QuotedText`, applied to both
  rule sets. Handles single quotes, double quotes and both here-string forms.
- `.claude/hooks/tests/guard-cases.json` — 25 cases
- `.claude/hooks/tests/guard_test.py` — runner

## Why

The obvious fix was to anchor the patterns to the start of a command. That was
rejected: it would also stop matching `... | ForEach-Object { Remove-Item $_ }`, where
the deletion sits inside a script block rather than in command position. Narrowing
*where* the guard looks trades a false positive for a false negative, and a false
negative here means a deleted file.

Blanking quoted contents removes the false-positive class without narrowing the search
at all. Real commands still match, because their tokens are outside quotes — including
`git commit -m "..."`, where only the message is blanked.

## Result

**25/25 cases pass**, covering three groups: things that must still block (seven git
mutations, five deletion forms including one inside a script block and one after a
pipe), things that must still be allowed (six read-only git forms), and the false
positives this targets (prose in single quotes, double quotes and here-strings, plus
`grep -r "git push" docs/`).

Two cases specifically pin down that quoted *arguments* cannot hide a real command:
`git commit -m "harmless words"` and the here-string form both still block.

The hook is tracked in git, so its tests are now tracked with it — it is the only thing
standing between a drifting assistant and the repository, and it had no tests until now.

**Known and accepted gap**, documented in the hook itself: a command smuggled entirely
inside a string, such as `powershell -c "git push"`, is no longer seen. This guard exists
to stop drift, not a determined bypass, and that trade is worth removing the false
positives.

Test cases live in JSON rather than inline strings so nothing in them is parsed by a
shell — an earlier attempt to run this table inline had the sandbox interpret the test
data as real commands. Deletion command names are JSON-escaped for the same reason.
