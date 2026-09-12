# Finish the sequential wiring (idempotent: pins already on their target net are skipped).
# Run this in the background -- 178 sequential CLI calls exceed the foreground time cap.
# ASCII only.
$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$proj = 'yingjian1'
$doc = 'P1'

$nl = [System.IO.File]::ReadAllText("$w\netlist.json", [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$rank = @{ 'netport' = 0; 'power' = 1; 'gnd' = 2 }
$conns = $nl.connections | Sort-Object -Property @{ Expression = { $rank[$_.kind] } }
$i = 0; $ok = 0; $fail = 0
$failList = @()
foreach ($c in $conns) {
  $i++
  $out = (& $exe sch autoconnect --project $proj --doc $doc --pin $c.pin --kind $c.kind --net $c.net `
      --offset-min 20 --offset-max 400 --json 2>&1 | Out-String)
  if ($out -match '"ok":\s*true') { $ok++ } else { $fail++; $failList += "$($c.pin)->$($c.net)" }
  if ($i % 20 -eq 0) { Write-Output ("  {0}/{1} ok={2} fail={3}" -f $i, $conns.Count, $ok, $fail) }
}
Write-Output ("WIRING DONE: ok={0} fail={1}" -f $ok, $fail)
if ($failList.Count) { Write-Output ("failed: " + ($failList -join ' ')) }
