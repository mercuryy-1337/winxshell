# check_size.ps1 — size guard for the WinUI 3 revamp.
#
# Phase 0 of REVAMP.md: the shipped shell (Explauncher.exe + any DLLs/assets we
# add for WinUI 3) must stay under 10 MB. Third-party tools bundled in
# release/explauncher/Explorer/ (PeaZip, 7z, etc.) are out of scope.
#
# This script is intended to run without admin privileges on the dev box AND on
# the remote VM. Exit code is non-zero if the budget is exceeded.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check_size.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check_size.ps1 -ThemeDir "C:\path\to\Theme"
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check_size.ps1 -BudgetMB 10 -WarnMB 9

[CmdletBinding()]
param(
    [string]$ThemeDir,
    [int]$BudgetMB = 10,
    [int]$WarnMB = 9
)

$ErrorActionPreference = "Stop"

if (-not $ThemeDir) {
    $here = Split-Path -Parent $MyInvocation.MyCommand.Path
    $ThemeDir = Join-Path $here "..\release\explauncher\Theme"
}

if (-not (Test-Path $ThemeDir)) {
    Write-Host "size-guard: Theme dir not found: $ThemeDir" -ForegroundColor Red
    exit 2
}

# What counts toward the budget. Keep this list narrow; new WinUI 3 deps we
# ship (e.g. Microsoft.WindowsAppRuntime.Bootstrap.dll if we ever vendor it)
# should be added here as they're introduced.
$counted = @(
    "Explauncher.exe",
    "wxsStub.dll",
    "wxsStub32.dll"
)

# Glob patterns also pulled into the budget (relative to ThemeDir).
$countedGlobs = @(
    "wxsUI\*"
)

$entries = @()
foreach ($name in $counted) {
    $p = Join-Path $ThemeDir $name
    if (Test-Path $p) {
        $entries += Get-Item $p
    }
}
foreach ($glob in $countedGlobs) {
    $p = Join-Path $ThemeDir $glob
    $entries += Get-ChildItem -Path $p -File -Recurse -ErrorAction SilentlyContinue
}

if ($entries.Count -eq 0) {
    Write-Host "size-guard: no shipped files found under $ThemeDir" -ForegroundColor Red
    exit 2
}

$total = ($entries | Measure-Object -Property Length -Sum).Sum
$budget = $BudgetMB * 1MB
$warn = $WarnMB * 1MB

# Per-file table (sorted largest first) so a regression is easy to attribute.
$rel = $ThemeDir.TrimEnd('\').Length + 1
$rows = $entries |
    Sort-Object Length -Descending |
    ForEach-Object {
        [pscustomobject]@{
            File = $_.FullName.Substring($rel)
            KB   = [math]::Round($_.Length / 1KB, 1)
        }
    }
($rows | Format-Table -AutoSize | Out-String -Width 4096).TrimEnd() | Write-Host

$totalMB = [math]::Round($total / 1MB, 2)
$budgetMB = [math]::Round($budget / 1MB, 2)

if ($total -gt $budget) {
    Write-Host "size-guard: FAIL  $totalMB MB > $budgetMB MB budget" -ForegroundColor Red
    exit 1
}
if ($total -gt $warn) {
    Write-Host "size-guard: WARN  $totalMB MB > $WarnMB MB warn threshold (budget $budgetMB MB)" -ForegroundColor Yellow
    exit 0
}
Write-Host "size-guard: OK    $totalMB MB / $budgetMB MB" -ForegroundColor Green
exit 0
