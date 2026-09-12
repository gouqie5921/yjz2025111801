# Repair overlapping net markers: for each pin in repair.tsv, remove its stub+marker and
# re-run the pin-aware planner with a longer stub range so the marker lands in free space.
# ASCII only (PowerShell 5.1 + BOM-less UTF-8 = ANSI parsing hazards).
# Usage: & fix_markers.ps1 [-MaxOffset 240] [-MinOffset 60]
param(
  [int]$MinOffset = 60,
  [int]$MaxOffset = 240
)
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$w = 'D:\Projecttest\yingjian1\eda'
$proj = 'yingjian1'
$doc = 'P1'

$lines = Get-Content "$w\repair.tsv" | Where-Object { $_.Trim() -ne '' }
Write-Output ("repair pins: " + $lines.Count)
$ok = 0; $fail = 0
foreach ($line in $lines) {
  $f = $line -split "`t"
  $pin = $f[0]; $kind = $f[1]; $net = $f[2]
  $null = (& $exe sch disconnect --project $proj --doc $doc --pin $pin 2>&1 | Out-String)
  $out = (& $exe sch autoconnect --project $proj --doc $doc --pin $pin --kind $kind --net $net --offset-min $MinOffset --offset-max $MaxOffset --json 2>&1 | Out-String)
  if ($out -match '"ok":\s*true') { $ok++; Write-Output ("  ok   {0,-10} {1} -> {2}" -f $pin, $kind, $net) }
  else { $fail++; Write-Output ("  FAIL {0,-10} {1} -> {2}" -f $pin, $kind, $net) }
  Start-Sleep -Milliseconds 300
}
Write-Output ("done: ok=$ok fail=$fail")
