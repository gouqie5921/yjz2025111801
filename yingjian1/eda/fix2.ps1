# Iterate the overlap repair: read repair.tsv (pin/kind/net) and re-place each pin's marker
# with the planner (fresh read per pin). Idempotent. ASCII only.
$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$proj = 'yingjian1'
$doc = 'P1'

$lines = Get-Content "$w\repair.tsv" | Where-Object { $_.Trim() -ne '' }
Write-Output ("pins to re-place: " + $lines.Count)
$ok = 0; $fail = 0
foreach ($line in $lines) {
  $f = $line -split "`t"
  $pin = $f[0]; $kind = $f[1]; $net = $f[2]
  $null = (& $exe sch disconnect --project $proj --doc $doc --pin $pin 2>&1 | Out-String)
  $out = (& $exe sch autoconnect --project $proj --doc $doc --pin $pin --kind $kind --net $net `
      --offset-min 20 --offset-max 400 --json 2>&1 | Out-String)
  if ($out -match '"ok":\s*true') { $ok++ } else { $fail++; Write-Output "  FAIL $pin -> $net" }
}
Write-Output ("re-place done: ok={0} fail={1}" -f $ok, $fail)
