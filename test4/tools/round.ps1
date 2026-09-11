# ============================================================================
#  round.ps1 - one tuning round: build -> flash -> capture -> evaluate.
#
#  Build uses build_cli.ps1 (direct arm-none-eabi-gcc) because ninja hangs in
#  this sandbox when it spawns the compiler with piped stdio.
#  Output firmware: build\cli\test4.elf
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File .\tools\round.ps1
#    powershell -ExecutionPolicy Bypass -File .\tools\round.ps1 -Seconds 15 -SkipFlash
#
#  Requires: VOFA+ / other serial tools CLOSED (COM port is exclusive),
#            ST-Link + USB-TTL connected, board powered.
#
#  NOTE 1: keep this file pure ASCII (PS 5.1 reads .ps1 as ANSI/GBK without BOM).
#  NOTE 2: ErrorActionPreference stays "Continue" on purpose - OpenOCD writes
#          its banner to stderr, and with "Stop" PS would abort a perfectly
#          fine flash. Exit codes are checked manually instead.
# ============================================================================
param(
    [int]   $Seconds   = 15,
    [string]$Port      = "COM7",
    [switch]$SkipFlash
)

$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$ocd  = "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
$ps51 = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$elf  = Join-Path $root "build\cli\test4.elf"

Write-Host "===== [1/3] BUILD =====" -ForegroundColor Cyan
$bout = & $ps51 -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_cli.ps1") 2>&1
$brc  = $LASTEXITCODE
$bout | Where-Object { $_ -match "error|warning:|RAM:|FLASH:|build exit|OK ->" } | ForEach-Object { ($_ -as [string]).Trim() }
if (($brc -ne 0) -or (-not (Test-Path $elf))) { Write-Host "BUILD FAILED" -ForegroundColor Red; exit 1 }

if (-not $SkipFlash) {
    Write-Host "===== [2/3] FLASH =====" -ForegroundColor Cyan
    $fout = & $ps51 -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "flash\flash.ps1") -OpenOcd $ocd -Elf $elf 2>&1
    $frc  = $LASTEXITCODE
    $fout | Where-Object { $_ -match "Programming Finished|Verified OK|Firmware not found|OpenOCD exit" } | ForEach-Object { ($_ -as [string]).Trim() }
    if ($frc -ne 0) { Write-Host ("FLASH FAILED (exit " + $frc + ")") -ForegroundColor Red; exit 2 }
    Write-Host "flash OK" -ForegroundColor Green
}

Write-Host "===== [3/3] CAPTURE + EVALUATE =====" -ForegroundColor Cyan
Start-Sleep -Seconds 2
& $ps51 -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "measure_step.ps1") -Port $Port -Seconds $Seconds
exit $LASTEXITCODE
