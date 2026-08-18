# PreToolUse guard for the MRCC CANSAT dashboard project.
#
# Enforces two project rules at the harness level so they cannot be violated by drift:
#   1. No file deletion  - only the user deletes files. Blanking a file counts as deletion.
#   2. No mutating git   - only the user writes to the repo. Read-only git is allowed.
#
# Contract: reads the PreToolUse JSON payload on stdin.
#   exit 0 -> allow
#   exit 2 -> block, stderr is shown to Claude as the reason

$ErrorActionPreference = 'Stop'

$raw = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }

try { $payload = $raw | ConvertFrom-Json } catch { exit 0 }

$tool = $payload.tool_name
$ti   = $payload.tool_input

function Deny([string]$msg) {
    [Console]::Error.WriteLine("BLOCKED by project rule: $msg")
    exit 2
}

# ---------------------------------------------------------------- rule 1b
# Writing empty content over a file that already exists is a disguised delete.
if ($tool -eq 'Write') {
    $path = $ti.file_path
    if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
        if ([string]::IsNullOrWhiteSpace([string]$ti.content)) {
            Deny "blanking an existing file is a deletion in disguise ($path). Propose the deletion to the user instead."
        }
    }
    exit 0
}

if ($tool -ne 'Bash' -and $tool -ne 'PowerShell') { exit 0 }

$cmd = [string]$ti.command
if ([string]::IsNullOrWhiteSpace($cmd)) { exit 0 }

# ----------------------------------------------------------------- rule 1a
# Explicit deletion commands, in either shell.
$deletePatterns = @(
    '(^|[\s;&|(`])(remove-item|rm|del|erase|rmdir|rd|ri)(\s|$)',
    '(^|[\s;&|(`])unlink(\s|$)',
    '(^|[\s;&|(`])clear-content(\s|$)',
    '\[(system\.)?io\.file\]::delete',
    '\.delete\('
)
foreach ($p in $deletePatterns) {
    if ($cmd -imatch $p) {
        Deny "this command deletes files. All deletion is the user's action - list the paths and the reason instead.`nCommand: $cmd"
    }
}

# ------------------------------------------------------------------ rule 2
# Git: allowlist of read-only subcommands. Anything else is the user's job.
$readOnly = @(
    'status','log','diff','show','blame','grep','shortlog','describe',
    'rev-parse','rev-list','name-rev','cat-file','ls-files','ls-tree',
    'ls-remote','for-each-ref','symbolic-ref','check-ignore','count-objects',
    'whatchanged','version','help','reflog','var','diff-tree','diff-index'
)

# Split on shell separators so `foo && git push` is still inspected.
$segments = [regex]::Split($cmd, '(?:\|\||&&|;|\||\r?\n)')

foreach ($seg in $segments) {
    $s = $seg.Trim()
    if ($s -notmatch '(?i)(^|[\s;&|(`])git(\.exe)?(\s|$)') { continue }

    # Strip everything up to and including the `git` token, then drop global flags.
    $rest = [regex]::Replace($s, '(?is)^.*?(^|[\s;&|(`])git(\.exe)?\s*', '')
    $tokens = @($rest -split '\s+' | Where-Object { $_ -ne '' })

    $sub = $null
    for ($i = 0; $i -lt $tokens.Count; $i++) {
        $t = $tokens[$i]
        if ($t -match '^--(no-pager|git-dir=|work-tree=|namespace=|literal-pathspecs|bare)') { continue }
        if ($t -eq '-C' -or $t -eq '-c') { $i++; continue }
        if ($t.StartsWith('-')) { continue }
        $sub = $t.ToLower()
        $rem = @($tokens[($i + 1)..($tokens.Count - 1)] | Where-Object { $_ -ne $null })
        break
    }

    if (-not $sub) {
        # bare `git` / `git --version` style - harmless
        continue
    }

    if ($readOnly -contains $sub) { continue }

    # Subcommands that are read-only only in certain shapes.
    $ok = $false
    switch ($sub) {
        'branch' {
            $ok = ($rem.Count -eq 0) -or (@($rem | Where-Object {
                $_ -notmatch '^-(a|v|vv|r|-list|-all|-verbose|-remotes|-show-current|-contains|-merged|-no-merged)$'
            }).Count -eq 0)
        }
        'config' { $ok = ($rem.Count -gt 0) -and ($rem[0] -match '^(--get|--get-all|--get-regexp|--list|-l)$') }
        'remote' { $ok = ($rem.Count -eq 0) -or ($rem[0] -match '^(-v|--verbose|show|get-url)$') }
        'stash'  { $ok = ($rem.Count -gt 0) -and ($rem[0] -match '^(list|show)$') }
        'tag'    { $ok = ($rem.Count -eq 0) -or (@($rem | Where-Object { $_ -notmatch '^-(l|-list|n\d*)$' }).Count -eq 0) }
        'worktree' { $ok = ($rem.Count -gt 0) -and ($rem[0] -eq 'list') }
        'submodule' { $ok = ($rem.Count -gt 0) -and ($rem[0] -eq 'status') }
    }
    if ($ok) { continue }

    Deny "``git $sub`` is not read-only. The user runs all mutating git commands - propose a commit title and body instead.`nCommand: $s"
}

exit 0
