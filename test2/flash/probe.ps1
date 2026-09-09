# ============================================================================
#  Test SWD connectivity only (does NOT flash). Run this first.
#  Usage same as flash.ps1 (-OpenOcd / env OPENOCD).
#  Success looks like: "... Examination succeed" then a halted target message.
# ============================================================================
param(
    [string]$OpenOcd = $env:OPENOCD,
    [string]$Cfg     = (Join-Path $PSScriptRoot "openocd_bluepill.cfg")
)
$ErrorActionPreference = "Stop"

function Find-OpenOcd {
    param([string]$Hint)
    if ($Hint -and (Test-Path $Hint)) { return (Resolve-Path $Hint).Path }
    $cands = @(
        "$env:APPDATA\xPacks\openocd",
        "$env:APPDATA\xPacks\@xpack-dev-tools\openocd",
        "$env:LOCALAPPDATA\xPacks\@xpack-dev-tools\openocd",
        "$env:ProgramData\chocolatey\bin\openocd.exe",
        "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\openocd-esp32"
    )
    foreach ($c in $cands) {
        if (Test-Path $c) {
            $hits = Get-ChildItem $c -Recurse -Filter openocd.exe -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($hits) { return $hits.FullName }
        }
    }
    throw "openocd.exe not found. Pass -OpenOcd or set env OPENOCD."
}
function Get-ScriptsDir {
    param([string]$Exe)
    foreach ($rel in @("..\openocd\scripts", "..\share\openocd\scripts", "..\..\share\openocd\scripts")) {
        $s = Join-Path (Split-Path $Exe -Parent) $rel
        if (Test-Path $s) { return (Resolve-Path $s).Path }
    }
    throw "Cannot find OpenOCD scripts dir."
}

$exe = Find-OpenOcd $OpenOcd
$scripts = Get-ScriptsDir $exe
Write-Host "OpenOCD : $exe" -ForegroundColor Cyan
Write-Host "---- connecting and halting target ----"
& $exe -s $scripts -f $Cfg -c "init; halt; exit"
Write-Host "OpenOCD exit code: $LASTEXITCODE"
