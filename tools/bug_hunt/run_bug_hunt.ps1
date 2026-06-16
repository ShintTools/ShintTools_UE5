<#
.SYNOPSIS
    Daily bug-hunt for the ShintTools UE5 plugin. Runs Claude Code headless
    against Source/ShintTools/**, finds one high-confidence defect, and files
    it as a `bug-hunt` GitHub issue - mirroring the launcher's daily routine.

.DESCRIPTION
    Mechanism-agnostic: run it manually, from Windows Task Scheduler (see
    register_task.ps1), or from a CI runner. It:
      1. Optionally pulls the latest main so it scans current code.
      2. Collects open `bug-hunt` issue titles for de-duplication.
      3. Invokes `claude -p` with PROMPT.md + the dedup list.
      4. Claude reads the code and runs `gh issue create` itself (or, with
         -DryRun, prints the proposed issue instead of filing).
    All output is tee'd to tools/bug_hunt/logs/hunt_<date>.log.

.PARAMETER Repo
    owner/name of the GitHub repo to file issues against.

.PARAMETER RepoDir
    Local checkout to scan. Defaults to this script's repo root.

.PARAMETER Model
    Claude model for the hunt. Default 'sonnet' keeps daily cost low; pass
    'opus' for a deeper sweep.

.PARAMETER DryRun
    Find + print the proposed issue but do NOT file it.

.PARAMETER Pull
    `git fetch && checkout main && pull` before scanning.

.EXAMPLE
    pwsh -File run_bug_hunt.ps1 -DryRun
    pwsh -File run_bug_hunt.ps1            # files one issue
#>
[CmdletBinding()]
param(
    [string] $Repo    = 'Noctxas97Dev/ShintTools_UE5',
    [string] $RepoDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [ValidateSet('sonnet','opus','haiku')]
    [string] $Model   = 'sonnet',
    [switch] $DryRun,
    [switch] $Pull
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Preconditions -----------------------------------------------------------
foreach ($exe in 'claude','gh','git') {
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) {
        throw "Required executable '$exe' not found on PATH."
    }
}

$logDir = Join-Path $PSScriptRoot 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir ("hunt_{0}.log" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))

function Write-Log([string] $msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $msg
    Write-Host $line
    Add-Content -Path $log -Value $line -Encoding utf8
}

Write-Log "Bug hunt start - repo=$Repo dir=$RepoDir model=$Model dryRun=$DryRun"

Push-Location $RepoDir
try {
    # --- Optional refresh ----------------------------------------------------
    if ($Pull) {
        Write-Log "git fetch + checkout main + pull"
        git fetch --quiet origin
        git checkout --quiet main
        git pull --quiet --ff-only
    }

    # --- De-dup context: open bug-hunt issue titles --------------------------
    Write-Log "Collecting open bug-hunt issues for de-dup"
    $openJson = gh issue list --repo $Repo --label bug-hunt --state open `
                    --limit 100 --json number,title 2>$null
    $open = @()
    if ($openJson) { $open = @($openJson | ConvertFrom-Json) }
    if ($open.Count -gt 0) {
        $openList = ($open | ForEach-Object { "  - #$($_.number): $($_.title)" }) -join "`n"
    }
    else {
        $openList = "  (none open)"
    }
    Write-Log "Open bug-hunt issues: $($open.Count)"

    # --- Compose the runtime prompt ------------------------------------------
    $promptCore = Get-Content -Raw -Encoding UTF8 -Path (Join-Path $PSScriptRoot 'PROMPT.md')
    if ($DryRun) {
        $modeNote = "RUN MODE: DRY RUN. Do NOT run gh issue create. Instead print the full proposed issue (title + body) to stdout, then stop."
    }
    else {
        $modeNote = "RUN MODE: LIVE. File the issue with gh issue create against repo $Repo, label bug-hunt."
    }

    $runtime = @"
$promptCore

---

## Runtime context (injected)

REPO: $Repo

$modeNote

### Open bug-hunt issues - do NOT duplicate any of these:
$openList
"@

    # --- Invoke Claude headless ----------------------------------------------
    # allowedTools whitelists read-only inspection + the gh/git CLIs so the run
    # is unattended (no permission prompts). Claude does the reading and the
    # gh issue create itself.
    Write-Log "Invoking claude -p (model=$Model)"
    $runtime | claude -p `
        --model $Model `
        --add-dir $RepoDir `
        --allowedTools 'Read,Grep,Glob,Bash' `
        2>&1 | Tee-Object -FilePath $log -Append

    $code = $LASTEXITCODE
    Write-Log "claude exited with code $code"
    exit $code
}
finally {
    Pop-Location
}
