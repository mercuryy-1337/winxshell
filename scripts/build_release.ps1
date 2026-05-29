# build_release.ps1 - build WinXShell in Release config and stage the binary
# for shipping to the test VM.
#
# Steps:
#   1. Locate MSBuild via vswhere.
#   2. msbuild WinXShell.vcxproj /p:Configuration=Release /p:Platform=$Platform.
#   3. Copy the freshly built WinXShell.exe to release/explauncher/Theme/Explauncher.exe.
#   4. Run scripts/check_size.ps1 (size guard).
#   5. Optionally rebuild release/explauncher/explauncher.zip for VM transfer.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_release.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform Win32
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Zip
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Clean

[CmdletBinding()]
param(
    [ValidateSet("x64","Win32","ARM")]
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$Zip,
    [switch]$SkipSizeCheck
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $here "..")
$proj = Join-Path $root "WinXShell.vcxproj"
$themeDir = Join-Path $root "release\explauncher\Theme"
$stagedExe = Join-Path $themeDir "Explauncher.exe"

if (-not (Test-Path $proj)) { throw "vcxproj not found at $proj" }

# locate MSBuild
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at $vswhere. Install Visual Studio 2022 or VS Build Tools."
}
$vsRoot = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsRoot) { throw "vswhere could not find a VS install with MSBuild." }
$msbuild = Join-Path $vsRoot "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) { throw "MSBuild not found at $msbuild" }

Write-Host "build: MSBuild       $msbuild"
$cfgLine = "build: Configuration Release|" + $Platform
Write-Host $cfgLine
Write-Host "build: Project       $proj"
Write-Host ""

# bootstrap lua vendor deps if missing (one-time per fresh clone).
# Replicates vendor/lua/build.bat, but uses our discovered MSBuild instead of
# the hardcoded path in the bat (D:\Program Files\... which won't exist here).
$luaDir = Join-Path $root "vendor\lua"
$luaInclude = Join-Path $luaDir "include\lua.hpp"
$luaLib = Join-Path $luaDir "lib\lua_x64.lib"
if (-not (Test-Path $luaInclude) -or -not (Test-Path $luaLib)) {
    Write-Host "build: bootstrapping lua vendor deps..."
    # bin.zip ships a "7za" subtree but the exe inside is named 7z.exe.
    $sevenz = Join-Path $luaDir "vsbuild\7za\x64\7z.exe"
    if (-not (Test-Path $sevenz)) {
        $unzip = Join-Path $luaDir "vsbuild\unzip.exe"
        $binzip = Join-Path $luaDir "vsbuild\bin.zip"
        if (-not (Test-Path $unzip) -or -not (Test-Path $binzip)) {
            throw "lua bootstrap: vendor/lua/vsbuild/unzip.exe + bin.zip not found"
        }
        Push-Location (Join-Path $luaDir "vsbuild")
        try { & $unzip -o bin.zip | Out-Null } finally { Pop-Location }
    }
    if (-not (Test-Path $sevenz)) { throw "lua bootstrap: 7z.exe still missing after bin.zip extract" }

    $luaName = "lua-5.4.7"
    $cjsonName = "lua-cjson-2.1.0"
    $luaSrc = Join-Path $luaDir "lua"
    if (-not (Test-Path $luaSrc)) {
        Push-Location $luaDir
        try {
            & $sevenz x "$luaName.tar.gz" -y | Out-Null
            & $sevenz x "$luaName.tar" -y | Out-Null
            Remove-Item "$luaName.tar" -Force
            Rename-Item $luaName "lua"
            Copy-Item "vsbuild\wmain.c" "lua\src\" -Force
            New-Item -ItemType Directory -Force "include" | Out-Null
            New-Item -ItemType Directory -Force "lib" | Out-Null
            Move-Item "lua\src\lua.h" "include\" -Force
            Move-Item "lua\src\lua.hpp" "include\" -Force
            Move-Item "lua\src\lualib.h" "include\" -Force
            Move-Item "lua\src\lauxlib.h" "include\" -Force
            Move-Item "lua\src\luaconf.h" "include\" -Force
        } finally { Pop-Location }
    }
    $cjsonSrc = Join-Path $luaDir "lua-cjson"
    if (-not (Test-Path $cjsonSrc)) {
        Push-Location $luaDir
        try {
            & $sevenz x "$cjsonName.zip" -y | Out-Null
            Rename-Item $cjsonName "lua-cjson"
            Copy-Item "vsbuild\patch\lua-cjson\dtoa.c" "lua-cjson\dtoa.c" -Force
        } finally { Pop-Location }
    }

    # Build the bundled lua.sln (Release + Debug, both x64 and Win32, to match the bat).
    $luaSln = Join-Path $luaDir "vsbuild\lua.sln"
    foreach ($cfg in @(@{C="Release";P="x64"}, @{C="Debug";P="x64"}, @{C="Release";P="Win32"}, @{C="Debug";P="Win32"})) {
        & $msbuild $luaSln "/p:Configuration=$($cfg.C)" "/p:Platform=$($cfg.P)" /m /nologo /verbosity:minimal
        if ($LASTEXITCODE -ne 0) { throw "lua bootstrap: msbuild lua.sln $($cfg.C)|$($cfg.P) failed" }
    }
    $libOut = Join-Path $luaDir "lib"
    New-Item -ItemType Directory -Force $libOut | Out-Null
    Copy-Item (Join-Path $luaDir "vsbuild\x64\Debug\lua.lib")           (Join-Path $libOut "lua_d_x64.lib")        -Force
    Copy-Item (Join-Path $luaDir "vsbuild\x64\Release\lua.lib")         (Join-Path $libOut "lua_x64.lib")          -Force
    Copy-Item (Join-Path $luaDir "vsbuild\Debug\lua.lib")               (Join-Path $libOut "lua_d.lib")            -Force
    Copy-Item (Join-Path $luaDir "vsbuild\Release\lua.lib")             (Join-Path $libOut "lua.lib")              -Force
    Copy-Item (Join-Path $luaDir "vsbuild\x64\Debug\lua-cjson.lib")     (Join-Path $libOut "lua-cjson_d_x64.lib")  -Force
    Copy-Item (Join-Path $luaDir "vsbuild\x64\Release\lua-cjson.lib")   (Join-Path $libOut "lua-cjson_x64.lib")    -Force
    Copy-Item (Join-Path $luaDir "vsbuild\Debug\lua-cjson.lib")         (Join-Path $libOut "lua-cjson_d.lib")      -Force
    Copy-Item (Join-Path $luaDir "vsbuild\Release\lua-cjson.lib")       (Join-Path $libOut "lua-cjson.lib")        -Force
    Write-Host "build: lua deps bootstrapped"
    Write-Host ""
}

