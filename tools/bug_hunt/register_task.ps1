<#
.SYNOPSIS
    Register (or remove) the Windows Scheduled Task that runs the ShintTools
    UE5 daily bug hunt. Mirrors the launcher routine's cadence: every morning
    at 09:25 local (the launcher hunt runs about 09:10 / 07:10 UTC).

.PARAMETER At
    Local time of day to run, "HH:mm". Default 09:25.

.PARAMETER Model
    Model passed through to run_bug_hunt.ps1. Default 'sonnet'.

.PARAMETER Remove
    Unregister the task instead of creating it.

.NOTES
    Runs as the current interactive user so it inherits the gh keyring token
    and Claude Code auth from your profile. The machine must be logged on at
    the scheduled time. For an always-on alternative, use the GitHub Action
    variant (see README.md).

.EXAMPLE
    pwsh -File register_task.ps1
    pwsh -File register_task.ps1 -At 08:00 -Model opus
    pwsh -File register_task.ps1 -Remove
#>
[CmdletBinding()]
param(
    [string] $At    = '09:25',
    [ValidateSet('sonnet','opus','haiku')]
    [string] $Model = 'sonnet',
    [switch] $Remove
)

$ErrorActionPreference = 'Stop'
$TaskName = 'ShintTools UE5 Bug Hunt'

if ($Remove) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false `
        -ErrorAction SilentlyContinue
    Write-Host "Removed scheduled task '$TaskName'."
    return
}

# Prefer pwsh (7+) if present, else Windows PowerShell.
$psExe = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
if (-not $psExe) { $psExe = (Get-Command powershell).Source }

$runner = Join-Path $PSScriptRoot 'run_bug_hunt.ps1'
if (-not (Test-Path $runner)) { throw "Runner not found: $runner" }

$argline = "-NoProfile -ExecutionPolicy Bypass -File `"$runner`" -Model $Model -Pull"

$action  = New-ScheduledTaskAction -Execute $psExe -Argument $argline `
               -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -Daily -At $At
$settings = New-ScheduledTaskSettingsSet `
               -StartWhenAvailable `
               -DontStopOnIdleEnd `
               -ExecutionTimeLimit (New-TimeSpan -Minutes 20) `
               -MultipleInstances IgnoreNew
# Run as the current user, only when logged on (needs keyring + claude auth).
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
               -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $TaskName `
    -Action $action -Trigger $trigger -Settings $settings -Principal $principal `
    -Description 'Daily Claude bug hunt on the ShintTools UE5 plugin; files bug-hunt issues.' `
    -Force | Out-Null

Write-Host "Registered '$TaskName' - daily at $At (model=$Model)."
Write-Host "Test now:  Start-ScheduledTask -TaskName '$TaskName'"
Write-Host "Dry run :  pwsh -File `"$runner`" -DryRun"
