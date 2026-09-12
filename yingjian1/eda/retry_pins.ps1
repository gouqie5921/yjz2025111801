# Retry the pins that failed wiring, escalating the stub range; fall back to explicit
# sch connect --direction/--offset when the planner cannot find a free slot.
# ASCII only. Usage: & retry_pins.ps1
$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$proj = 'yingjian1'
$doc = 'P1'

$pins = @(
  @('U2:3', 'power', '+5V'), @('C2:1', 'power', '+5V'), @('J6:3', 'power', '+5V'),
  @('J3:1', 'power', '+5V'), @('C1:1', 'power', '+5V'),
  @('D1:2', 'power', 'VBUS'), @('J1:B4A9', 'power', 'VBUS'),
  @('R2:2', 'gnd', 'GND'), @('J1:A1B12', 'gnd', 'GND'), @('J1:10', 'gnd', 'GND')
)
$ranges = @(@(20, 200), @(120, 400), @(200, 700))
$still = @()
foreach ($p in $pins) {
  $done = $false
  foreach ($r in $ranges) {
    $out = (& $exe sch autoconnect --project $proj --doc $doc --pin $p[0] --kind $p[1] --net $p[2] `
        --offset-min $r[0] --offset-max $r[1] --json 2>&1 | Out-String)
    if ($out -match '"ok":\s*true') {
      Write-Output ("ok   {0,-12} {1} (range {2}-{3})" -f $p[0], $p[2], $r[0], $r[1]); $done = $true; break
    }
  }
  if (-not $done) {
    # explicit fallback: short stub pointing down, no scorer involved
    $dir = if ($p[1] -eq 'gnd') { 'down' } else { 'up' }
    $out2 = (& $exe sch connect --project $proj --doc $doc --pin $p[0] --kind $p[1] --net $p[2] `
        --direction $dir --offset 20 --json 2>&1 | Out-String)
    if ($out2 -match '"ok":\s*true') { Write-Output ("ok*  {0,-12} {1} (explicit {2}/20)" -f $p[0], $p[2], $dir) }
    else { $still += $p[0]; Write-Output ("FAIL {0,-12} {1}" -f $p[0], $p[2]) }
  }
}
Write-Output ("still failing: " + ($(if ($still.Count) { $still -join ' ' } else { 'none' })))
