# ShintTools UE5 — Daily Bug Hunt

A daily routine that points Claude Code at the plugin's C++/Slate sources,
finds **one** high-confidence defect, and files it as a `bug-hunt` GitHub issue
— the same shape the launcher routine uses (Bug / File / Lines / Faulty code /
Root cause / Trigger path / Suggested fix).

## Files

| File | Role |
|---|---|
| `PROMPT.md` | The hunt instructions (scope, what is/isn't a bug, output template). |
| `run_bug_hunt.ps1` | Runner — gathers open issues for de-dup, invokes `claude -p`, logs to `logs/`. |
| `register_task.ps1` | Registers/removes the Windows Scheduled Task (local cadence). |
| `.github/workflows/bug-hunt.yml` | Always-on cloud alternative (GitHub Actions cron). |

## Prerequisites

- `claude` (Claude Code CLI) on PATH and signed in.
- `gh` authenticated as the account that should author the issues.
- The `bug-hunt` label exists on the repo (already created).

## Run it manually

```powershell
# Dry run — finds a bug and prints the proposed issue, files nothing:
pwsh -File tools/bug_hunt/run_bug_hunt.ps1 -DryRun

# Live — files one issue:
pwsh -File tools/bug_hunt/run_bug_hunt.ps1

# Deeper sweep with Opus, refreshing main first:
pwsh -File tools/bug_hunt/run_bug_hunt.ps1 -Model opus -Pull
```

## Option A — local schedule (Windows Task Scheduler)

Mirrors the launcher routine: runs each morning under your user account, so it
inherits your `gh` keyring token and Claude auth.

```powershell
pwsh -File tools/bug_hunt/register_task.ps1            # daily 09:25, sonnet
pwsh -File tools/bug_hunt/register_task.ps1 -At 08:00 -Model opus
pwsh -File tools/bug_hunt/register_task.ps1 -Remove    # uninstall
```

The machine must be logged on at the scheduled time.

## Option B — always-on (GitHub Actions)

`bug-hunt.yml` runs the same prompt on a cron in the cloud, independent of your
PC. It needs one repo secret:

- `ANTHROPIC_API_KEY` — billed against the Anthropic API (not your Claude
  subscription). `GITHUB_TOKEN` (auto-provided) authors the issues as
  `github-actions[bot]`.

Enable it by adding the secret under **Settings → Secrets and variables →
Actions**, then the workflow fires daily at 07:25 UTC.

## Tuning

- **Noise**: the prompt is told to file nothing on a quiet day (`NO_NEW_BUG_FOUND`)
  rather than invent a weak bug. If it still over-files, tighten the
  "What is NOT a bug" section in `PROMPT.md`.
- **Cost**: `sonnet` is the default for a daily cadence. Use `opus` weekly for a
  deeper pass.
- **Scope**: edit the Scope section in `PROMPT.md` to include/exclude paths.
- **Variants**: the prompt reviews all three shipped build variants -
  Marketplace (`SHINT_MARKETPLACE_BUILD=1`), Free (`SHINT_FREE_TIER=1`) and Paid -
  plus the runtime tier gating, so defects inside `#if` branches that the
  default developer config compiles out are still found. Each issue tags which
  variant(s) it affects.