# build
$target = if ($Clean) { "Rebuild" } else { "Build" }
$msbArgs = @(
    $proj,
    "/t:$target",
    "/p:Configuration=Release",
    "/p:Platform=$Platform",
    "/m",
    "/nologo",
    "/verbosity:minimal"
)
& $msbuild @msbArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "build: FAIL  msbuild exited $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

# locate output. OutDir per vcxproj is $(SolutionDir)$(PlatformName)\$(Configuration)
# Resulting exe is named after the project (WinXShell.exe), then renamed on stage.
$outDir = Join-Path $root "$Platform\Release"
$builtExe = Join-Path $outDir "WinXShell.exe"
if (-not (Test-Path $builtExe)) {
    Write-Host "build: FAIL  expected build output not found at $builtExe" -ForegroundColor Red
    exit 2
}

# stage
if (-not (Test-Path $themeDir)) { throw "Theme dir not found at $themeDir" }
Copy-Item -LiteralPath $builtExe -Destination $stagedExe -Force
$staged = Get-Item $stagedExe
Write-Host ""
$stageMsg = "build: staged       {0} ({1:N1} KB)" -f $stagedExe, ($staged.Length / 1KB)
Write-Host $stageMsg

# size guard
if (-not $SkipSizeCheck) {
    Write-Host ""
    & (Join-Path $here "check_size.ps1") -ThemeDir $themeDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "build: size guard failed (see above)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# optional zip
if ($Zip) {
    $releaseDir = Join-Path $root "release\explauncher"
    $zipPath = Join-Path $releaseDir "explauncher.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Host ""
    Write-Host "build: zipping      $zipPath"
    Compress-Archive -Path (Join-Path $releaseDir "*") -DestinationPath $zipPath -CompressionLevel Optimal -Force
    $zipItem = Get-Item $zipPath
    $zipMsg = "build: zip          {0:N1} MB" -f ($zipItem.Length / 1MB)
    Write-Host $zipMsg
}

Write-Host ""
Write-Host "build: OK" -ForegroundColor Green
