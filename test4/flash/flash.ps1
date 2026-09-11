# ============================================================================
#  One-click flash test4.elf to STM32F103C8T6 via OpenOCD
#  (bypasses ST anti-counterfeit check -> works with clone ST-Link / clone MCU)
#
#  Usage:
#    .\flash\flash.ps1                                          # auto-detect
#    .\flash\flash.ps1 -OpenOcd D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe
#    $env:OPENOCD="...openocd.exe"; .\flash\flash.ps1
#
#  Board must be powered and ST-Link wired: SWDIO/SWCLK/GND/3V3 (see cfg).
# ============================================================================
param(
    [string]$OpenOcd = $env:OPENOCD,
    [string]$Elf     = $null,
    [string]$Cfg     = (Join-Path $PSScriptRoot "openocd_bluepill.cfg")
)

$ErrorActionPreference = "Stop"

# Auto-locate firmware: prefer Release, fall back to Debug
# (NOTE: keep this file pure ASCII - Windows PowerShell 5.1 reads .ps1 as ANSI/GBK
#  when there is no BOM, so non-ASCII comments break parsing.)
if (-not $Elf) {
    $rel = Join-Path $PSScriptRoot "..\build\Release\test4.elf"
    $dbg = Join-Path $PSScriptRoot "..\build\Debug\test4.elf"
    if (Test-Path $rel) { $Elf = $rel } else { $Elf = $dbg }
}

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
    throw "openocd.exe not found. Install xPack OpenOCD, then pass -OpenOcd or set env OPENOCD."
}

function Get-ScriptsDir {
    param([string]$Exe)
    foreach ($rel in @("..\openocd\scripts", "..\share\openocd\scripts", "..\..\share\openocd\scripts")) {
        $s = Join-Path (Split-Path $Exe -Parent) $rel
        if (Test-Path $s) { return (Resolve-Path $s).Path }
    }
    throw "Cannot find OpenOCD scripts dir next to openocd.exe."
}

$exe    = Find-OpenOcd -Hint $OpenOcd
$scripts = Get-ScriptsDir $exe
if (-not (Test-Path $Elf))  { throw "Firmware not found: $Elf" }
if (-not (Test-Path $Cfg))  { throw "Config not found: $Cfg" }

Write-Host "OpenOCD : $exe" -ForegroundColor Cyan
Write-Host "Scripts : $scripts"
Write-Host "Firmware: $Elf"
Write-Host "Config  : $Cfg"
Write-Host "---- flashing (Ctrl+C to abort) ----"

# OpenOCD parses the -c script as Tcl, where backslashes are escapes.
# Use forward slashes for the image path (OpenOCD accepts them on Windows).
$ElfFwd = $Elf.Replace('\', '/')
& $exe -s $scripts -f $Cfg -c "init; halt; program $ElfFwd verify reset exit"
$code = $LASTEXITCODE
Write-Host "OpenOCD exit code: $code"
exit $code
