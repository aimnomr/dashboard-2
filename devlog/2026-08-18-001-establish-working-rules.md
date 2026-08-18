# 001 · Establish working rules and enforcement hooks

**Date** 2026-08-18
**Type** decision
**Refs** —

## What

Agreed six working rules for the project: joint plan/propose/execute decisions, an append-only
devlog, a `status.md` read at session start and written at session end, git operations reserved
to Aiman, a `wiki/` knowledge base, and a hard no-deletion limit.

Wired the two hard limits as a `PreToolUse` hook rather than leaving them to memory:

- `.claude/hooks/guard-destructive.ps1`
- `.claude/settings.json`

The guard blocks file deletion (`rm`, `del`, `Remove-Item`, `rmdir`, `unlink`, `Clear-Content`,
`.Delete()`), blocks writing empty content over an existing file, and allowlists read-only git
subcommands while blocking every mutating one. Chained commands are inspected per segment.

## Why

Behavioural rules drift. The two that must never fail — no deletion, no mutating git — are
enforced by the harness instead, making violation impossible rather than merely promised.

Rules were also written to persistent memory so they load automatically in future sessions.

## Result

Verified against 13 test payloads: `git status`, `git log`, `git diff`, `git branch -a` and
non-git commands pass; `git commit`, `git push`, `git add`, `git branch -D`, `npm test && git
push`, `rm -rf` and `Remove-Item` are blocked.

Known caveat: commands run via the `!` prefix inside a Claude Code session may also be blocked,
so git work should happen in a normal terminal.
