# build_winxshell_zip.ps1 - create a portable, framework-dependent x64 release ZIP.
#
# The Windows App Runtime itself is intentionally not bundled: WinXShell uses
# the framework-dependent deployment model and bootstraps the pinned runtime at
# startup. The bootstrap DLL must stay beside WinXShell.exe.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_winxshell_zip.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_winxshell_zip.ps1 -Version 0.1.0
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build_winxshell_zip.ps1 -SkipBuild

[CmdletBinding()]
param(
    [string]$Version,
    [switch]$SkipBuild,
    [int]$BudgetMB = 10
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here "..")).Path
$project = Join-Path $root "WinXShell.vcxproj"
$output = Join-Path $root "x64\Release"
$dist = Join-Path $root "dist"

if (-not (Test-Path $project)) { throw "WinXShell.vcxproj not found at $project" }

if (-not $Version) {
    $revision = (& git -C $root rev-parse --short HEAD 2>$null).Trim()
    if (-not $revision) { $revision = "local" }
    $Version = "0.0.0-dev-$revision"
}
if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z._-]*$') {
    throw "Version must contain only letters, digits, dot, underscore, or dash."
}

if (-not $SkipBuild) {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }
    $vsRoot = (& $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
    if (-not $vsRoot) {
        $vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    }
    $msbuild = Join-Path $vsRoot "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) { throw "MSBuild.exe not found at $msbuild" }
    $vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    $buildCommand = 'call "{0}" && "{1}" "{2}" /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64' -f $vcvars, $msbuild, $project
    & cmd.exe /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE" }
}

$exe = Join-Path $output "WinXShell.exe"
$bootstrap = Join-Path $output "Microsoft.WindowsAppRuntime.Bootstrap.dll"
foreach ($file in @($exe, $bootstrap)) {
    if (-not (Test-Path $file)) { throw "Required release file is missing: $file" }
}

$packageName = "WinXShell-$Version-x64"
$stage = Join-Path $dist $packageName
$zip = Join-Path $dist "$packageName.zip"
$verify = Join-Path $dist ".$packageName-verify"

Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $verify -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item -LiteralPath $bootstrap -Destination $stage
foreach ($file in @("WinXShell.jcfg", "WinXShell.lua")) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $stage
}
foreach ($directory in @("Resources", "en_US", "StartMenuUI")) {
    Copy-Item -LiteralPath (Join-Path $root $directory) -Destination $stage -Recurse
}

@"
WinXShell $Version (x64)

This archive is framework-dependent. Before running WinXShell.exe, install the
Microsoft Windows App Runtime 2.3.1 x64. Keep
Microsoft.WindowsAppRuntime.Bootstrap.dll next to WinXShell.exe; it selects the
pinned runtime before WinUI 3 is loaded.

Run WinXShell.exe from this extracted folder. Start-menu content remains the
legacy implementation during the taskbar revamp.
"@ | Set-Content -LiteralPath (Join-Path $stage "README.txt") -Encoding utf8

$payloadBytes = (Get-ChildItem -LiteralPath $stage -File -Recurse | Measure-Object -Property Length -Sum).Sum
if ($payloadBytes -gt ($BudgetMB * 1MB)) {
    throw ("Release payload is {0:N2} MB, above the {1} MB budget." -f ($payloadBytes / 1MB), $BudgetMB)
}

Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal -Force
Expand-Archive -LiteralPath $zip -DestinationPath $verify -Force
$verifyRoot = Join-Path $verify $packageName
foreach ($relative in @("WinXShell.exe", "Microsoft.WindowsAppRuntime.Bootstrap.dll", "WinXShell.jcfg", "Resources", "en_US")) {
    if (-not (Test-Path (Join-Path $verifyRoot $relative))) {
        throw "ZIP verification failed: missing $relative"
    }
}
Remove-Item -LiteralPath $verify -Recurse -Force

$zipItem = Get-Item -LiteralPath $zip
Write-Host ("release zip: {0}" -f $zipItem.FullName)
Write-Host ("payload:     {0:N2} MB" -f ($payloadBytes / 1MB))
Write-Host ("zip:         {0:N2} MB" -f ($zipItem.Length / 1MB))
