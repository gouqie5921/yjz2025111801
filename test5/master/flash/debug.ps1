# ============================================================================
#  Terminal GDB debugging over OpenOCD (no ST gdbserver -> works with clones)
#
#  What it does:
#    1) starts OpenOCD as a GDB server (new minimized window, port 3333)
#    2) connects the bundled arm-none-eabi-gdb, resets to main, loads the elf
#    3) drops you into the gdb prompt: set breakpoints, `continue`, step, etc.
#
#  Typical gdb commands:
#      b main            - breakpoint at main
#      b servo_set_angle - breakpoint in function
#      c                 - continue (run)
#      p cmd_angle       - print variable
#      n / s             - step over / step into
#      monitor reset halt
#      quit              - exit gdb (OpenOCD is stopped automatically)
#
#  Usage:
#    .\flash\debug.ps1                                  # auto-detect openocd
#    .\flash\debug.ps1 -OpenOcd D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe
# ============================================================================
param(
    [string]$OpenOcd = $env:OPENOCD,
    [string]$Elf     = $null,
    [string]$Cfg     = (Join-Path $PSScriptRoot "openocd_bluepill.cfg")
)
$ErrorActionPreference = "Stop"

# Auto-locate firmware: prefer build\cli (direct gcc build), then Release/Debug
# (NOTE: keep this file pure ASCII - see flash.ps1 comment)
if (-not $Elf) {
    $rel = Join-Path $PSScriptRoot "..\build\cli\master.elf"
    $dbg = Join-Path $PSScriptRoot "..\build\Debug\master.elf"
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
function Find-Gdb {
    $g = Get-Command arm-none-eabi-gdb -ErrorAction SilentlyContinue
    if ($g) { return $g.Source }
    $bundle = "$env:LOCALAPPDATA\stm32cube\bundles"
    if (Test-Path $bundle) {
        $h = Get-ChildItem $bundle -Recurse -Filter arm-none-eabi-gdb.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($h) { return $h.FullName }
    }
    return $null
}

$exe   = Find-OpenOcd $OpenOcd
$scripts = Get-ScriptsDir $exe
$gdb   = Find-Gdb
if (-not $gdb) { throw "arm-none-eabi-gdb not found." }
if (-not (Test-Path $Elf)) { throw "Firmware not found: $Elf (build first)" }

Write-Host "OpenOCD : $exe" -ForegroundColor Cyan
Write-Host "GDB     : $gdb"
Write-Host "Firmware: $Elf"

# 1) OpenOCD as GDB server (own window so it keeps running while gdb is attached)
$ocd = Start-Process -FilePath $exe -ArgumentList @("-s", "`"$scripts`"", "-f", "`"$Cfg`"") -PassThru -WindowStyle Minimized

try {
    # wait for gdb port 3333
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            $c = New-Object Net.Sockets.TcpClient("127.0.0.1", 3333)
            $c.Close()
            $ready = $true
            break
        } catch { }
    }
    if (-not $ready) { throw "OpenOCD did not open gdb port 3333 - check probe/board connection." }

    # 2) attach gdb: reset to main, load program, drop to interactive prompt
    & $gdb -q -ex "target extended-remote localhost:3333" `
             -ex "monitor reset halt" `
             -ex "load" `
             -ex "monitor reset halt" `
             $Elf
} finally {
    if ($ocd -and -not $ocd.HasExited) {
        Write-Host "Stopping OpenOCD..." -ForegroundColor Yellow
        Stop-Process -Id $ocd.Id -Force -ErrorAction SilentlyContinue
    }
}
